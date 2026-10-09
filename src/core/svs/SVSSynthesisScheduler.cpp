#include "SVSSynthesisScheduler.h"
#include "ConfigManager.h"
#include "SVSCache.h"
#include "SVSTempoSnapshot.h"
#include "Song.h"
#include <QCoreApplication>
#include <QEvent>
#include <QRunnable>
#include <algorithm>
namespace lmms::svs {
SynthesisScheduler& SynthesisScheduler::instance()
{
	static SynthesisScheduler scheduler(
		std::clamp(ConfigManager::inst()->value("svs", "concurrency", "2").toInt(), 1, 16));
	return scheduler;
}
SynthesisScheduler::SynthesisScheduler(int budget, QObject* parent)
	: QObject(parent)
	, m_budget(std::clamp(budget, 1, 16))
{
	m_pool.setMaxThreadCount(m_budget);
}
SynthesisScheduler::~SynthesisScheduler()
{
	shutdown();
}
void SynthesisScheduler::shutdown()
{
	m_stopping = true;
	cancelAll();
	m_pool.waitForDone();
	m_declarationPool.waitForDone();
	// Workers have exited; their queued completions must not outlive Engine owners.
	QCoreApplication::removePostedEvents(this, QEvent::MetaCall);
	m_running.clear();
	m_busy.clear();
	m_active = 0;
}
std::shared_ptr<RenderControl> SynthesisScheduler::submit(std::shared_ptr<Plugin> plugin, Input input, int priority,
	StateCallback state, ResultCallback result, PartialCallback partial, QVector<SynthesisSegment> retained)
{
	auto job
		= std::make_shared<Job>(Job{std::move(plugin), std::move(input), priority, std::make_shared<RenderControl>(),
			std::move(state), std::move(result), std::move(partial), std::move(retained)});
	if (m_stopping)
	{
		job->control->cancel();
		job->state("Cancelled");
		return job->control;
	}
	m_queue.push_back(job);
	job->state("Queued");
	dispatch();
	return job->control;
}
void SynthesisScheduler::cancel(const std::shared_ptr<RenderControl>& control)
{
	if (control)
		control->cancel();
	dispatch();
}
void SynthesisScheduler::cancelAll()
{
	for (const auto& job : m_queue)
		job->control->cancel();
	for (const auto& job : m_running)
		job->control->cancel();
	m_queue.clear();
}
void SynthesisScheduler::dispatch()
{
	if (m_stopping)
		return;
	m_queue.erase(
		std::remove_if(m_queue.begin(), m_queue.end(), [](const auto& job) { return job->control->cancelled.load(); }),
		m_queue.end());
	std::stable_sort(
		m_queue.begin(), m_queue.end(), [](const auto& a, const auto& b) { return a->priority > b->priority; });
	while (m_active < m_budget)
	{
		// Plugin currently serializes instance access; do not spend worker slots
		// waiting for a non-concurrent engine's mutex.
		auto next = std::find_if(
			m_queue.begin(), m_queue.end(), [this](const auto& job) { return !m_busy.contains(job->plugin.get()); });
		if (next == m_queue.end())
			break;
		auto job = *next;
		m_queue.erase(next);
		m_busy.insert(job->plugin.get());
		m_running.push_back(job);
		++m_active;
		m_peak = std::max(m_peak, m_active);
		job->state("Rendering");
		m_pool.start(QRunnable::create([this, job] {
			QString error;
			std::shared_ptr<const Audio> result;
			QVector<SynthesisSegment> completedSegments;
			if (!job->control->cancelled)
			{
				if (job->input.tempoSnapshot)
				{
					auto& input = job->input;
					const auto end = input.document["position"].toDouble() - input.document["contentOffset"].toDouble()
						+ input.document["contentEndTick"].toDouble();
					QJsonArray points;
					if (!std::isfinite(end) || end > MaxSongLength
						|| !input.tempoSnapshot->buildMap(
							int(std::ceil(std::max(0., end))), points, error, job->control))
					{
						if (error.isEmpty())
							error = "Invalid SVS tempo extent";
					}
					else
					{
						input.document["tempoMap"] = points;
						TimeMapping mapping;
						if (readTimeMapping(input.document, input.secondsPerTick, mapping, error))
							input.duration
								= std::max(0., mapping.localSeconds(input.document["contentEndTick"].toDouble()));
					}
				}
				if (error.isEmpty())
				{
					if (!job->plugin)
					{
						result = Cache::instance().get(job->input.document["cacheOnlyKey"].toString(), job->input);
						if (!result)
							error = "Missing voice/plugin; no valid cached audio";
					}
					else
					{
						const bool cacheable
							= !job->plugin->identity().isEmpty() && !job->input.document.contains("developmentFaults");
						const auto key = Cache::key(job->input, job->plugin->identity());
						bool fresh = true;
						if (supportsSegmentedSynthesis(job->input))
						{
							TimeMapping mapping;
							if (readTimeMapping(job->input.document, job->input.secondsPerTick, mapping, error))
							{
								auto segments = planSynthesisSegments(job->input, mapping, error);
								auto publish = [&] {
									if (!job->partial || segments.size() < 2 || job->control->cancelled)
										return;
									auto audio = assembleSynthesisSegments(job->input, mapping, segments, error);
									if (!error.isEmpty())
										return;
									QMetaObject::invokeMethod(
										this,
										[job, audio = std::move(audio), segments] {
											if (!job->control->cancelled)
												job->partial(audio, segments);
										},
										Qt::QueuedConnection);
								};
								if (error.isEmpty())
								{
									for (auto& segment : segments)
									{
										for (const auto& old : job->retained)
											if (old.audio && segment.signature == old.signature
												&& segment.input.notes == old.input.notes)
											{
												segment.audio = old.audio;
												break;
											}
										if (!segment.audio && cacheable)
											segment.audio = Cache::instance().get(
												Cache::key(segment.input, job->plugin->identity()), segment.input);
										segment.cached = bool(segment.audio);
									}
									publish();
									for (int index = 0;
										index < segments.size() && error.isEmpty() && !job->control->cancelled; ++index)
									{
										QMetaObject::invokeMethod(
											this,
											[job, index, total = segments.size()] {
												if (!job->control->cancelled)
													job->state(QString("Rendering %1/%2").arg(index + 1).arg(total));
											},
											Qt::QueuedConnection);
										auto& segment = segments[index];
										if (segment.audio)
											continue;
										segment.audio = job->plugin->render(segment.input, error, job->control);
										if (!segment.audio)
										{
											if (error.isEmpty())
												error = "SVS segment synthesis failed";
											error += QString(" [segment=%1/%2]").arg(index + 1).arg(segments.size());
											break;
										}
										if (cacheable && !job->control->cancelled)
											Cache::instance().put(Cache::key(segment.input, job->plugin->identity()),
												segment.input, segment.audio);
										publish();
									}
									if (error.isEmpty() && !job->control->cancelled)
										result = assembleSynthesisSegments(job->input, mapping, segments, error, true);
									if (result)
										completedSegments = std::move(segments);
								}
							}
						}
						else
						{
							if (cacheable)
							{
								result = Cache::instance().get(key, job->input);
								fresh = !result;
							}
							if (!result && !job->control->cancelled)
								result = job->plugin->render(job->input, error, job->control);
						}
						if (result && cacheable && fresh && !job->control->cancelled)
						{
							Cache::instance().put(key, job->input, result);
							auto tagged = std::make_shared<Audio>(*result);
							tagged->cacheKey = key;
							tagged->cacheInputHash = Cache::editableKey(job->input);
							result = std::move(tagged);
						}
					}
				}
			}
			QMetaObject::invokeMethod(
				this,
				[this, job, result = std::move(result), segments = std::move(completedSegments), error] {
					--m_active;
					m_busy.remove(job->plugin.get());
					m_running.removeAll(job);
					if (!job->control->cancelled)
					{
						if (result && job->partial && !segments.isEmpty())
							job->partial(result, segments);
						job->result(result, error);
					}
					dispatch();
				},
				Qt::QueuedConnection);
		}));
	}
}
}
