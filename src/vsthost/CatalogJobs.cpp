#include "vsthost/CatalogJobs.h"
#include <condition_variable>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <thread>

namespace lmms::vsthost {
struct CatalogJobs::Impl
{
	struct Job
	{
		std::uint64_t generation;
		std::vector<ScanRoot> roots;
		PluginCatalog::Options options;
	};
	Executor execute;
	mutable std::mutex mutex;
	std::condition_variable available;
	std::optional<Job> pending;
	std::stop_source activeStop;
	Snapshot state;
	bool stopping = false;
	std::uint64_t cancelled = 0;
	std::jthread worker;
	explicit Impl(Executor executor)
		: execute(std::move(executor))
		, worker([this] { run(); })
	{
	}
	void run()
	{
		for (;;)
		{
			Job job;
			std::stop_token stop;
			{
				std::unique_lock lock(mutex);
				available.wait(lock, [this] { return stopping || pending.has_value(); });
				if (stopping)
				{
					return;
				}
				job = std::move(*pending);
				pending.reset();
				activeStop = std::stop_source{};
				stop = activeStop.get_token();
				state.running = job.generation;
				state.path.clear();
				state.completed = 0;
				state.total = 0;
			}
			CatalogReport report;
			try
			{
				report = execute(job.roots, job.options, stop,
					[this, generation = job.generation](const QString& path, std::size_t completed, std::size_t total) {
						std::lock_guard lock(mutex);
						if (generation == state.requested && generation != cancelled && !stopping)
						{
							state.path = path;
							state.completed = completed;
							state.total = total;
						}
					});
			}
			catch (...)
			{
				report.failures.push_back({{}, "catalog worker exception", Error::InitializationFailed});
			}
			{
				std::lock_guard lock(mutex);
				if (!stopping && job.generation == state.requested && job.generation != cancelled
					&& !stop.stop_requested() && !report.cancelled)
				{
					state.report = std::make_shared<const CatalogReport>(std::move(report));
					state.published = job.generation;
				}
				state.running = 0;
				state.path.clear();
				state.completed = 0;
				state.total = 0;
			}
		}
	}
};

CatalogJobs::CatalogJobs(PluginCatalog::Configuration configuration, Executor executor)
{
	if (!executor)
	{
		executor = [catalog = PluginCatalog(std::move(configuration))](const std::vector<ScanRoot>& roots,
					   PluginCatalog::Options options, std::stop_token stop, PluginCatalog::Progress progress) {
			return catalog.scan(roots, options, stop, std::move(progress));
		};
	}
	m_impl = std::make_unique<Impl>(std::move(executor));
}

CatalogJobs::~CatalogJobs()
{
	std::stop_source stop;
	{
		std::lock_guard lock(m_impl->mutex);
		m_impl->stopping = true;
		m_impl->pending.reset();
		stop = m_impl->activeStop;
	}
	stop.request_stop();
	m_impl->available.notify_all();
	m_impl->worker.join();
}

std::uint64_t CatalogJobs::refresh(std::vector<ScanRoot> roots, PluginCatalog::Options options)
{
	std::stop_source stop;
	std::uint64_t generation;
	{
		std::lock_guard lock(m_impl->mutex);
		if (m_impl->state.requested == UINT64_MAX)
		{
			throw std::overflow_error("Catalog request generation exhausted");
		}
		generation = ++m_impl->state.requested;
		m_impl->state.path.clear();
		m_impl->state.completed = 0;
		m_impl->state.total = 0;
		m_impl->pending = Impl::Job{generation, std::move(roots), options};
		stop = m_impl->activeStop;
	}
	stop.request_stop();
	m_impl->available.notify_all();
	return generation;
}

void CatalogJobs::cancel()
{
	std::stop_source stop;
	{
		std::lock_guard lock(m_impl->mutex);
		m_impl->pending.reset();
		stop = m_impl->activeStop;
		m_impl->cancelled = m_impl->state.requested;
	}
	stop.request_stop();
}

CatalogJobs::Snapshot CatalogJobs::snapshot() const
{
	std::lock_guard lock(m_impl->mutex);
	auto snapshot = m_impl->state;
	snapshot.busy = m_impl->pending.has_value() || snapshot.running != 0;
	snapshot.cancelled = snapshot.requested != 0 && snapshot.requested == m_impl->cancelled;
	return snapshot;
}
} // namespace lmms::vsthost
