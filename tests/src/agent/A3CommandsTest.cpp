#include <QtTest>

#include <QDataStream>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>

#include <cmath>
#include <memory>

#include "agent/CommandBus.h"
#include "agent/ExportCommands.h"
#include "Engine.h"
#include "MidiClip.h"
#include "PatternStore.h"
#include "PatternTrack.h"
#include "ControllerConnection.h"
#include "ConfigManager.h"
#include "InstrumentTrack.h"
#include "Piano.h"
#include "PluginFactory.h"
#include "ProjectJournal.h"
#include "ProjectRenderer.h"
#include "SampleBuffer.h"
#include "SampleFrame.h"
#include "Song.h"
#include "Timeline.h"

namespace {

QString writeTestWave(QTemporaryDir& directory, bool audible = false)
{
	const QString path = directory.filePath("test.wav");
	QFile file(path);
	if (!file.open(QIODevice::WriteOnly))
	{
		return {};
	}

	constexpr quint32 sampleRate = 44100;
	constexpr quint32 frames = sampleRate;
	constexpr quint16 channels = 1;
	constexpr quint16 bitsPerSample = 16;
	constexpr quint32 dataBytes = frames * channels * bitsPerSample / 8;
	QDataStream stream(&file);
	stream.setByteOrder(QDataStream::LittleEndian);
	file.write("RIFF", 4);
	stream << quint32(36 + dataBytes);
	file.write("WAVEfmt ", 8);
	stream << quint32(16) << quint16(1) << channels << sampleRate;
	stream << quint32(sampleRate * channels * bitsPerSample / 8);
	stream << quint16(channels * bitsPerSample / 8) << bitsPerSample;
	file.write("data", 4);
	stream << dataBytes;
	if (audible)
	{
		for (quint32 frame = 0; frame < frames; ++frame)
		{
			stream << qint16(8192 * std::sin(6.283185307179586 * 440 * frame / sampleRate));
		}
	}
	else
	{
		file.write(QByteArray(static_cast<int>(dataBytes), '\0'));
	}
	return path;
}

} // namespace

class A3CommandsTest : public QObject
{
	Q_OBJECT
	std::unique_ptr<QTemporaryDir> m_shutdownDirectory;

private slots:
	void initTestCase() { lmms::Engine::init(true); }

	void cleanupTestCase()
	{
		lmms::Engine::destroy();
		QVERIFY(!lmms::agent::hasActiveAudioExport());
		m_shutdownDirectory.reset();
	}

	void init()
	{
		lmms::agent::shutdownAudioExports();
		lmms::Engine::getSong()->stop();
		lmms::Engine::getSong()->clearProject();
		lmms::Engine::projectJournal()->clearJournal();
	}

	void persistsCoreEditingAndProtectsSaveDestinations()
	{
		auto& bus = lmms::agent::CommandBus::instance();
		auto* song = lmms::Engine::getSong();
		QVERIFY(bus.execute("song.getInfo").ok);
		QVERIFY(bus.execute("song.setTimeSignature", {{"numerator", 3}, {"denominator", 4}}).ok);
		QCOMPARE(song->ticksPerBar(), 144);
		QVERIFY(bus.execute("song.setMasterVolume", {{"value", 75}}).ok);
		QVERIFY(bus.execute("song.setMasterPitch", {{"semitones", 2}}).ok);
		QCOMPARE(song->masterVolume(), 75);
		QCOMPARE(song->masterPitch(), 2);
		QVERIFY(bus.execute("transport.setPosition", {{"ticks", 48}}).ok);
		QCOMPARE(bus.execute("transport.getPosition").data.value("ticks").toInt(), 48);
		QVERIFY(bus.execute("track.create").ok);
		QVERIFY(bus.execute("track.setMuted", {{"track", 0}, {"value", true}}).ok);
		QVERIFY(song->tracks()[0]->isMuted());
		QVERIFY(bus.execute("track.setSolo", {{"track", 0}, {"value", true}}).ok);
		QVERIFY(song->tracks()[0]->isSolo());
		QVERIFY(!song->tracks()[0]->isMuted());
		QVERIFY(bus.execute("track.setSolo", {{"track", 0}, {"value", false}}).ok);
		QVERIFY(song->tracks()[0]->isMuted());
		QVERIFY(bus.execute("history.undo").ok);
		QVERIFY(song->tracks()[0]->isSolo());
		QVERIFY(!song->tracks()[0]->isMuted());
		QVERIFY(bus.execute("instrument.setPanning", {{"track", 0}, {"value", -20}}).ok);
		QVERIFY(bus.execute("instrument.setBaseNote", {{"track", 0}, {"value", 60}}).ok);
		QVERIFY(bus.execute("clip.create", {{"track", 0}}).ok);
		QVERIFY(bus.execute("clip.resize", {{"track", 0}, {"clip", 0}, {"length", 288}}).ok);
		QVERIFY(bus.execute("clip.setMute", {{"track", 0}, {"clip", 0}, {"value", true}}).ok);
		QCOMPARE(bus.execute("clip.list", {{"track", 0}}).data.value("clips").toArray().size(), 1);
		QVERIFY(!bus.execute("model.list", {{"prefix", "song/track:0"}}).data.value("models").toArray().isEmpty());
		QVERIFY(!bus.execute("model.search", {{"keyword", "volume"}}).data.value("models").toArray().isEmpty());
		QVERIFY(!bus.execute("mixer.listChannels").data.value("channels").toArray().isEmpty());
		QVERIFY(bus.execute("mixer.setMute", {{"channel", 0}, {"value", true}}).ok);
		QVERIFY(bus.execute("mixer.getMaster").data.value("muted").toBool());
		QTemporaryDir directory;
		QVERIFY(directory.isValid());
		const auto path = directory.filePath("settings.mmp");
		QVERIFY(bus.execute("song.save", {{"path", path}}).ok);
		QVERIFY(bus.execute("song.save").ok);
		const auto otherPath = directory.filePath("protected.mmp");
		QVERIFY(QFile::copy(path, otherPath));
		QCOMPARE(bus.execute("song.save", {{"path", otherPath}}).errorCode, QString("file_exists"));
		QVERIFY(bus.execute("song.save", {{"path", otherPath}, {"overwrite", true}}).ok);
		QVERIFY(bus.execute("song.load", {{"path", path}}).ok);
		QCOMPARE(song->masterVolume(), 75);
		QCOMPARE(song->masterPitch(), 2);
		QCOMPARE(song->ticksPerBar(), 144);
		auto* track = dynamic_cast<lmms::InstrumentTrack*>(song->tracks()[0]);
		QVERIFY(track);
		QCOMPARE(track->panningModel()->value(), -20.0f);
		QCOMPARE(track->baseNoteModel()->value(), 60);
		QCOMPARE(track->getClips()[0]->length().getTicks(), 288);
		QVERIFY(track->getClips()[0]->isMuted());
		QVERIFY(bus.execute("track.remove", {{"track", 0}}).ok);
		QCOMPARE(song->tracks().size(), std::size_t(0));
		QVERIFY(bus.execute("history.undo").ok);
		QCOMPARE(song->tracks().size(), std::size_t(1));
		const auto originalTempo = song->getTempo();
		QVERIFY(bus.beginBatch("save must not leak from a failed script"));
		QVERIFY(bus.execute("song.setTempo", {{"bpm", 200}}).ok);
		QCOMPARE(bus.execute("song.save").errorCode, QString("external_write_in_batch"));
		QCOMPARE(song->getTempo(), originalTempo);
	}

