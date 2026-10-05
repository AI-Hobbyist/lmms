#include "agent/ExportCommands.h"
#include "agent/CommandBus.h"

#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QJsonArray>
#include <QPointer>
#include <QSaveFile>
#include <QTemporaryFile>
#include <QUuid>

#include <algorithm>
#include <array>
#include <deque>
#include <memory>

#include "AudioEngine.h"
#include "Engine.h"
#include "OutputSettings.h"
#include "ProjectRenderer.h"
#include "Song.h"
#include "Timeline.h"

namespace lmms::agent
{
namespace
{
struct TimelineState
{
	tick_t ticks;
	float frameOffset;
	double seconds;
	TimePos playStart;
};

struct ExportTask
{
	QString id;
	QString path;
	QString status = "running";
	QString error;
	bool overwrite = false;
	int progress = 0;
	qint64 bytes = 0;
	qint64 durationMs = 0;
	QJsonObject settings;
	QPointer<ProjectRenderer> renderer;
	std::unique_ptr<QTemporaryFile> temporary;
	Song::PlaybackState playback{};
	std::array<TimelineState, Song::PlayModeCount> timelines{};
	TimePos loopBegin;
	TimePos loopEnd;
	bool loopEnabled = false;
	bool exportLoop = false;
	bool betweenMarkers = false;
	int loopCount = 1;
	bool modified = false;
	QElapsedTimer elapsed;
};

std::deque<std::shared_ptr<ExportTask>> tasks;

QJsonObject taskInfo(const ExportTask& task)
{
	auto result = task.settings;
	result.insert("task", task.id);
	result.insert("path", task.path);
	result.insert("status", task.status);
	result.insert("progress", task.renderer ? std::clamp(task.renderer->progressPercent(), 0, 100) : task.progress);
	result.insert("bytes", task.bytes);
	result.insert("elapsedMs", task.status == "running" ? task.elapsed.elapsed() : task.durationMs);
	if (!task.error.isEmpty()) { result.insert("error", task.error); }
	return result;
}

bool commitFile(ExportTask& task)
{
	QFile source(task.temporary->fileName());
	if (!source.open(QIODevice::ReadOnly) || source.size() == 0)
	{
		task.error = "The renderer did not produce an audio file.";
		return false;
	}
	task.bytes = source.size();
	source.close();
	if (!QFileInfo::exists(task.path))
	{
		if (source.rename(task.path)) { return true; }
	}
	if (!task.overwrite && QFileInfo::exists(task.path))
	{
		task.error = "The destination appeared during rendering; overwrite was not authorized.";
		return false;
	}
	if (!source.open(QIODevice::ReadOnly)) { task.error = source.errorString(); return false; }
	QSaveFile destination(task.path);
	if (!destination.open(QIODevice::WriteOnly)) { task.error = destination.errorString(); return false; }
	while (!source.atEnd())
	{
		const auto buffer = source.read(65536);
		if (buffer.isEmpty() || destination.write(buffer) != buffer.size())
		{
			task.error = "Could not commit the rendered audio file.";
			return false;
		}
	}
	if (!destination.commit()) { task.error = destination.errorString(); return false; }
	return true;
}

void finishTask(const std::shared_ptr<ExportTask>& task, bool cancelled = false)
{
	if (task->status != "running" || !task->renderer) { return; }
	auto* renderer = task->renderer.data();
	QObject::disconnect(renderer, nullptr, Engine::audioEngine(), nullptr);
	renderer->wait();
	const bool succeeded = renderer->renderSucceeded();
	if(!succeeded&&!renderer->renderError().isEmpty()) task->error=renderer->renderError();
	task->progress = std::clamp(renderer->progressPercent(), 0, 100);
	// Closing the encoder finalizes its header before the temporary file can be committed.
	Engine::audioEngine()->restoreAudioDevice();
	{
		const auto guard = Engine::audioEngine()->requestChangesGuard();
		auto* song = Engine::getSong();
		auto& timeline = song->getTimeline(Song::PlayMode::Song);
		timeline.setLoopPoints(task->loopBegin, task->loopEnd);
		timeline.setLoopEnabled(task->loopEnabled);
		song->setExportLoop(task->exportLoop);
		song->setRenderBetweenMarkers(task->betweenMarkers);
		song->setLoopRenderCount(task->loopCount);
		for (std::size_t index = 0; index < task->timelines.size(); ++index)
		{
			const auto& saved = task->timelines[index];
			song->getTimeline(static_cast<Song::PlayMode>(index)).restorePlaybackPosition(
				saved.ticks, saved.frameOffset, saved.seconds, saved.playStart);
		}
		song->restorePlaybackState(task->playback);
		song->setModified(task->modified);
	}
	delete renderer;
	task->renderer = nullptr;
	if (cancelled) { task->status = "cancelled"; }
	else if (succeeded && commitFile(*task)) { task->status = "completed"; task->progress = 100; }
	else
	{
		task->status = "failed";
		if (task->error.isEmpty()) { task->error = "The audio encoder failed while rendering."; }
	}
	task->temporary.reset();
	task->durationMs = task->elapsed.elapsed();
}

std::shared_ptr<ExportTask> findTask(const QString& id)
{
	const auto found = std::find_if(tasks.begin(), tasks.end(), [&id](const auto& task) { return task->id == id; });
	return found == tasks.end() ? nullptr : *found;
}

CommandResult exportAudio(const QJsonObject& arguments)
{
	auto* song = Engine::getSong();
	if (!song || !Engine::audioEngine()) { return CommandResult::failure("engine_unavailable", "The audio engine is unavailable."); }
	if (CommandBus::instance().batchDepth() != 0)
	{
		return CommandResult::failure("external_write_in_batch", "Start audio exports after the project transaction has committed.");
	}
	if (hasActiveAudioExport() || song->isExporting()) { return CommandResult::failure("export_busy", "An audio export is already active."); }
	const QFileInfo destination(arguments.value("path").toString());
	if (arguments.value("path").toString().isEmpty() || destination.isDir() || !destination.dir().exists())
	{
		return CommandResult::failure("invalid_arguments", "The export path requires an existing parent directory.");
	}
	const bool overwrite = arguments.value("overwrite").toBool();
	if (destination.exists() && !overwrite) { return CommandResult::failure("file_exists", "Specify overwrite:true to replace the existing file."); }
	auto formatName = arguments.value("format").toString("wav").toLower();
	if (formatName == "wave") { formatName = "wav"; }
	const auto format = formatName == "wav" ? ProjectRenderer::ExportFileFormat::Wave :
		formatName == "flac" ? ProjectRenderer::ExportFileFormat::Flac :
		formatName == "ogg" ? ProjectRenderer::ExportFileFormat::Ogg : ProjectRenderer::ExportFileFormat::MP3;
	if (!ProjectRenderer::fileEncodeDevices[static_cast<std::size_t>(format)].isAvailable())
	{
		return CommandResult::failure("encoder_unavailable", "This LMMS build does not include the requested audio encoder.");
	}
	const auto quality = arguments.value("quality").toObject();
	const int sampleRate = quality.value("sampleRate").toInt(44100);
	const int bitRate = quality.value("bitrate").toInt(192);
	const int bits = quality.value("bitDepth").toInt(24);
	const auto stereoName = quality.value("stereoMode").toString("stereo");
	if ((formatName == "ogg" || formatName == "mp3") && bitRate > 320)
	{
		return CommandResult::failure("invalid_arguments", "OGG and MP3 bitrate must not exceed 320 kbit/s.");
	}
	if ((formatName != "mp3" && stereoName != "stereo") || (formatName == "flac" && bits == 32))
	{
		return CommandResult::failure("invalid_arguments", "Mono/joint stereo require MP3; FLAC supports 16 or 24 bit output.");
	}
	const auto depth = bits == 16 ? OutputSettings::BitDepth::Depth16Bit : bits == 24 ?
		OutputSettings::BitDepth::Depth24Bit : OutputSettings::BitDepth::Depth32Bit;
	const auto stereo = stereoName == "mono" ? OutputSettings::StereoMode::Mono : stereoName == "joint" ?
		OutputSettings::StereoMode::JointStereo : OutputSettings::StereoMode::Stereo;
	OutputSettings settings(sampleRate, bitRate, depth, stereo);
	settings.setCompressionLevel(quality.value("compression").toDouble(0.625));
	settings.setInteractiveErrors(false);
	const bool hasRange = arguments.contains("range");
	const auto range = arguments.value("range").toObject();
	const int start = range.value("start").toInt();
	const int end = range.value("end").toInt();
	if (hasRange && (start >= end || end > MaxSongLength))
	{
		return CommandResult::failure("invalid_arguments", "The export range must have an end after its start, within the song limit.");
	}
	const int loopCount = arguments.value("loopCount").toInt(1);
	const auto& existingTimeline = song->getTimeline(Song::PlayMode::Song);
	const auto loopLength = hasRange ? end - start : existingTimeline.loopEnd() - existingTimeline.loopBegin();
	if (loopCount > 1 && loopLength <= 0) { return CommandResult::failure("invalid_arguments", "Repeated exports require a nonempty loop range."); }
	if (static_cast<qint64>(loopLength) * loopCount > MaxSongLength)
	{
		return CommandResult::failure("invalid_arguments", "The effective repeated range exceeds the song limit.");
	}
	QJsonObject result{ {"path", destination.absoluteFilePath()}, {"format", formatName}, {"loopCount", loopCount},
		{"quality", QJsonObject{ {"sampleRate", sampleRate}, {"bitrate", bitRate}, {"bitDepth", bits},
			{"stereoMode", stereoName}, {"compression", settings.getCompressionLevel()} }} };
	if (hasRange) { result.insert("range", range); }
	if (arguments.value("dryRun").toBool()) { result.insert("wouldStart", true); return CommandResult::success(result); }
	auto task = std::make_shared<ExportTask>();
	task->id = QUuid::createUuid().toString(QUuid::WithoutBraces);
	task->path = destination.absoluteFilePath();
	task->overwrite = overwrite;
	task->settings = result;
	task->temporary = std::make_unique<QTemporaryFile>(destination.dir().filePath(".lmms-render-XXXXXX"));
	if (!task->temporary->open()) { return CommandResult::failure("export_failed", task->temporary->errorString()); }
	task->temporary->close();
	auto renderer = std::make_unique<ProjectRenderer>(settings, format, task->temporary->fileName());
	if (!renderer->isReady()) { return CommandResult::failure("export_failed", "The audio encoder could not open its output device."); }
	{
		const auto guard = Engine::audioEngine()->requestChangesGuard();
		task->playback = song->capturePlaybackState();
		task->modified = song->isModified();
		task->loopBegin = existingTimeline.loopBegin();
		task->loopEnd = existingTimeline.loopEnd();
		task->loopEnabled = existingTimeline.loopEnabled();
		task->loopCount = song->getLoopRenderCount();
		task->exportLoop = song->exportLoop();
		task->betweenMarkers = song->renderBetweenMarkers();
		for (std::size_t index = 0; index < task->timelines.size(); ++index)
		{
			const auto& timeline = song->getTimeline(static_cast<Song::PlayMode>(index));
			task->timelines[index] = {timeline.ticks(), timeline.frameOffset(), timeline.getElapsedSeconds(), timeline.playStartPosition()};
		}
		song->stop();
		if (hasRange) { song->getTimeline(Song::PlayMode::Song).setLoopPoints(TimePos(start), TimePos(end)); }
		song->setRenderBetweenMarkers(hasRange);
		song->setLoopRenderCount(loopCount);
		song->setExportLoop(arguments.value("asLoop").toBool());
	}
	while (tasks.size() >= 64 && tasks.front()->status != "running") { tasks.pop_front(); }
	tasks.push_back(task);
	task->renderer = renderer.release();
	task->elapsed.start();
	QObject::connect(task->renderer.data(), &ProjectRenderer::finished, Engine::audioEngine(), [task] { finishTask(task); }, Qt::QueuedConnection);
	Engine::audioEngine()->storeAudioDevice();
	task->renderer->startProcessing();
	return CommandResult::success(taskInfo(*task));
}

QJsonObject integerSchema(int minimum, int maximum)
{
	return { {"type", "integer"}, {"minimum", minimum}, {"maximum", maximum} };
}

QJsonObject objectSchema(QJsonObject properties, QJsonArray required = {})
{
	return { {"type", "object"}, {"properties", properties}, {"required", required}, {"additionalProperties", false} };
}
} // namespace

bool hasActiveAudioExport()
{
	return std::any_of(tasks.begin(), tasks.end(), [](const auto& task) { return task->status == "running"; });
}

void shutdownAudioExports()
{
	for (const auto& task : tasks)
	{
		if (task->status == "running" && task->renderer)
		{
			QObject::disconnect(task->renderer.data(), nullptr, Engine::audioEngine(), nullptr);
			task->renderer->abortProcessing();
			finishTask(task, true);
		}
	}
	tasks.clear();
}

void registerExportCommands(CommandBus& bus)
{
	CommandDescriptor audio;
	audio.name = "export.audio";
	audio.summary = "Start a local audio render; returns a task for export.status/cancel. Range uses song ticks. Output files are committed only on success.";
	audio.mutability = Mutability::Destructive;
	audio.scope = TxScope::None;
	audio.argsSchema = objectSchema({ {"path", QJsonObject{ {"type", "string"}, {"minLength", 1} }},
		{"format", QJsonObject{ {"type", "string"}, {"enum", QJsonArray{"wav", "wave", "flac", "ogg", "mp3"}} }},
		{"overwrite", QJsonObject{ {"type", "boolean"} }}, {"asLoop", QJsonObject{ {"type", "boolean"} }},
		{"loopCount", integerSchema(1, 512)},
		{"range", objectSchema({ {"start", integerSchema(0, MaxSongLength)}, {"end", integerSchema(1, MaxSongLength)} }, {"start", "end"})},
		{"quality", objectSchema({ {"sampleRate", integerSchema(8000, 384000)}, {"bitrate", integerSchema(8, 512)},
			{"bitDepth", QJsonObject{ {"type", "integer"}, {"enum", QJsonArray{16, 24, 32}} }},
			{"stereoMode", QJsonObject{ {"type", "string"}, {"enum", QJsonArray{"mono", "stereo", "joint"}} }},
			{"compression", QJsonObject{ {"type", "number"}, {"minimum", 0}, {"maximum", 1}}} })} }, {"path"});
	audio.handler = exportAudio;
	bus.registerCommand(audio);
	CommandDescriptor status;
	status.name = "export.status";
	status.summary = "Query an audio export task, or list up to 64 recent tasks when task is omitted.";
	status.argsSchema = objectSchema({ {"task", QJsonObject{ {"type", "string"}}} });
	status.handler = [](const QJsonObject& arguments) {
		if (arguments.contains("task"))
		{
			const auto task = findTask(arguments.value("task").toString());
			if (!task) { return CommandResult::failure("export_task_not_found", "The export task is unknown or has expired."); }
			if (task->renderer && task->renderer->isFinished()) { finishTask(task); }
			return CommandResult::success(taskInfo(*task));
		}
		QJsonArray result;
		for (const auto& task : tasks)
		{
			if (task->renderer && task->renderer->isFinished()) { finishTask(task); }
			result.append(taskInfo(*task));
		}
		return CommandResult::success({ {"tasks", result} });
	};
	bus.registerCommand(status);
	CommandDescriptor cancel;
	cancel.name = "export.cancel";
	cancel.summary = "Cancel an audio export and restore the device and transport. Completed/cancelled tasks remain queryable.";
	cancel.mutability = Mutability::Mutating;
	cancel.argsSchema = objectSchema({ {"task", QJsonObject{ {"type", "string"}}} }, {"task"});
	cancel.handler = [](const QJsonObject& arguments) {
		const auto task = findTask(arguments.value("task").toString());
		if (!task) { return CommandResult::failure("export_task_not_found", "The export task is unknown or has expired."); }
		if (arguments.value("dryRun").toBool())
		{
			auto result = taskInfo(*task);
			result.insert("wouldCancel", task->status == "running");
			return CommandResult::success(result);
		}
		if (task->status == "running" && task->renderer)
		{
			QObject::disconnect(task->renderer.data(), nullptr, Engine::audioEngine(), nullptr);
			task->renderer->abortProcessing();
			finishTask(task, true);
		}
		return CommandResult::success(taskInfo(*task));
	};
	bus.registerCommand(cancel);
}
} // namespace lmms::agent
