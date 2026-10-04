#ifndef LMMS_VSTHOST_CATALOG_JOBS_H
#define LMMS_VSTHOST_CATALOG_JOBS_H
#include "vsthost/PluginCatalog.h"
#include <memory>

namespace lmms::vsthost
{
class LMMS_EXPORT CatalogJobs
{
public:
	using Executor = std::function<CatalogReport(const std::vector<ScanRoot>&,
		PluginCatalog::Options, std::stop_token, PluginCatalog::Progress)>;
	struct Snapshot
	{
		std::uint64_t requested = 0, running = 0, published = 0;
		bool busy = false, cancelled = false;
		QString path;
		std::size_t completed = 0, total = 0;
		std::shared_ptr<const CatalogReport> report;
	};
	explicit CatalogJobs(PluginCatalog::Configuration configuration, Executor executor = {});
	~CatalogJobs();
	CatalogJobs(const CatalogJobs&) = delete;
	CatalogJobs& operator=(const CatalogJobs&) = delete;
	// Control/UI-thread requests never wait for a native scan or filesystem call.
	// A pending request replaces older pending work; a running request is cancelled.
	std::uint64_t refresh(std::vector<ScanRoot> roots, PluginCatalog::Options options);
	void cancel();
	Snapshot snapshot() const;
private:
	struct Impl;
	std::unique_ptr<Impl> m_impl;
};
} // namespace lmms::vsthost
#endif
