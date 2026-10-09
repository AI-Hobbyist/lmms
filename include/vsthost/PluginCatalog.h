#ifndef LMMS_VSTHOST_PLUGIN_CATALOG_H
#define LMMS_VSTHOST_PLUGIN_CATALOG_H

#include "lmms_export.h"
#include "vsthost/Protocol.h"
#include "vsthost/ScanRoots.h"
#include <functional>
#include <stop_token>

namespace lmms::vsthost {
struct LMMS_EXPORT PluginIdentity
{
	Format format = Format::Vst3;
	Architecture architecture = Architecture::X64;
	std::array<std::uint8_t, 16> cid{};
	std::uint32_t vst2Id = 0;
	// VST2 IDs can collide. Retain the normalized module origin as part of the
	// compatibility identity, independently of the user-facing plugin name.
	QString vst2Source;
	QString key() const;
	bool operator==(const PluginIdentity&) const = default;
};

struct PluginLocator
{
	QString modulePath, binaryPath, version;
	QByteArray fingerprint;
	bool operator==(const PluginLocator&) const = default;
};

struct CatalogEntry
{
	PluginIdentity identity;
	PluginLocator locator;
	QString name, vendor, category, subcategories;
	std::uint32_t flags = 0;
	bool shell = false;
	bool operator==(const CatalogEntry&) const = default;
};

struct CatalogFailure
{
	QString path, operation;
	Error error = Error::LoadFailed;
	std::uint32_t nativeCode = 0;
	QByteArray fingerprint;
	bool quarantined = false;
};

struct LMMS_EXPORT CatalogReport
{
	std::vector<CatalogEntry> entries;
	std::vector<CatalogFailure> failures;
	QString cacheError;
	std::size_t modules = 0, cachedModules = 0;
	bool cancelled = false;
	std::vector<CatalogEntry> candidates(const PluginIdentity& identity) const;
};

class LMMS_EXPORT PluginCatalog
{
public:
	struct Configuration
	{
		QString vst2Helper32, vst2Helper64, vst3Helper32, vst3Helper64;
		QString cacheFile, hostVersion;
		QString ioHelper; // Empty selects RemoteCatalogIo.exe beside the x64 VST3 helper.
	};
	struct Options
	{
		bool force = false;
		std::uint32_t timeoutMs = 30000;
		std::uint32_t ioTimeoutMs = 30000;
	};
	using Progress = std::function<void(const QString&, std::size_t, std::size_t)>;
	explicit PluginCatalog(Configuration configuration);
	// Blocking control-worker operation, never an audio-thread API. Each call
	// owns its native scan sessions; reports are immutable snapshots and do not
	// hold/reconfigure active audio instances. Independent scans may run in
	// parallel; cache publication is atomic and protected by a short file lock.
	CatalogReport scan(
		const std::vector<ScanRoot>& roots, Options options, std::stop_token stop = {}, Progress progress = {}) const;

private:
	Configuration m_configuration;
};
} // namespace lmms::vsthost
#endif