	void humanizesOnlySelectedNotesReproducibly()
	{
		auto& bus = lmms::agent::CommandBus::instance();
		QVERIFY(bus.execute("track.create").ok);
		QVERIFY(bus.execute("clip.create", {{"track", 0}}).ok);
		QVERIFY(
			bus.execute("midi.addNotes",
				   {{"track", 0}, {"clip", 0},
					   {"notes",
						   QJsonArray{QJsonObject{{"position", 24}, {"length", 12}, {"key", 60}, {"volume", 80}},
							   QJsonObject{{"position", 96}, {"length", 12}, {"key", 62}, {"volume", 90}}}}})
				.ok);
		const auto before = bus.execute("midi.getNotes", {{"track", 0}, {"clip", 0}}).data.value("notes").toArray();
		const QJsonObject arguments{{"track", 0}, {"clip", 0}, {"seed", 19}, {"timing", 4}, {"velocity", 8},
			{"range", QJsonObject{{"start", 0}, {"end", 48}}}, {"keys", QJsonArray{60}}};
		QCOMPARE(bus.execute("midi.humanize", arguments).data.value("changed").toInt(), 1);
		const auto after = bus.execute("midi.getNotes", {{"track", 0}, {"clip", 0}}).data.value("notes").toArray();
		QCOMPARE(after[1], before[1]);
		QVERIFY(after[0] != before[0]);
		QVERIFY(bus.execute("history.undo").ok);
		QVERIFY(bus.execute("midi.humanize", arguments).ok);
		QCOMPARE(bus.execute("midi.getNotes", {{"track", 0}, {"clip", 0}}).data.value("notes").toArray(), after);
	}

	void rendersAudioWithQueriesAndRestoresPlayback()
	{
		auto& bus = lmms::agent::CommandBus::instance();
		auto* song = lmms::Engine::getSong();
		QTemporaryDir directory;
		QVERIFY(directory.isValid());
		const auto sample = writeTestWave(directory, true);
		QVERIFY(bus.execute("import.sampleToTrack", {{"path", sample}}).ok);
		QVERIFY(bus.execute("song.setTempo", {{"bpm", 120}}).ok);
		QVERIFY(bus.execute("transport.play").ok);
		QVERIFY(bus.execute("transport.togglePause").ok);
		auto& timeline = song->getTimeline(lmms::Song::PlayMode::Song);
		timeline.setLoopPoints(lmms::TimePos(384), lmms::TimePos(768));
		timeline.setLoopEnabled(true);
		timeline.restorePlaybackPosition(37, 12.5f, 0.371, lmms::TimePos(20));
		song->setLoopRenderCount(3);
		song->setExportLoop(true);
		song->setRenderBetweenMarkers(true);
		song->setModified(false);
		const auto history = bus.execute("history.status").data;
		const auto originalRate = lmms::Engine::audioEngine()->outputSampleRate();
		const auto path = directory.filePath("试听.wav");
		const QJsonObject arguments{{"path", path}, {"format", "wav"},
			{"quality", QJsonObject{{"sampleRate", 48000}, {"bitDepth", 16}}},
			{"range", QJsonObject{{"start", 0}, {"end", 192}}}};
		auto previewArguments = arguments;
		previewArguments.insert("dryRun", true);
		QVERIFY(bus.execute("export.audio", previewArguments).ok);
		QVERIFY(!QFileInfo::exists(path));
		QVERIFY(!lmms::agent::hasActiveAudioExport());
		const auto started = bus.execute("export.audio", arguments);
		QVERIFY2(started.ok, qPrintable(started.errorMessage));
		const auto task = started.data.value("task").toString();
		QVERIFY(!task.isEmpty());
		QVERIFY(lmms::agent::hasActiveAudioExport());
		QCOMPARE(bus.execute("track.create").errorCode, QString("export_busy"));
		QCOMPARE(bus.execute("transport.stop").errorCode, QString("export_busy"));
		QCOMPARE(bus.execute("history.undo").errorCode, QString("export_busy"));
		QVERIFY(!bus.beginBatch("blocked while exporting"));
		QVERIFY(bus.execute("query.songSummary").ok);
		QTRY_VERIFY_WITH_TIMEOUT(
			bus.execute("export.status", {{"task", task}}).data.value("status").toString() != "running", 15000);
		const auto status = bus.execute("export.status", {{"task", task}});
		QCOMPARE(status.data.value("status").toString(), QString("completed"));
		QCOMPARE(status.data.value("progress").toInt(), 100);
		QVERIFY(!lmms::agent::hasActiveAudioExport());
		QVERIFY(!song->isExporting());
		QCOMPARE(song->playMode(), lmms::Song::PlayMode::Song);
		QVERIFY(song->isPaused());
		QCOMPARE(timeline.ticks(), 37);
		QCOMPARE(timeline.frameOffset(), 12.5f);
		QVERIFY(std::abs(timeline.getElapsedSeconds() - 0.371) < 1e-9);
		QCOMPARE(timeline.playStartPosition().getTicks(), 20);
		QCOMPARE(timeline.loopBegin().getTicks(), 384);
		QCOMPARE(timeline.loopEnd().getTicks(), 768);
		QVERIFY(timeline.loopEnabled());
		QCOMPARE(song->getLoopRenderCount(), 3);
		QVERIFY(song->exportLoop());
		QVERIFY(song->renderBetweenMarkers());
		QVERIFY(!song->isModified());
		QCOMPARE(lmms::Engine::audioEngine()->outputSampleRate(), originalRate);
		QCOMPARE(bus.execute("history.status").data, history);
		const auto audio = lmms::SampleBuffer::fromFile(path);
		QVERIFY(audio && !audio->empty());
		QCOMPARE(audio->sampleRate(), lmms::sample_rate_t(48000));
		QVERIFY(std::abs(static_cast<double>(audio->size()) / audio->sampleRate() - 2.0) < 0.05);
		float peak = 0;
		for (const auto& frame : *audio)
		{
			peak = std::max(peak, std::abs(frame[0]));
		}
		QVERIFY2(peak > 0.1f && peak < 0.6f, qPrintable(QString::number(peak)));
		QCOMPARE(bus.execute("export.audio", arguments).errorCode, QString("file_exists"));
		QCOMPARE(bus.execute("export.cancel", {{"task", task}}).data.value("status").toString(), QString("completed"));
		QVERIFY(!bus.execute("export.status", {{"task", "missing"}}).ok);
		QCOMPARE(bus.execute("export.status").data.value("tasks").toArray().size(), 1);
	}

	void rendersAvailableFormatsAndRepeatedRanges()
	{
		auto& bus = lmms::agent::CommandBus::instance();
		QTemporaryDir directory;
		QVERIFY(directory.isValid());
		QVERIFY(bus.execute("import.sampleToTrack", {{"path", writeTestWave(directory, true)}}).ok);
		QVERIFY(bus.execute("song.setTempo", {{"bpm", 120}}).ok);
		for (const auto& format : {QString("wav"), QString("flac"), QString("ogg"), QString("mp3")})
		{
			const auto type = format == "wav" ? lmms::ProjectRenderer::ExportFileFormat::Wave
				: format == "flac"			  ? lmms::ProjectRenderer::ExportFileFormat::Flac
				: format == "ogg"			  ? lmms::ProjectRenderer::ExportFileFormat::Ogg
											  : lmms::ProjectRenderer::ExportFileFormat::MP3;
			if (!lmms::ProjectRenderer::fileEncodeDevices[static_cast<std::size_t>(type)].isAvailable())
			{
				continue;
			}
			const auto path = directory.filePath("重复." + format);
			QFile original(path);
			QVERIFY(original.open(QIODevice::WriteOnly));
			original.write("replace only on success");
			original.close();
			const auto started = bus.execute("export.audio",
				{{"path", path}, {"format", format}, {"overwrite", true}, {"loopCount", 2}, {"asLoop", true},
					{"range", QJsonObject{{"start", 0}, {"end", 96}}},
					{"quality", QJsonObject{{"sampleRate", 44100}, {"bitDepth", 24}}}});
			QVERIFY2(started.ok, qPrintable(started.errorMessage));
			const auto task = started.data.value("task").toString();
			QTRY_VERIFY_WITH_TIMEOUT(
				bus.execute("export.status", {{"task", task}}).data.value("status") != "running", 15000);
			const auto status = bus.execute("export.status", {{"task", task}}).data;
			QCOMPARE(status.value("status").toString(), QString("completed"));
			QVERIFY(status.value("bytes").toDouble() > 100);
			const auto audio = lmms::SampleBuffer::fromFile(path);
			QVERIFY2(audio && !audio->empty(), qPrintable(format));
			QVERIFY2(
				std::abs(static_cast<double>(audio->size()) / audio->sampleRate() - 2.0) < 0.15, qPrintable(format));
			float peak = 0;
			for (const auto& frame : *audio)
			{
				peak = std::max(peak, std::abs(frame[0]));
			}
			QVERIFY2(peak > 0.1f, qPrintable(format));
		}
		QCOMPARE(QDir(directory.path()).entryList({".lmms-render-*"}, QDir::Files | QDir::Hidden).size(), 0);
		QVERIFY(
			!bus.execute("export.audio",
					{{"path", directory.filePath("bad.ogg")}, {"format", "ogg"},
						{"quality", QJsonObject{{"bitrate", 512}}}})
				.ok);
		QVERIFY(!lmms::agent::hasActiveAudioExport());
	}

