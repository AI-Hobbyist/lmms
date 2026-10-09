#include "SVCConversion.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QPointer>
#include <QTemporaryFile>
#include <QtEndian>
#include <algorithm>
#include <cmath>

#include "ConfigManager.h"
#include "SVCCache.h"
#include "SVCCatalog.h"
#include "SVCClip.h"
#include "SVCTrack.h"

namespace lmms::svc {
struct ConversionService::Task
{
	QPointer<SVCClip> clip;
	std::shared_ptr<PlaybackState> playback;
	std::vector<Segment> segments;
	EngineProfile engine;
	QJsonObject selection;
	ChunkConfig config;
	QString working;
	QString connectionIdentity;
	uint64_t generation = 0;
	std::atomic<unsigned> pending{0};
};

namespace {
void writeInput(QFile& file, const SourceAudio& source, const Segment& segment, uint32_t rate,
	const std::function<bool()>& cancelled)
{
	const auto frames = static_cast<uint64_t>(std::llround(double(segment.transmittedFrames()) * rate / source.rate));
	if (!frames || frames > (UINT32_MAX - 36) / 2) { throw std::runtime_error("SVC input WAV exceeds supported size"); }
	QByteArray header(44, 0);
	header.replace(0, 4, "RIFF");
	qToLittleEndian<uint32_t>(36 + frames * 2, header.data() + 4);
	header.replace(8, 8, "WAVEfmt ");
	qToLittleEndian<uint32_t>(16, header.data() + 16);
	qToLittleEndian<uint16_t>(1, header.data() + 20);
	qToLittleEndian<uint16_t>(1, header.data() + 22);
	qToLittleEndian<uint32_t>(rate, header.data() + 24);
	qToLittleEndian<uint32_t>(rate * 2, header.data() + 28);
	qToLittleEndian<uint16_t>(2, header.data() + 32);
	qToLittleEndian<uint16_t>(16, header.data() + 34);
	header.replace(36, 4, "data");
	qToLittleEndian<uint32_t>(frames * 2, header.data() + 40);
	if (file.write(header) != header.size()) { throw std::runtime_error("Cannot write SVC input WAV"); }
	for (uint64_t offset = 0; offset < frames;)
	{
		if (cancelled()) { throw std::runtime_error("SVC conversion cancelled"); }
		const auto count = std::min<uint64_t>(SVC_MAX_FEED / 2, frames - offset);
		QByteArray bytes(count * 2, 0);
		for (uint64_t index = 0; index < count; ++index)
		{
			const auto position = segment.inputStart + double(offset + index) * source.rate / rate;
			if (position >= segment.inputEnd) { continue; }
			const auto frame = static_cast<uint64_t>(position);
			const auto next = std::min(frame + 1, segment.inputEnd - 1);
			const auto fraction = position - frame;
			const auto value = (source.stereo[frame * 2] + source.stereo[frame * 2 + 1]) * .5 * (1 - fraction)
				+ (source.stereo[next * 2] + source.stereo[next * 2 + 1]) * .5 * fraction;
			qToLittleEndian<int16_t>(static_cast<int16_t>(std::clamp(std::llround(value * 32768), -32768LL, 32767LL)),
				bytes.data() + index * 2);
		}
		if (file.write(bytes) != bytes.size()) { throw std::runtime_error("Cannot write SVC input samples"); }
		offset += count;
	}
	if (!file.flush() || !file.seek(0)) { throw std::runtime_error("Cannot rewind SVC input WAV"); }
}

QJsonObject mapping(const Segment& segment)
{
	return {{"start", QString::number(segment.start)}, {"end", QString::number(segment.end)},
		{"inputStart", QString::number(segment.inputStart)}, {"inputEnd", QString::number(segment.inputEnd)},
		{"padding", QString::number(segment.padding)}, {"sampleRate", int(segment.sampleRate)},
		{"algorithmVersion", int(segment.algorithmVersion)}};
}
} // namespace

ConversionService& ConversionService::instance()
{
	static ConversionService service;
	return service;
}

ConversionService::~ConversionService()
{ shutdown(); }

void ConversionService::render(SVCTrack* track)
{
	for (auto* base : track->getClips())
	{
		renderClip(static_cast<SVCClip*>(base));
	}
}

void ConversionService::renderClip(SVCClip* clip)
{
	auto* track = static_cast<SVCTrack*>(clip->getTrack());
	if (!clip->playback())
	{
		clip->setStatus(tr("Import audio before rendering"));
		return;
	}
	QString error;
	const auto selection = Catalog::instance().requestSelection(track->selection(), error);
	if (!error.isEmpty())
	{
		clip->setStatus(error);
		return;
	}
	auto task = std::make_shared<Task>();
	task->clip = clip;
	task->playback = clip->playback();
	task->engine = Catalog::instance().engine(selection.value("engine_id").toString());
	task->connectionIdentity = Catalog::instance().connection(task->engine.id).address;
	task->selection = selection;
	task->config = track->chunkConfig();
	task->working = ConfigManager::inst()->workingDir();
	try
	{
		std::lock_guard lock(m_mutex);
		if (m_queue.size() >= 32)
		{
			clip->setStatus(tr("SVC queue is full; try again"));
			return;
		}
		clip->invalidate();
		task->generation = task->playback->generation();
		clip->setStatus(tr("Queued for silence analysis and conversion"));
		m_queue.push_back(task);
		if (!m_worker.joinable())
		{
			m_stopping = false;
			m_worker = std::thread([this] { work(); });
		}
		m_wake.notify_all();
	}
	catch (const std::exception& exception)
	{
		clip->setStatus(QString::fromUtf8(exception.what()));
	}
}

void ConversionService::cancel(SVCTrack* track)
{
	for (auto* base : track->getClips())
	{
		auto* clip = static_cast<SVCClip*>(base);
		clip->invalidate();
		clip->setStatus(tr("Cancelled; partial result retained"));
	}
	m_wake.notify_all();
}

bool ConversionService::post(const std::shared_ptr<Task>& task, std::function<void(SVCClip*)> action)
{
	std::unique_lock lock(m_mutex);
	m_wake.wait(lock, [&] { return m_stopping || task->pending < 2; });
	if (m_stopping) { return false; }
	++task->pending;
	QMetaObject::invokeMethod(
		this,
		[this, task, action = std::move(action)] {
			if (task->clip && task->playback->generation() == task->generation) { action(task->clip.data()); }
			--task->pending;
			m_wake.notify_all();
		},
		Qt::QueuedConnection);
	return true;
}

void ConversionService::work()
{
	for (;;)
	{
		std::shared_ptr<Task> task;
		{
			std::unique_lock lock(m_mutex);
			m_wake.wait(lock, [&] { return m_stopping || !m_queue.empty(); });
			if (m_stopping) { return; }
			task = m_queue.front();
			m_queue.pop_front();
		}
		run(task);
	}
}

void ConversionService::run(const std::shared_ptr<Task>& task)
{
	const auto cancelled = [&] { return m_stopping || task->playback->generation() != task->generation; };
	if (cancelled()) { return; }
	try
	{
		post(task, [](SVCClip* clip) { clip->setStatus(tr("Analyzing silence and splitting input")); });
		const auto source = task->playback->snapshot()->source;
		const auto limits = task->engine.capabilities.value("limits").toObject();
		task->segments = segmentAudio(source->stereo.data(), source->frames(), 2, source->rate, task->config,
			{limits.value("min_seconds").toDouble(), limits.value("max_seconds").toDouble(), UINT64_MAX});
		const auto inputRate = task->engine.inputRate ? task->engine.inputRate : source->rate;
		for (const auto& segment : task->segments)
		{
			const auto bytes = 44.0 + std::round(double(segment.transmittedFrames()) * inputRate / source->rate) * 2;
			if (bytes > limits.value("max_upload_bytes").toDouble())
			{
				throw std::invalid_argument("SVC request exceeds the backend upload byte limit");
			}
		}
		if (cancelled()) { return; }
		if (!post(task, [task](SVCClip* clip) { task->generation = clip->beginConversion(task->segments); }))
		{
			return;
		}
		// Only the worker waits for GUI metadata publication; the GUI never waits for analysis or network I/O.
		std::unique_lock lock(m_mutex);
		m_wake.wait(lock, [&] { return m_stopping || task->pending == 0; });
	}
	catch (const std::exception& exception)
	{
		const auto error = QString::fromUtf8(exception.what());
		post(task, [error](SVCClip* clip) { clip->setStatus(error); });
		return;
	}
	for (uint64_t segment = 0; segment < task->segments.size() && !cancelled(); ++segment)
	{
		std::unique_ptr<CachePair> cache;
		try
		{
			const auto& range = task->segments[segment];
			const auto directory = QDir(task->working).filePath("cache/svc/" + task->engine.id + "/input");
			if (!QDir().mkpath(directory)) { throw std::runtime_error("Cannot create SVC cache directory"); }
			QTemporaryFile prepared(directory + "/staging-XXXXXX.wav");
			if (!prepared.open()) { throw std::runtime_error("Cannot prepare SVC input file"); }
			const auto source = task->playback->snapshot()->source;
			writeInput(
				prepared, *source, range, task->engine.inputRate ? task->engine.inputRate : source->rate, cancelled);
			auto snapshot = task->selection;
			snapshot.insert("connection_identity", task->connectionIdentity);
			snapshot.insert("segment", mapping(range));
			snapshot.insert("chunking",
				QJsonObject{{"silenceThresholdDbfs", task->config.silenceThresholdDbfs},
					{"lengthThresholdSeconds", task->config.lengthThresholdSeconds},
					{"forcedChunkSeconds", task->config.forcedChunkSeconds}});
			cache = CachePair::create(task->working, task->engine.id, prepared, snapshot);
			QFile input(cache->inputPath());
			if (!input.open(QIODevice::ReadOnly) || (task->engine.inputIsPcm && !input.seek(44)))
			{
				throw std::runtime_error("Cannot read SVC cached input");
			}
			struct Context
			{
				ConversionService* service;
				std::shared_ptr<Task> task;
				CachePair* cache;
				bool invalid = false;
				QString error;
			} context{this, task, cache.get()};
			const auto selection = QJsonDocument(task->selection).toJson(QJsonDocument::Compact);
			svc_request request{};
			request.size = sizeof(request);
			request.abi_version = SVC_ABI_VERSION;
			request.generation_id = task->generation;
			request.segment_id = segment;
			request.selection_json = selection.constData();
			request.input_user = &input;
			request.read = [](void* user, uint8_t* destination, size_t capacity) -> int64_t {
				return static_cast<QFile*>(user)->read(
					reinterpret_cast<char*>(destination), std::min<size_t>(capacity, SVC_MAX_FEED));
			};
			request.callback_user = &context;
			request.callback = [](void* user, const svc_event* event) -> int {
				auto& context = *static_cast<Context*>(user);
				try
				{
					if (context.service->m_stopping || context.task->playback->generation() != event->generation_id)
					{
						return 1;
					}
					if (event->type == SVC_AUDIO)
					{
						if (!context.cache->append(*event) || !context.task->playback->publish(*event))
						{
							context.invalid = true;
							return 1;
						}
						auto metadata = *event;
						metadata.bytes = nullptr;
						metadata.request_id = nullptr;
						context.service->post(
							context.task, [metadata](SVCClip* clip) { clip->publishStatus(metadata); });
					}
					else if (event->type == SVC_ERROR)
					{
						context.error = QString::fromUtf8(reinterpret_cast<const char*>(event->bytes),
							std::min<size_t>(event->byte_count, SVC_MAX_HEADER));
					}
					else if (event->type == SVC_START || event->type == SVC_PROGRESS || event->type == SVC_DONE)
					{
						auto stage = event->type == SVC_DONE ? QObject::tr("Audio received; validating completion")
							: event->type == SVC_START		 ? QObject::tr("Backend preprocessing")
															 : QObject::tr("Backend processing");
						if (event->bytes && event->byte_count <= SVC_MAX_HEADER)
						{
							const auto progress = QJsonDocument::fromJson(
								QByteArray(reinterpret_cast<const char*>(event->bytes), event->byte_count))
													  .object();
							if (progress.value("stage") == "upload")
							{
								stage = QObject::tr("Uploading: %1 bytes")
											.arg(progress.value("uploaded_bytes").toDouble(), 0, 'f', 0);
							}
							else if (progress.value("stage") == "preprocessing")
							{
								stage = QObject::tr("Upload complete; backend preprocessing");
							}
						}
						context.service->post(context.task, [stage](SVCClip* clip) { clip->setStatus(stage); });
					}
					return 0;
				}
				catch (...)
				{
					context.invalid = true;
					return 1;
				}
			};
			const auto* api = task->engine.api;
			void* job = api->start(task->engine.context.get(), &request);
			if (!job) { throw std::runtime_error("SVC backend rejected the frozen request"); }
			svc_status terminal = SVC_OK;
			while (terminal == SVC_OK && !cancelled())
			{
				terminal = api->pump(job);
			}
			if (cancelled())
			{
				api->cancel(job);
				terminal = SVC_CANCELLED;
			}
			api->destroy(job);
			if (context.invalid) { terminal = SVC_FAILED; }
			const auto valid = task->playback->complete(task->generation, segment, terminal);
			if (!valid || !cache->complete(terminal))
			{
				cache->fail(terminal == SVC_CANCELLED ? "cancelled-partial" : "failed-partial");
				terminal = terminal == SVC_CANCELLED ? SVC_CANCELLED : SVC_FAILED;
			}
			const QJsonObject reference = terminal == SVC_COMPLETE
				? QJsonObject{{"engine", task->engine.id}, {"hash", cache->hash()}}
				: QJsonObject{};
			post(task,
				[generation = task->generation, segment, terminal, reference, error = context.error](SVCClip* clip) {
					clip->finishSegment(generation, segment, terminal, reference, true);
					if (!error.isEmpty())
					{
						clip->setStatus(QObject::tr("Failed: %1; partial result retained").arg(error));
					}
				});
			if (terminal != SVC_COMPLETE) { break; }
		}
		catch (const std::exception& exception)
		{
			if (cache) { cache->fail(cancelled() ? "cancelled-partial" : "failed-partial"); }
			const auto error = QString::fromUtf8(exception.what());
			post(task, [error, generation = task->generation, segment](SVCClip* clip) {
				clip->finishSegment(generation, segment, SVC_FAILED);
				clip->setStatus(QObject::tr("Failed: %1; partial result retained").arg(error));
			});
			break;
		}
	}
}

void ConversionService::shutdown()
{
	m_stopping = true;
	m_wake.notify_all();
	if (m_worker.joinable()) { m_worker.join(); }
	std::lock_guard lock(m_mutex);
	m_queue.clear();
}
} // namespace lmms::svc