	void cancelsAudioExportsWithoutOverwritingFiles()
	{
		auto& bus = lmms::agent::CommandBus::instance();
		auto* song = lmms::Engine::getSong();
		QTemporaryDir directory;
		QVERIFY(directory.isValid());
		const auto path = directory.filePath("preserved.wav");
		QFile file(path);
		QVERIFY(file.open(QIODevice::WriteOnly));
		file.write("original");
		file.close();
		QVERIFY(bus.execute("song.setTempo", {{"bpm", 20}}).ok);
		const auto history = bus.execute("history.status").data;
		const auto started = bus.execute("export.audio",
			{{"path", path}, {"overwrite", true}, {"range", QJsonObject{{"start", 0}, {"end", lmms::MaxSongLength}}}});
		QVERIFY2(started.ok, qPrintable(started.errorMessage));
		const auto task = started.data.value("task").toString();
		QVERIFY(bus.execute("export.cancel", {{"task", task}, {"dryRun", true}}).ok);
		QVERIFY(lmms::agent::hasActiveAudioExport());
		const auto cancelled = bus.execute("export.cancel", {{"task", task}});
		QCOMPARE(cancelled.data.value("status").toString(), QString("cancelled"));
		QVERIFY(!song->isExporting());
		QVERIFY(!lmms::agent::hasActiveAudioExport());
		QVERIFY(song->isStopped());
		QCOMPARE(bus.execute("history.status").data, history);
		QVERIFY(file.open(QIODevice::ReadOnly));
		QCOMPARE(file.readAll(), QByteArray("original"));
		file.close();
		QCOMPARE(bus.execute("export.cancel", {{"task", task}}).data.value("status").toString(), QString("cancelled"));
		QCOMPARE(QDir(directory.path()).entryList({".lmms-render-*"}, QDir::Files | QDir::Hidden).size(), 0);
		QVERIFY(
			!bus.execute(
					"export.audio", {{"path", path}, {"overwrite", true}, {"quality", QJsonObject{{"sampleRate", 1}}}})
				.ok);
		QVERIFY(
			!bus.execute("export.audio",
					{{"path", path}, {"overwrite", true}, {"range", QJsonObject{{"start", 4}, {"end", 4}}}})
				.ok);
		QVERIFY(bus.beginBatch("file export is not atomic with project edits"));
		QVERIFY(bus.execute("track.create").ok);
		QCOMPARE(bus.execute("export.audio", {{"path", directory.filePath("batch.wav")}}).errorCode,
			QString("external_write_in_batch"));
		QCOMPARE(song->tracks().size(), std::size_t(0));
	}

	void readsAndSetsConfigurationWithoutProjectHistory()
	{
		auto& bus = lmms::agent::CommandBus::instance();
		auto* config = lmms::ConfigManager::inst();
		config->setValue("agentTest", "setting", "original");
		const auto history = bus.execute("history.status").data;
		QCOMPARE(bus.execute("config.get", {{"group", "agentTest"}, {"key", "setting"}}).data.value("value").toString(),
			QString("original"));
		QVERIFY(
			bus.execute(
				   "config.set", {{"group", "agentTest"}, {"key", "setting"}, {"value", "preview"}, {"dryRun", true}})
				.ok);
		QCOMPARE(config->value("agentTest", "setting"), QString("original"));
		QVERIFY(bus.execute("config.set", {{"group", "agentTest"}, {"key", "setting"}, {"value", "changed"}}).ok);
		QCOMPARE(config->value("agentTest", "setting"), QString("changed"));
		QCOMPARE(bus.execute("history.status").data, history);
		QVERIFY(!bus.execute("config.set", {{"group", "agentTest"}, {"key", "setting"}, {"value", 4}}).ok);
		QVERIFY(!bus.execute("config.set", {{"group", "agentMcp"}, {"key", "port"}, {"value", "9000"}}).ok);
		QVERIFY(!bus.execute("config.get", {{"group", "invalid/group"}, {"key", "setting"}}).ok);
		QVERIFY(bus.beginBatch("config cannot join project undo"));
		QVERIFY(bus.execute("track.create").ok);
		QCOMPARE(bus.execute("config.set", {{"group", "agentTest"}, {"key", "setting"}, {"value", "batch"}}).errorCode,
			QString("external_write_in_batch"));
		QCOMPARE(config->value("agentTest", "setting"), QString("changed"));
		QCOMPARE(lmms::Engine::getSong()->tracks().size(), std::size_t(0));
		config->deleteValue("agentTest", "setting");
	}

	void reflectsNativeParametersPresetsAndPeakControllers()
	{
		const auto plugins = qEnvironmentVariable("LMMS_AGENT_PLUGIN_TEST_PATH");
		if (plugins.isEmpty())
		{
			QSKIP("Set LMMS_AGENT_PLUGIN_TEST_PATH to run native DLL integration.");
		}
		QDir::setSearchPaths("plugins", {plugins});
		lmms::PluginFactory::instance()->discoverPlugins();
		auto& bus = lmms::agent::CommandBus::instance();
		const auto parameter = [](const QJsonObject& data, const QString& name) {
			for (const auto& entry : data.value("parameters").toArray())
			{
				if (entry.toObject().value("name") == name)
				{
					return entry.toObject();
				}
			}
			return QJsonObject{};
		};
		QVERIFY(bus.execute("track.create").ok);
		QVERIFY(bus.execute("instrument.load", {{"track", 0}, {"plugin", "tripleoscillator"}}).ok);
		const auto oscillator = parameter(bus.execute("instrument.getParams", {{"track", 0}}).data, "vol0");
		QCOMPARE(oscillator.value("level").toString(), QString("L2"));
		QCOMPARE(oscillator.value("type").toString(), QString("number"));
		const auto volumePath = oscillator.value("path").toString();
		QVERIFY(bus.execute("model.getValue", {{"path", volumePath}}).ok);
		QVERIFY(bus.execute("instrument.setParam", {{"track", 0}, {"name", "vol0"}, {"value", 42}}).ok);
		QVERIFY(
			bus.execute("instrument.setParam", {{"track", 0}, {"name", "vol0"}, {"value", 80}, {"dryRun", true}}).ok);
		QCOMPARE(bus.execute("model.getValue", {{"path", volumePath}}).data.value("value").toDouble(), 42.0);
		QVERIFY(
			!bus.execute("instrument.setParam",
					{{"track", 0}, {"name", "vol0"}, {"value", oscillator.value("max").toDouble() + 1}})
				.ok);
		QVERIFY(!bus.execute("instrument.setParam", {{"track", 0}, {"name", "wavetype0"}, {"value", 1.5}}).ok);
		QVERIFY(!bus.execute("instrument.setParam", {{"track", 0}, {"name", "vol0"}, {"value", "42"}}).ok);
		QTemporaryDir directory;
		QVERIFY(directory.isValid());
		const auto preset = directory.filePath("合成器.xpf");
		QVERIFY(bus.execute("instrument.savePreset", {{"track", 0}, {"path", preset}}).ok);
		QCOMPARE(
			bus.execute("instrument.savePreset", {{"track", 0}, {"path", preset}}).errorCode, QString("file_exists"));
		QVERIFY(bus.execute("instrument.savePreset", {{"track", 0}, {"path", preset}, {"overwrite", true}}).ok);
		QVERIFY(bus.execute("instrument.setParam", {{"track", 0}, {"name", "vol0"}, {"value", 80}}).ok);
		QVERIFY(bus.execute("instrument.loadPreset", {{"track", 0}, {"path", preset}, {"dryRun", true}}).ok);
		QCOMPARE(bus.execute("model.getValue", {{"path", volumePath}}).data.value("value").toDouble(), 80.0);
		QVERIFY(bus.execute("instrument.loadPreset", {{"track", 0}, {"path", preset}}).ok);
		QCOMPARE(bus.execute("model.getValue", {{"path", volumePath}}).data.value("value").toDouble(), 42.0);
		QVERIFY(bus.execute("history.undo").ok);
		QCOMPARE(bus.execute("model.getValue", {{"path", volumePath}}).data.value("value").toDouble(), 80.0);
		QVERIFY(bus.execute("history.redo").ok);
		QVERIFY(
			bus.execute("instrument.load",
				   {{"track", 0}, {"plugin", "audiofileprocessor"}, {"path", writeTestWave(directory, true)}})
				.ok);
		QVERIFY(bus.execute("instrument.setParam", {{"track", 0}, {"name", "reversed"}, {"value", true}}).ok);
		QVERIFY(
			parameter(bus.execute("instrument.getParams", {{"track", 0}}).data, "reversed").value("value").toBool());
		QVERIFY(bus.execute("effect.add", {{"owner", "track:0"}, {"plugin", "amplifier"}}).ok);
		const auto gain
			= parameter(bus.execute("effect.getParams", {{"owner", "track:0"}, {"slot", 0}}).data, "volume");
		QCOMPARE(gain.value("level").toString(), QString("L2"));
		QVERIFY(
			bus.execute("effect.setParam", {{"owner", "track:0"}, {"slot", 0}, {"name", "volume"}, {"value", 125}}).ok);
		QCOMPARE(bus.execute("model.getValue", {{"path", gain.value("path")}}).data.value("value").toDouble(), 125.0);
		QVERIFY(
			!bus.execute("effect.setParam",
					{{"owner", "track:0"}, {"slot", 0}, {"name", "volume"},
						{"value", gain.value("max").toDouble() + 1}})
				.ok);
		QVERIFY(bus.execute("controller.add", {{"type", "Peak"}, {"owner", "track:0"}, {"dryRun", true}}).ok);
		QCOMPARE(bus.execute("controller.list").data.value("controllers").toArray().size(), 0);
		const auto peak = bus.execute("controller.add", {{"type", "Peak"}, {"owner", "track:0"}});
		QVERIFY2(peak.ok, qPrintable(peak.errorMessage));
		QCOMPARE(peak.data.value("type").toString(), QString("Peak"));
		QVERIFY(bus.execute("controller.setParam", {{"controller", 0}, {"name", "base"}, {"value", 0.3}}).ok);
		QVERIFY(bus.execute("controller.connect", {{"controller", 0}, {"target", gain.value("path")}}).ok);
		QVERIFY(bus.execute("controller.remove", {{"controller", 0}}).ok);
		QCOMPARE(bus.execute("controller.list").data.value("controllers").toArray().size(), 0);
		QVERIFY(bus.execute("history.undo").ok);
		QCOMPARE(bus.execute("controller.list").data.value("controllers").toArray().size(), 1);
		QVERIFY(bus.execute("history.redo").ok);
	}

	void exportsNativeMidiAndRoundTripsNotes()
	{
		const auto pluginDirectory = qEnvironmentVariable("LMMS_AGENT_PLUGIN_TEST_PATH");
		if (pluginDirectory.isEmpty())
		{
			QSKIP("Set LMMS_AGENT_PLUGIN_TEST_PATH to run native DLL integration.");
		}
		QDir::setSearchPaths("plugins", {pluginDirectory});
		lmms::PluginFactory::instance()->discoverPlugins();
		auto& bus = lmms::agent::CommandBus::instance();
		auto* song = lmms::Engine::getSong();
		QVERIFY(bus.execute("track.create").ok);
		QVERIFY(bus.execute("clip.create", {{"track", 0}, {"position", 0}}).ok);
		QVERIFY(
			bus.execute("midi.addNotes",
				   {{"track", 0}, {"clip", 0},
					   {"notes",
						   QJsonArray{QJsonObject{{"position", 0}, {"length", 24}, {"key", 60}},
							   QJsonObject{{"position", 48}, {"length", 24}, {"key", 62}}}}})
				.ok);
		QVERIFY(bus.execute("pattern.create", {{"name", "Pattern"}}).ok);
		QVERIFY(bus.execute("pattern.setLength", {{"pattern", 0}, {"bars", 2}}).ok);
		QVERIFY(bus.execute("track.create", {{"parent", "pattern"}}).ok);
		QVERIFY(bus.execute("track.create", {{"parent", "pattern"}, {"type", "sample"}}).ok);
		QVERIFY(bus.execute("track.create", {{"parent", "pattern"}, {"type", "automation"}}).ok);
		QVERIFY(
			bus.execute("midi.addNotes",
				   {{"parent", "pattern"}, {"track", 0}, {"clip", 0},
					   {"notes", QJsonArray{QJsonObject{{"position", 0}, {"length", 24}, {"key", 36}}}}})
				.ok);
		QVERIFY(bus.execute("pattern.placeInSong", {{"pattern", 0}, {"position", 0}, {"length", 768}}).ok);
		QTemporaryDir directory;
		QVERIFY(directory.isValid());
		const auto path = directory.filePath("导出.mid");
		const auto history = bus.execute("history.status").data;
		QVERIFY(bus.execute("export.midi", {{"path", path}, {"dryRun", true}}).ok);
		QVERIFY(!QFileInfo::exists(path));
		const auto exported = bus.execute("export.midi", {{"path", path}});
		QVERIFY2(exported.ok, qPrintable(exported.errorMessage));
		QCOMPARE(bus.execute("history.status").data, history);
		QFile file(path);
		QVERIFY(file.open(QIODevice::ReadOnly));
		const auto bytes = file.readAll();
		QVERIFY(bytes.startsWith("MThd"));
		QCOMPARE(static_cast<unsigned char>(bytes[11]), static_cast<unsigned char>(2));
		file.close();
		QCOMPARE(bus.execute("export.midi", {{"path", path}}).errorCode, QString("file_exists"));
		QVERIFY(bus.execute("export.midi", {{"path", path}, {"overwrite", true}}).ok);
		QVERIFY(bus.execute("song.clearProject").ok);
		const auto imported = bus.execute("import.midi", {{"path", path}});
		QVERIFY2(imported.ok, qPrintable(imported.errorMessage));
		QJsonArray noteSets;
		for (std::size_t index = 0; index < song->tracks().size(); ++index)
		{
			if (song->tracks()[index]->type() != lmms::Track::Type::Instrument)
			{
				continue;
			}
			for (std::size_t clip = 0; clip < song->tracks()[index]->getClips().size(); ++clip)
			{
				const auto notes = bus.execute(
					"midi.getNotes", {{"track", static_cast<int>(index)}, {"clip", static_cast<int>(clip)}});
				for (const auto& note : notes.data.value("notes").toArray())
				{
					noteSets.append(note);
				}
			}
		}
		QCOMPARE(noteSets.size(), 4); // Two song notes plus two repeats of a two-bar pattern.
		QVERIFY(bus.beginBatch("external export rejected"));
		QVERIFY(bus.execute("track.create").ok);
		QCOMPARE(bus.execute("export.midi", {{"path", directory.filePath("batch.mid")}}).errorCode,
			QString("external_write_in_batch"));
		QVERIFY(!QFileInfo::exists(directory.filePath("batch.mid")));
	}

	void importsNativeMidiAndHydrogenWithoutDialogs()
	{
		const auto pluginDirectory = qEnvironmentVariable("LMMS_AGENT_PLUGIN_TEST_PATH");
		if (pluginDirectory.isEmpty())
		{
			QSKIP("Set LMMS_AGENT_PLUGIN_TEST_PATH to run native DLL integration.");
		}
		QDir::setSearchPaths("plugins", {pluginDirectory});
		lmms::PluginFactory::instance()->discoverPlugins();
		auto& bus = lmms::agent::CommandBus::instance();
		auto* song = lmms::Engine::getSong();
		QTemporaryDir directory;
		QVERIFY(directory.isValid());
		const auto midiPath = directory.filePath("one-note.mid");
		QFile midi(midiPath);
		QVERIFY(midi.open(QIODevice::WriteOnly));
		const auto bytes = QByteArray::fromHex("4d546864000000060000000100604d54726b0000000c00903c6460803c0000ff2f00");
		QCOMPARE(midi.write(bytes), qint64(bytes.size()));
		midi.close();
		const auto preview = bus.execute("import.midi", {{"path", midiPath}, {"dryRun", true}});
		QVERIFY2(preview.ok, qPrintable(preview.errorMessage));
		QCOMPARE(song->tracks().size(), std::size_t(0));
		QVERIFY(bus.execute("track.create", {{"name", "Target"}}).ok);
		const auto imported = bus.execute("import.midi", {{"path", midiPath}, {"track", 0}});
		QVERIFY2(imported.ok, qPrintable(imported.errorMessage));
		QCOMPARE(song->tracks()[0]->name(), QString("Target"));
		QCOMPARE(song->tracks()[0]->getClips().size(), std::size_t(1));
		const auto notes = bus.execute("midi.getNotes", {{"track", 0}, {"clip", 0}});
		QCOMPARE(notes.data.value("total").toInt(), 1);
		QCOMPARE(notes.data.value("notes").toArray()[0].toObject().value("key").toInt(), 60);
		QVERIFY(bus.execute("history.undo").ok);
		QCOMPARE(song->tracks().size(), std::size_t(1));
		QCOMPARE(song->tracks()[0]->getClips().size(), std::size_t(0));
		QVERIFY(bus.execute("history.redo").ok);
		QCOMPARE(song->tracks()[0]->getClips().size(), std::size_t(1));
		QFile invalidMidi(directory.filePath("bad.mid"));
		QVERIFY(invalidMidi.open(QIODevice::WriteOnly));
		invalidMidi.write("MThd");
		invalidMidi.close();
		const auto trackCount = song->tracks().size();
		QVERIFY(!bus.execute("import.midi", {{"path", invalidMidi.fileName()}}).ok);
		QCOMPARE(song->tracks().size(), trackCount);
		QVERIFY(bus.execute("song.clearProject").ok);
		const auto sample = writeTestWave(directory);
		const auto hydrogenPath = directory.filePath("drums.h2song");
		const auto xml = QString(
			"<hydrogen><song><version>1.2.0</version><name>Test</name>"
			"<instrumentList><instrument><id>0</id><name>Kick</name><layer><filename>%1</filename>"
			"</layer></instrument></instrumentList><patternList><pattern><name>Empty</name><size>192</size>"
			"<noteList/></pattern><pattern><name>Beat</name><size>192</size><noteList><note><position>0</position>"
			"<velocity>1</velocity><instrument>0</instrument><key>C4</key></note></noteList></pattern>"
			"</patternList><patternSequence><group><patternID>Beat</patternID></group></patternSequence>"
			"</song></hydrogen>")
							 .arg(sample.toHtmlEscaped());
		QFile hydrogen(hydrogenPath);
		QVERIFY(hydrogen.open(QIODevice::WriteOnly));
		hydrogen.write(xml.toUtf8());
		hydrogen.close();
		const auto hydrogenPreview = bus.execute("import.hydrogen", {{"path", hydrogenPath}, {"dryRun", true}});
		QVERIFY2(hydrogenPreview.ok, qPrintable(hydrogenPreview.errorMessage));
		QCOMPARE(song->tracks().size(), std::size_t(0));
		const auto hydrogenImport = bus.execute("import.hydrogen", {{"path", hydrogenPath}});
		QVERIFY2(hydrogenImport.ok, qPrintable(hydrogenImport.errorMessage));
		QCOMPARE(lmms::Engine::patternStore()->numOfPatterns(), 2);
		QCOMPARE(bus.execute("midi.getNotes", {{"parent", "pattern"}, {"track", 0}, {"clip", 1}})
					 .data.value("total")
					 .toInt(),
			1);
		QVERIFY(bus.execute("history.undo").ok);
		QCOMPARE(song->tracks().size(), std::size_t(0));
		QVERIFY(hydrogen.open(QIODevice::WriteOnly | QIODevice::Truncate));
		hydrogen.write(QString(xml).replace("<instrument>0</instrument>", "<instrument>999</instrument>").toUtf8());
		hydrogen.close();
		QVERIFY(!bus.execute("import.hydrogen", {{"path", hydrogenPath}}).ok);
		QCOMPARE(song->tracks().size(), std::size_t(0));
		QCOMPARE(lmms::Engine::patternStore()->tracks().size(), std::size_t(0));
	}

	void importsSamplesAtomically()
	{
		auto& bus = lmms::agent::CommandBus::instance();
		auto* song = lmms::Engine::getSong();
		QTemporaryDir directory;
		QVERIFY(directory.isValid());
		const auto path = writeTestWave(directory);
		QVERIFY(!path.isEmpty());
		const auto preview = bus.execute("import.sampleToTrack", {{"path", path}, {"position", 96}, {"dryRun", true}});
		QVERIFY2(preview.ok, qPrintable(preview.errorMessage));
		QCOMPARE(song->tracks().size(), std::size_t(0));
		const auto imported
			= bus.execute("import.sampleToTrack", {{"path", path}, {"position", 96}, {"name", "Sample"}});
		QVERIFY2(imported.ok, qPrintable(imported.errorMessage));
		QCOMPARE(song->tracks().size(), std::size_t(1));
		QCOMPARE(song->tracks().at(0)->name(), QString("Sample"));
		QCOMPARE(song->tracks().at(0)->getClips().at(0)->startPosition().getTicks(), 96);
		QVERIFY(bus.execute("history.undo").ok);
		QCOMPARE(song->tracks().size(), std::size_t(0));
		QVERIFY(bus.execute("history.redo").ok);
		QCOMPARE(song->tracks().size(), std::size_t(1));
		QVERIFY(bus.execute("import.sampleToTrack", {{"path", path}, {"track", 0}, {"position", 192}}).ok);
		QCOMPARE(song->tracks().at(0)->getClips().size(), std::size_t(2));
		QVERIFY(!bus.execute("import.sampleToTrack", {{"path", directory.filePath("missing.wav")}}).ok);
		QFile bad(directory.filePath("invalid.wav"));
		QVERIFY(bad.open(QIODevice::WriteOnly));
		bad.write("not audio");
		bad.close();
		QVERIFY(!bus.execute("import.sampleToTrack", {{"path", bad.fileName()}}).ok);
		QCOMPARE(song->tracks().size(), std::size_t(1));
		QCOMPARE(song->tracks().at(0)->getClips().size(), std::size_t(2));
		QVERIFY(bus.execute("track.create", {{"type", "instrument"}}).ok);
		QVERIFY(!bus.execute("import.sampleToTrack", {{"path", path}, {"track", 1}}).ok);
		QCOMPARE(song->tracks().at(1)->getClips().size(), std::size_t(0));
	}

	void persistsSongLoopRangeWithoutGui()
	{
		auto& bus = lmms::agent::CommandBus::instance();
		auto* song = lmms::Engine::getSong();
		const int ticksPerBar = song->ticksPerBar();
		auto& timeline = song->getTimeline(lmms::Song::PlayMode::Song);
		timeline.setLoopPoints(lmms::TimePos(0), lmms::TimePos(0));
		timeline.setLoopEnabled(false);

		const auto dryRun
			= bus.execute("transport.setLoopRange", QJsonObject{{"startBar", 2}, {"endBar", 4}, {"dryRun", true}});
		QVERIFY(dryRun.ok);
		QCOMPARE(dryRun.data.value("start").toInt(), 2 * ticksPerBar);
		QVERIFY(!timeline.loopEnabled());

		const auto range = bus.execute("transport.setLoopRange", QJsonObject{{"startBar", 1}, {"endBar", 3}});
		QVERIFY(range.ok);
		QCOMPARE(range.data.value("start").toInt(), ticksPerBar);
		QCOMPARE(range.data.value("end").toInt(), 3 * ticksPerBar);
		QVERIFY(range.data.value("enabled").toBool());

		QCOMPARE(timeline.loopBegin().getTicks(), ticksPerBar);
		QCOMPARE(timeline.loopEnd().getTicks(), 3 * ticksPerBar);
		QVERIFY(timeline.loopEnabled());

		QVERIFY(bus.execute("history.undo").ok);
		QVERIFY(!timeline.loopEnabled());
		QVERIFY(bus.execute("history.redo").ok);
		QCOMPARE(timeline.loopBegin().getTicks(), ticksPerBar);
		QCOMPARE(timeline.loopEnd().getTicks(), 3 * ticksPerBar);
		QVERIFY(timeline.loopEnabled());

		QTemporaryDir temporaryDirectory;
		QVERIFY(temporaryDirectory.isValid());
		const QString projectPath = temporaryDirectory.filePath("loop-range.mmp");
		QVERIFY(song->saveProjectFile(projectPath, false));

		timeline.setLoopPoints(lmms::TimePos(0), lmms::TimePos(0));
		timeline.setLoopEnabled(false);
		song->loadProject(projectPath);

		QCOMPARE(timeline.loopBegin().getTicks(), ticksPerBar);
		QCOMPARE(timeline.loopEnd().getTicks(), 3 * ticksPerBar);
		QVERIFY(timeline.loopEnabled());
	}

	void supportsMidiSampleAndPreviewCommands()
	{
		auto& bus = lmms::agent::CommandBus::instance();
		auto* song = lmms::Engine::getSong();

		QVERIFY(bus.execute("track.create", QJsonObject{{"type", "instrument"}}).ok);
		QVERIFY(bus.execute("clip.create", QJsonObject{{"track", 0}, {"type", "midi"}, {"length", song->ticksPerBar()}})
				.ok);
		QVERIFY(
			bus.execute("midi.addNotes",
				   QJsonObject{{"track", 0}, {"clip", 0},
					   {"notes",
						   QJsonArray{QJsonObject{{"position", 24}, {"length", 48}, {"key", 60}, {"volume", 100}}}}})
				.ok);

		const auto setSteps = bus.execute("midi.setSteps", QJsonObject{{"track", 0}, {"clip", 0}, {"steps", 32}});
		QVERIFY(setSteps.ok);
		QCOMPARE(setSteps.data.value("steps").toInt(), 32);

		const auto setBeat = bus.execute("midi.setClipType", QJsonObject{{"track", 0}, {"clip", 0}, {"type", "beat"}});
		QVERIFY(setBeat.ok);
		QCOMPARE(setBeat.data.value("type").toString(), QString("beat"));

		auto* midiClip = dynamic_cast<lmms::MidiClip*>(song->tracks().at(0)->getClips().at(0));
		QVERIFY(midiClip != nullptr);
		QCOMPARE(midiClip->type(), lmms::MidiClip::Type::BeatClip);
		QCOMPARE(midiClip->steps(), 32);

		QVERIFY(bus.execute("midi.setClipType", QJsonObject{{"track", 0}, {"clip", 0}, {"type", "melody"}}).ok);
		const auto humanize = bus.execute("midi.humanize",
			QJsonObject{{"track", 0}, {"clip", 0}, {"timing", 6}, {"velocity", 8}, {"detune", 0.5}, {"seed", 17}});
		QVERIFY(humanize.ok);
		QCOMPARE(humanize.data.value("seed").toInt(), 17);
		QVERIFY(midiClip->notes().front()->hasDetuningInfo());

		const auto preview
			= bus.execute("transport.previewClip", QJsonObject{{"track", 0}, {"clip", 0}, {"loop", false}});
		QVERIFY(preview.ok);
		QCOMPARE(preview.data.value("mode").toString(), QString("midiClip"));
		QVERIFY(!preview.data.value("loop").toBool());
		song->stop();

		QVERIFY(bus.execute("track.create", QJsonObject{{"type", "sample"}}).ok);
		const auto sampleClip = bus.execute(
			"clip.create", QJsonObject{{"track", 1}, {"type", "sample"}, {"length", song->ticksPerBar()}});
		QVERIFY(sampleClip.ok);
		QCOMPARE(sampleClip.data.value("type").toString(), QString("sample"));

		QTemporaryDir temporaryDirectory;
		QVERIFY(temporaryDirectory.isValid());
		const QString samplePath = writeTestWave(temporaryDirectory);
		QVERIFY(!samplePath.isEmpty());
		const auto loaded = bus.execute("sample.setFile", QJsonObject{{"track", 1}, {"clip", 0}, {"path", samplePath}});
		QVERIFY(loaded.ok);
		QCOMPARE(loaded.data.value("sampleRate").toInt(), 44100);
		QVERIFY(loaded.data.value("frames").toDouble() > 0.0);

		const auto reversed
			= bus.execute("sample.setReversed", QJsonObject{{"track", 1}, {"clip", 0}, {"value", true}});
		QVERIFY(reversed.ok);
		QVERIFY(reversed.data.value("reversed").toBool());
		const auto offset = bus.execute("sample.setOffset", QJsonObject{{"track", 1}, {"clip", 0}, {"value", 1}});
		QVERIFY(offset.ok);
		QCOMPARE(offset.data.value("offsetTicks").toInt(), 1);

		const auto info = bus.execute("sample.getInfo", QJsonObject{{"track", 1}, {"clip", 0}});
		QVERIFY(info.ok);
		QVERIFY(info.data.value("reversed").toBool());
		QCOMPARE(info.data.value("offsetTicks").toInt(), 1);
	}

	void supportsSongLifecycleCommands()
	{
		auto& bus = lmms::agent::CommandBus::instance();
		auto* song = lmms::Engine::getSong();

		QVERIFY(bus.execute("track.create", QJsonObject{{"type", "instrument"}}).ok);
		QVERIFY(bus.execute("song.setTempo", QJsonObject{{"bpm", 133}}).ok);

		QTemporaryDir temporaryDirectory;
		QVERIFY(temporaryDirectory.isValid());
		const QString projectPath = temporaryDirectory.filePath("lifecycle.mmp");
		const auto save = bus.execute("song.save", QJsonObject{{"path", projectPath}});
		QVERIFY(save.ok);
		QVERIFY(QFile::exists(projectPath));
		QCOMPARE(song->projectFileName(), projectPath);

		QVERIFY(bus.execute("song.setTempo", QJsonObject{{"bpm", 144}}).ok);
		const auto dryRunLoad = bus.execute("song.load", QJsonObject{{"path", projectPath}, {"dryRun", true}});
		QVERIFY(dryRunLoad.ok);
		QCOMPARE(song->getTempo(), 144);

		const auto load = bus.execute("song.load", QJsonObject{{"path", projectPath}});
		QVERIFY(load.ok);
		QCOMPARE(song->getTempo(), 133);
		QCOMPARE(static_cast<int>(song->tracks().size()), 1);
		QVERIFY(bus.execute("history.undo").ok);
		QCOMPARE(song->getTempo(), 144);
		QVERIFY(bus.execute("history.redo").ok);
		QCOMPARE(song->getTempo(), 133);

		const auto clear = bus.execute("song.clearProject");
		QVERIFY(clear.ok);
		QCOMPARE(clear.data.value("trackCount").toInt(), 0);
		QVERIFY(bus.execute("history.undo").ok);
		QCOMPARE(static_cast<int>(song->tracks().size()), 1);
	}

	void persistsScalesAndSnapsSelectedNotes()
	{
		auto& bus = lmms::agent::CommandBus::instance();
		QVERIFY(bus.execute("scale.set", {{"root", 0}, {"type", "major"}}).ok);
		QVERIFY(bus.execute("scale.set", {{"root", 9}, {"type", "minor"}, {"dryRun", true}}).ok);
		QCOMPARE(bus.execute("scale.get").data.value("root").toInt(), 0);
		QCOMPARE(bus.execute("scale.get").data.value("type").toString(), QString("major"));
		QVERIFY(bus.execute("track.create", {{"type", "instrument"}}).ok);
		QVERIFY(bus.execute("clip.create", {{"track", 0}, {"type", "midi"}}).ok);
		QVERIFY(
			bus.execute("midi.addNotes",
				   {{"track", 0}, {"clip", 0},
					   {"notes",
						   QJsonArray{QJsonObject{{"position", 0}, {"length", 12}, {"key", 61}},
							   QJsonObject{{"position", 24}, {"length", 12}, {"key", 66}}}}})
				.ok);
		const QJsonObject args{{"track", 0}, {"clip", 0}, {"range", QJsonObject{{"start", 0}, {"end", 24}}}};
		QCOMPARE(bus.execute("scale.snapNotes", args).data.value("changed").toInt(), 1);
		auto notes = bus.execute("midi.getNotes", {{"track", 0}, {"clip", 0}}).data.value("notes").toArray();
		QCOMPARE(notes[0].toObject().value("key").toInt(), 60);
		QCOMPARE(notes[1].toObject().value("key").toInt(), 66);
		QVERIFY(bus.execute("history.undo").ok);
		notes = bus.execute("midi.getNotes", {{"track", 0}, {"clip", 0}}).data.value("notes").toArray();
		QCOMPARE(notes[0].toObject().value("key").toInt(), 61);
		QVERIFY(bus.execute("scale.set", {{"root", 9}, {"type", "minor"}}).ok);
		QTemporaryDir directory;
		QVERIFY(directory.isValid());
		const auto path = directory.filePath("scale.mmp");
		QVERIFY(bus.execute("song.save", {{"path", path}}).ok);
		QVERIFY(bus.execute("scale.set", {{"root", 2}, {"type", "chromatic"}}).ok);
		QVERIFY(bus.execute("song.load", {{"path", path}}).ok);
		QCOMPARE(bus.execute("scale.get").data.value("root").toInt(), 9);
		QCOMPARE(bus.execute("scale.get").data.value("type").toString(), QString("minor"));
		QVERIFY(!bus.execute("scale.set", {{"root", 12}, {"type", "major"}}).ok);
		QVERIFY(!bus.execute("scale.set", {{"root", 0}, {"type", "custom"}, {"semitones", QJsonArray{2, 5}}}).ok);
	}

	void configuresInstrumentFunctionsAndMidiPorts()
	{
		auto& bus = lmms::agent::CommandBus::instance();
		auto* song = lmms::Engine::getSong();
		QVERIFY(bus.execute("track.create", {{"parent", "pattern"}, {"type", "instrument"}}).ok);
		const auto trackPath = QString("pattern/track:0");
		QVERIFY(
			bus.execute("instrument.setArpeggio",
				   {{"track", trackPath}, {"enabled", true}, {"params", QJsonObject{{"range", 2}, {"direction", 1}}}})
				.ok);
		QVERIFY(static_cast<lmms::InstrumentTrack*>(lmms::Engine::patternStore()->tracks()[0])->isArpeggioEnabled());
		QVERIFY(
			!bus.execute("instrument.setArpeggio",
					{{"track", trackPath}, {"enabled", false}, {"params", QJsonObject{{"range", 100}}}})
				.ok);
		QVERIFY(static_cast<lmms::InstrumentTrack*>(lmms::Engine::patternStore()->tracks()[0])->isArpeggioEnabled());
		QVERIFY(
			bus.execute("instrument.setNoteStacking",
				   {{"track", trackPath}, {"enabled", true}, {"params", QJsonObject{{"range", 2}}}})
				.ok);
		QVERIFY(bus.execute("instrument.setMidiIn", {{"track", trackPath}, {"channel", 3}, {"enabled", false}}).ok);
		QVERIFY(bus.execute("instrument.setMidiOut", {{"track", trackPath}, {"channel", 4}, {"enabled", true}}).ok);
		auto* track = static_cast<lmms::InstrumentTrack*>(lmms::Engine::patternStore()->tracks()[0]);
		QCOMPARE(track->midiPort()->inputChannel(), 3);
		QCOMPARE(track->midiPort()->outputChannel(), 4);
		QVERIFY(bus.execute("instrument.setMidiOut", {{"track", trackPath}, {"channel", 8}, {"dryRun", true}}).ok);
		track = static_cast<lmms::InstrumentTrack*>(lmms::Engine::patternStore()->tracks()[0]);
		QCOMPARE(track->midiPort()->outputChannel(), 4);
		QVERIFY(!bus.execute("instrument.setMidiIn", {{"track", trackPath}, {"channel", 17}}).ok);
		QVERIFY(!bus.execute("instrument.setMidiIn", {{"track", trackPath}, {"port", "missing-test-port"}}).ok);
		const int depth = bus.execute("history.status").data.value("undoDepth").toInt();
		QVERIFY(
			bus.execute("instrument.setPiano",
				   {{"track", trackPath}, {"enabled", true}, {"params", QJsonObject{{"key", 60}}}, {"dryRun", true}})
				.ok);
		QCOMPARE(bus.execute("history.status").data.value("undoDepth").toInt(), depth);
		QVERIFY(!static_cast<lmms::InstrumentTrack*>(lmms::Engine::patternStore()->tracks()[0])
				->pianoModel()
				->isKeyPressed(60));
		QVERIFY(bus.execute("instrument.setPiano", {{"track", trackPath}, {"enabled", false}}).ok);
		QTemporaryDir directory;
		QVERIFY(directory.isValid());
		const QString presetPath = directory.filePath("instrument.xpf");
		QVERIFY(
			bus.execute("instrument.savePreset", {{"track", trackPath}, {"path", presetPath}, {"dryRun", true}}).ok);
		QVERIFY(!QFile::exists(presetPath));
		QVERIFY(bus.execute("instrument.savePreset", {{"track", trackPath}, {"path", presetPath}}).ok);
		QVERIFY(QFile::exists(presetPath));
		QVERIFY(
			!bus.execute("instrument.loadPreset", {{"track", trackPath}, {"path", directory.filePath("missing.xpf")}})
				.ok);
		QCOMPARE(song->tracks().size(), std::size_t(1));
	}

	void editsControllersAndRestoresConnections()
	{
		auto& bus = lmms::agent::CommandBus::instance();
		auto* song = lmms::Engine::getSong();
		QVERIFY(bus.execute("track.create", {{"type", "instrument"}}).ok);
		QVERIFY(bus.execute("controller.add", {{"type", "LFO"}, {"name", "Pulse"}}).ok);
		QVERIFY(bus.execute("controller.setParam", {{"controller", 0}, {"name", "base"}, {"value", 0.25}}).ok);
		QVERIFY(!bus.execute("controller.setParam", {{"controller", 0}, {"name", "base"}, {"value", 2}}).ok);
		QCOMPARE(
			bus.execute("model.getValue", {{"path", "song/controller:0/base"}}).data.value("value").toDouble(), 0.25);
		QVERIFY(!bus.execute("controller.connect", {{"controller", 0}, {"target", "song/controller:0/base"}}).ok);
		const auto target = QString("song/track:0/instrument/volume");
		QVERIFY(bus.execute("controller.connect", {{"controller", 0}, {"target", target}}).ok);
		QVERIFY(bus.execute("controller.add", {{"type", "MIDI"}}).ok);
		QVERIFY(bus.execute("controller.setParam", {{"controller", 1}, {"name", "channel"}, {"value", 2}}).ok);
		QVERIFY(bus.execute("controller.setParam", {{"controller", 1}, {"name", "cc"}, {"value", 74}}).ok);
		QVERIFY(bus.execute("controller.connect", {{"controller", 1}, {"target", target}}).ok);
		QCOMPARE(static_cast<lmms::InstrumentTrack*>(song->tracks()[0])
					 ->volumeModel()
					 ->controllerConnection()
					 ->getController(),
			song->controllers()[1]);
		QVERIFY(bus.execute("controller.remove", {{"controller", 1}, {"dryRun", true}}).ok);
		QCOMPARE(song->controllers().size(), std::size_t(2));
		QVERIFY(bus.execute("controller.remove", {{"controller", 1}}).ok);
		QVERIFY(!static_cast<lmms::InstrumentTrack*>(song->tracks()[0])->volumeModel()->controllerConnection());
		QVERIFY(bus.execute("history.undo").ok);
		QCOMPARE(song->controllers().size(), std::size_t(2));
		QCOMPARE(static_cast<lmms::InstrumentTrack*>(song->tracks()[0])
					 ->volumeModel()
					 ->controllerConnection()
					 ->getController(),
			song->controllers()[1]);
		QVERIFY(bus.execute("history.redo").ok);
		QCOMPARE(song->controllers().size(), std::size_t(1));
		QCOMPARE(
			bus.execute("controller.list").data.value("controllers").toArray()[0].toObject().value("name").toString(),
			QString("Pulse"));
	}

	void movesPatternTracksWithTheirRowContent()
	{
		auto& bus = lmms::agent::CommandBus::instance();
		QVERIFY(bus.execute("pattern.create", {{"name", "Alpha"}}).ok);
		QVERIFY(bus.execute("track.create", {{"type", "sample"}, {"name", "Separator"}}).ok);
		QVERIFY(bus.execute("pattern.create", {{"name", "Beta"}}).ok);
		QVERIFY(bus.execute("track.create", {{"parent", "pattern"}}).ok);
		for (int pattern = 0; pattern < 2; ++pattern)
		{
			QVERIFY(
				bus.execute("midi.addNotes",
					   {{"parent", "pattern"}, {"track", 0}, {"clip", pattern},
						   {"notes", QJsonArray{QJsonObject{{"position", 0}, {"length", 24}, {"key", 60 + pattern}}}}})
					.ok);
		}
		QVERIFY(bus.execute("track.move", {{"track", 0}, {"newIndex", 2}}).ok);
		QCOMPARE(bus.execute("pattern.get", {{"pattern", 0}}).data.value("name").toString(), QString("Beta"));
		QCOMPARE(bus.execute("pattern.get", {{"pattern", 1}}).data.value("name").toString(), QString("Alpha"));
		const auto first = bus.execute("midi.getNotes", {{"parent", "pattern"}, {"track", 0}, {"clip", 0}});
		QCOMPARE(first.data.value("notes").toArray()[0].toObject().value("key").toInt(), 61);
		QVERIFY(bus.execute("history.undo").ok);
		QCOMPARE(bus.execute("pattern.get", {{"pattern", 0}}).data.value("name").toString(), QString("Alpha"));
		QVERIFY(bus.execute("history.redo").ok);
		QTemporaryDir directory;
		QVERIFY(directory.isValid());
		const auto path = directory.filePath("moved-patterns.mmp");
		QVERIFY(bus.execute("song.save", {{"path", path}}).ok);
		QVERIFY(bus.execute("song.clearProject").ok);
		QVERIFY(bus.execute("song.load", {{"path", path}}).ok);
		QCOMPARE(bus.execute("pattern.get", {{"pattern", 0}}).data.value("name").toString(), QString("Beta"));
		const auto restored = bus.execute("midi.getNotes", {{"parent", "pattern"}, {"track", 0}, {"clip", 0}});
		QCOMPARE(restored.data.value("notes").toArray()[0].toObject().value("key").toInt(), 61);
		QVERIFY(bus.execute("track.move", {{"track", 2}, {"newIndex", 0}}).ok);
		QCOMPARE(bus.execute("pattern.get", {{"pattern", 0}}).data.value("name").toString(), QString("Alpha"));
	}

	void editsAndPersistsPatternsWithoutGui()
	{
		auto& bus = lmms::agent::CommandBus::instance();
		auto* song = lmms::Engine::getSong();
		QVERIFY(bus.execute("pattern.create", {{"name", "Intro"}}).ok);
		QVERIFY(bus.execute("pattern.setLength", {{"pattern", 0}, {"bars", 4}}).ok);
		QCOMPARE(lmms::Engine::patternStore()->lengthOfPattern(0), 4);
		QVERIFY(bus.execute("track.create", {{"parent", "pattern"}, {"type", "instrument"}}).ok);
		QVERIFY(
			bus.execute("midi.addNotes",
				   {{"parent", "pattern"}, {"track", 0}, {"clip", 0},
					   {"notes", QJsonArray{QJsonObject{{"position", 0}, {"length", 24}, {"key", 60}}}}})
				.ok);
		const auto preview = bus.execute("pattern.create", {{"index", 0}, {"name", "Preview"}, {"dryRun", true}});
		QVERIFY(preview.ok);
		QCOMPARE(lmms::Engine::patternStore()->numOfPatterns(), 1);
		QCOMPARE(bus.execute("pattern.get", {{"pattern", 0}}).data.value("bars").toInt(), 4);
		QVERIFY(bus.execute("pattern.create", {{"index", 0}, {"name", "Verse"}}).ok);
		QCOMPARE(bus.execute("pattern.get", {{"pattern", 1}}).data.value("name").toString(), QString("Intro"));
		QCOMPARE(bus.execute("midi.getNotes", {{"parent", "pattern"}, {"track", 0}, {"clip", 1}})
					 .data.value("total")
					 .toInt(),
			1);
		QCOMPARE(bus.execute("midi.getNotes", {{"parent", "pattern"}, {"track", 0}, {"clip", 0}})
					 .data.value("total")
					 .toInt(),
			0);
		QVERIFY(bus.execute("pattern.rename", {{"pattern", 1}, {"name", "Chorus"}}).ok);
		const auto placed = bus.execute("pattern.placeInSong", {{"pattern", 1}, {"position", 384}});
		QVERIFY(placed.ok);
		QCOMPARE(placed.data.value("length").toInt(), 4 * song->ticksPerBar());
		QVERIFY(bus.execute("pattern.removeFromSong", {{"track", placed.data.value("track")}, {"clip", 0}}).ok);
		QVERIFY(bus.execute("history.undo").ok);
		QVERIFY(bus.execute("pattern.remove", {{"pattern", 0}}).ok);
		QCOMPARE(bus.execute("pattern.get", {{"pattern", 0}}).data.value("name").toString(), QString("Chorus"));
		QVERIFY(bus.execute("history.undo").ok);
		QCOMPARE(bus.execute("pattern.list").data.value("patterns").toArray().size(), 2);
		QTemporaryDir directory;
		QVERIFY(directory.isValid());
		const auto path = directory.filePath("patterns.mmp");
		QVERIFY(bus.execute("song.save", {{"path", path}}).ok);
		QVERIFY(bus.execute("song.clearProject").ok);
		QVERIFY(bus.execute("song.load", {{"path", path}}).ok);
		QCOMPARE(bus.execute("pattern.get", {{"pattern", 1}}).data.value("bars").toInt(), 4);
		QVERIFY(!bus.execute("pattern.setLength", {{"pattern", 1}, {"bars", 0}}).ok);
		QVERIFY(!bus.execute("pattern.create", {{"index", 3}}).ok);
		QVERIFY(!bus.execute("pattern.get", {{"pattern", 2}}).ok);
	}

	void leavesActiveExportForEngineShutdown()
	{
		m_shutdownDirectory = std::make_unique<QTemporaryDir>();
		QVERIFY(m_shutdownDirectory->isValid());
		auto& bus = lmms::agent::CommandBus::instance();
		QVERIFY(bus.execute("song.setTempo", {{"bpm", 20}}).ok);
		const auto started = bus.execute("export.audio",
			{{"path", m_shutdownDirectory->filePath("shutdown.wav")},
				{"range", QJsonObject{{"start", 0}, {"end", lmms::MaxSongLength}}}});
		QVERIFY2(started.ok, qPrintable(started.errorMessage));
		QVERIFY(lmms::agent::hasActiveAudioExport());
	}
};

QTEST_GUILESS_MAIN(A3CommandsTest)
#include "A3CommandsTest.moc"
