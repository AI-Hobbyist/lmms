#include "vsthost/PluginCatalog.h"
#include "vsthost/Vst2Scanner.h"
#include "vsthost/Vst3Scanner.h"
#include "vsthost/CatalogIo.h"
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QMap>
#include <QSet>
#include <algorithm>
#include <cmath>

namespace lmms::vsthost {
namespace {
constexpr qsizetype MaxModules = 65536;
constexpr qsizetype MaxEntries = 262144;
struct Module
{
	QString path, binary;
	Format format = Format::Vst3;
	Architecture architecture = Architecture::X64;
	QByteArray fingerprint;
	std::vector<CatalogEntry> entries;
	QString key() const { return QString::number(static_cast<unsigned>(format)) + ":" + scanPathKey(binary); }
};

QJsonObject entryJson(const CatalogEntry& entry)
{
	const auto cid = QByteArray(reinterpret_cast<const char*>(entry.identity.cid.data()), 16).toHex();
	return {{"cid", QString::fromLatin1(cid)}, {"id", static_cast<double>(entry.identity.vst2Id)},
		{"source", entry.identity.vst2Source}, {"version", entry.locator.version}, {"name", entry.name},
		{"vendor", entry.vendor}, {"category", entry.category}, {"subcategories", entry.subcategories},
		{"flags", static_cast<double>(entry.flags)}, {"shell", entry.shell}};
}

bool hex(const QJsonValue& value, qsizetype count, QByteArray& decoded)
{
	if (!value.isString() || value.toString().size() != count * 2)
	{
		return false;
	}
	const auto encoded = value.toString().toLatin1().toLower();
	decoded = QByteArray::fromHex(encoded);
	return decoded.size() == count && decoded.toHex() == encoded;
}

bool number(const QJsonValue& value, std::uint32_t& decoded)
{
	if (!value.isDouble())
	{
		return false;
	}
	const auto d = value.toDouble();
	if (!std::isfinite(d) || d < 0 || d > UINT32_MAX || std::floor(d) != d)
	{
		return false;
	}
	decoded = static_cast<std::uint32_t>(d);
	return true;
}

bool text(const QJsonObject& json, const char* key, QString& decoded, qsizetype limit = 4096)
{
	if (!json.value(key).isString())
	{
		return false;
	}
	decoded = json.value(key).toString();
	return decoded.size() <= limit && (limit == 32767 || decoded.toUtf8().size() <= limit)
		&& !decoded.contains(QChar(0));
}

bool readDocument(const CatalogIo& io, const QString& path, const QString& version, QJsonArray& records, QString& error)
{
	const auto reply = io.request({{"op", "read-document"}, {"path", path}});
	if (reply.error != Error::None || !reply.object.value("exists").isBool())
	{
		error = "Supervised catalog cache read failed: " + path;
		return false;
	}
	if (!reply.object.value("exists").toBool())
	{
		return true;
	}
	const auto object = reply.object.value("document").toObject();
	if (reply.object.value("invalid").toBool(true) || !reply.object.value("document").isObject()
		|| object.value("schema").toDouble() != 1 || object.value("host").toString() != version
		|| !object.value("records").isArray() || object.value("records").toArray().size() > MaxModules)
	{
		error = "Catalog cache schema/host mismatch or invalid JSON: " + path;
		return false;
	}
	records = object.value("records").toArray();
	return true;
}

bool readCache(
	const CatalogIo& io, const QString& path, const QString& version, QMap<QString, Module>& cache, QString& error)
{
	QJsonArray records;
	if (!readDocument(io, path, version, records, error))
	{
		return false;
	}
	qsizetype total = 0;
	QMap<QString, Module> parsed;
	for (const auto& record : records)
	{
		if (!record.isObject())
		{
			return false;
		}
		const auto json = record.toObject();
		Module module;
		std::uint32_t format = 0, architecture = 0;
		if (!text(json, "path", module.path, 32767) || !text(json, "binary", module.binary, 32767)
			|| !QDir::isAbsolutePath(module.path) || !QDir::isAbsolutePath(module.binary)
			|| !number(json.value("format"), format) || (format != 2 && format != 3)
			|| !number(json.value("architecture"), architecture) || (architecture != 0x14c && architecture != 0x8664)
			|| !hex(json.value("fingerprint"), 32, module.fingerprint) || !json.value("entries").isArray())
		{
			return false;
		}
		module.format = static_cast<Format>(format);
		module.architecture = static_cast<Architecture>(architecture);
		QSet<QString> identities;
		for (const auto& item : json.value("entries").toArray())
		{
			if (!item.isObject() || ++total > MaxEntries || module.entries.size() >= 4096)
			{
				return false;
			}
			const auto row = item.toObject();
			CatalogEntry entry;
			QByteArray cid;
			entry.identity.format = module.format;
			entry.identity.architecture = module.architecture;
			if (!hex(row.value("cid"), 16, cid) || !number(row.value("id"), entry.identity.vst2Id)
				|| !number(row.value("flags"), entry.flags) || !text(row, "source", entry.identity.vst2Source, 32767)
				|| !text(row, "version", entry.locator.version) || !text(row, "name", entry.name)
				|| entry.name.isEmpty() || !text(row, "vendor", entry.vendor) || !text(row, "category", entry.category)
				|| !text(row, "subcategories", entry.subcategories) || !row.value("shell").isBool())
			{
				return false;
			}
			std::copy_n(reinterpret_cast<const std::uint8_t*>(cid.constData()), 16, entry.identity.cid.begin());
			entry.shell = row.value("shell").toBool();
			if ((module.format == Format::Vst3
					&& (cid == QByteArray(16, '\0') || entry.shell || entry.identity.vst2Id
						|| !entry.identity.vst2Source.isEmpty() || entry.category != "Audio Module Class"))
				|| (module.format == Format::Vst2
					&& (cid != QByteArray(16, '\0') || (entry.shell && !entry.identity.vst2Id)
						|| entry.identity.vst2Source != scanPathKey(module.path))))
			{
				return false;
			}
			if (identities.contains(entry.identity.key()))
			{
				return false;
			}
			identities.insert(entry.identity.key());
			entry.locator.modulePath = module.path;
			entry.locator.binaryPath = module.binary;
			entry.locator.fingerprint = module.fingerprint;
			module.entries.push_back(std::move(entry));
		}
		if (parsed.contains(module.key()))
		{
			return false;
		}
		parsed.insert(module.key(), std::move(module));
	}
	cache = std::move(parsed);
	return true;
}

QMap<QString, CatalogFailure> readFailures(const CatalogIo& io, const QString& path, const QString& version)
{
	QJsonArray records;
	QString ignored;
	if (!readDocument(io, path, version, records, ignored))
	{
		return {};
	}
	QMap<QString, CatalogFailure> failures;
	for (const auto& record : records)
	{
		if (!record.isObject())
		{
			return {};
		}
		const auto row = record.toObject();
		CatalogFailure failure;
		std::uint32_t code = 0;
		if (!text(row, "path", failure.path, 32767) || !text(row, "operation", failure.operation)
			|| !hex(row.value("fingerprint"), 32, failure.fingerprint) || !number(row.value("error"), code) || !code
			|| code > static_cast<unsigned>(Error::ProcessingFailed)
			|| !number(row.value("native"), failure.nativeCode))
		{
			return {};
		}
		failure.error = static_cast<Error>(code);
		failures.insert(scanPathKey(failure.path), failure);
	}
	return failures;
}

QByteArray fingerprint(const CatalogIo& io, const Module& module, CatalogFailure& failure)
{
	const auto reply = io.request({{"op", "fingerprint"}, {"path", module.path}, {"binary", module.binary}});
	QByteArray bytes;
	if (reply.error == Error::None && hex(reply.object.value("fingerprint"), 32, bytes))
	{
		return bytes;
	}
	failure = {
		module.binary, "fingerprint", reply.error == Error::None ? Error::LoadFailed : reply.error, reply.nativeCode};
	return {};
}

void discover(const CatalogIo& io, const ScanRoot& root, QSet<QString>& visited, std::vector<Module>& discovered,
	std::vector<CatalogFailure>& failures)
{
	QByteArray roots;
	QString ignored;
	encodeScanRoots({root}, roots, ignored);
	const auto reply = io.request({{"op", "discover"}, {"roots", QJsonDocument::fromJson(roots).object()}});
	auto fail = [&](Error error) { failures.push_back({root.path, "filesystem discovery", error, reply.nativeCode}); };
	if (reply.error != Error::None)
	{
		fail(reply.error);
		return;
	}
	if (!reply.object.value("modules").isArray() || !reply.object.value("failures").isArray())
	{
		fail(Error::InvalidMessage);
		return;
	}
	const auto modules = reply.object.value("modules").toArray(), errors = reply.object.value("failures").toArray();
	if (modules.size() + discovered.size() > MaxModules || errors.size() + failures.size() > MaxModules)
	{
		fail(Error::InvalidMessage);
		return;
	}
	std::vector<Module> parsed;
	std::vector<CatalogFailure> parsedErrors;
	for (const auto& item : modules)
	{
		const auto object = item.toObject();
		Module module;
		std::uint32_t format = 0, architecture = 0;
		if (!item.isObject() || !text(object, "path", module.path, 32767)
			|| !text(object, "binary", module.binary, 32767) || !QDir::isAbsolutePath(module.path)
			|| !QDir::isAbsolutePath(module.binary) || !number(object.value("format"), format)
			|| (format != 2 && format != 3) || !number(object.value("architecture"), architecture)
			|| (architecture != 0x14c && architecture != 0x8664)
			|| !root.formats.contains(format == 2 ? "vst2" : "vst3"))
		{
			fail(Error::InvalidMessage);
			return;
		}
		module.format = static_cast<Format>(format);
		module.architecture = static_cast<Architecture>(architecture);
		parsed.push_back(std::move(module));
	}
	for (const auto& item : errors)
	{
		const auto object = item.toObject();
		CatalogFailure failure;
		std::uint32_t error = 0;
		if (!item.isObject() || !text(object, "path", failure.path, 32767)
			|| !text(object, "operation", failure.operation) || !number(object.value("error"), error) || !error
			|| error > static_cast<unsigned>(Error::ProcessingFailed))
		{
			fail(Error::InvalidMessage);
			return;
		}
		failure.error = static_cast<Error>(error);
		parsedErrors.push_back(std::move(failure));
	}
	for (auto& module : parsed)
	{
		if (visited.contains(module.key()))
		{
			continue;
		}
		visited.insert(module.key());
		discovered.push_back(std::move(module));
	}
	failures.insert(failures.end(), parsedErrors.begin(), parsedErrors.end());
}
} // namespace

QString PluginIdentity::key() const
{
	const auto bits = architecture == Architecture::X86 ? "32:" : "64:";
	if (format == Format::Vst3)
	{
		return "vst3:" + QString(bits)
			+ QString::fromLatin1(QByteArray(reinterpret_cast<const char*>(cid.data()), 16).toHex());
	}
	return "vst2:" + QString(bits) + QString::number(vst2Id) + ":" + vst2Source;
}

std::vector<CatalogEntry> CatalogReport::candidates(const PluginIdentity& identity) const
{
	std::vector<CatalogEntry> found;
	for (const auto& entry : entries)
	{
		if (entry.identity == identity)
		{
			found.push_back(entry);
		}
	}
	return found;
}

PluginCatalog::PluginCatalog(Configuration configuration)
	: m_configuration(std::move(configuration))
{
	if (m_configuration.ioHelper.isEmpty() && !m_configuration.vst3Helper64.isEmpty())
	{
		m_configuration.ioHelper = QFileInfo(m_configuration.vst3Helper64).absolutePath() + "/RemoteCatalogIo.exe";
	}
}

CatalogReport PluginCatalog::scan(
	const std::vector<ScanRoot>& roots, Options options, std::stop_token stop, Progress progress) const
{
	CatalogReport report;
	std::vector<Module> discovered;
	CatalogIo io(m_configuration.ioHelper, options.ioTimeoutMs, stop);
	QSet<QString> modulePaths, rootPaths;
	if (roots.size() > 256 || !options.timeoutMs || options.timeoutMs > 300000 || !options.ioTimeoutMs
		|| options.ioTimeoutMs > 300000
		|| (!m_configuration.cacheFile.isEmpty() && m_configuration.hostVersion.isEmpty()))
	{
		report.failures.push_back({{}, "configuration", Error::InvalidMessage});
		return report;
	}
	for (const auto& root : roots)
	{
		if (stop.stop_requested())
		{
			report.cancelled = true;
			return report;
		}
		if (!root.enabled)
		{
			continue;
		}
		std::vector<ScanRoot> normalized;
		QString error;
		if (!normalizeScanRoots({root}, normalized, error))
		{
			report.failures.push_back({root.path, "configuration", Error::InvalidMessage});
			continue;
		}
		const auto key = scanPathKey(normalized[0].path);
		if (rootPaths.contains(key))
		{
			continue;
		}
		rootPaths.insert(key);
		discover(io, normalized[0], modulePaths, discovered, report.failures);
	}
	report.modules = discovered.size();
	QMap<QString, Module> cache;
	QMap<QString, CatalogFailure> quarantined;
	if (!m_configuration.cacheFile.isEmpty() && !options.force)
	{
		if (!readCache(io, m_configuration.cacheFile, m_configuration.hostVersion, cache, report.cacheError))
		{
			cache.clear();
			if (report.cacheError.isEmpty())
			{
				report.cacheError = "Invalid catalog cache records";
			}
		}
		quarantined = readFailures(io, m_configuration.cacheFile + ".failures.json", m_configuration.hostVersion);
	}
	std::vector<Module> successful;
	std::size_t processed = 0;
	for (auto& module : discovered)
	{
		if (stop.stop_requested())
		{
			break;
		}
		if (progress)
		{
			progress(module.path, processed, discovered.size());
		}
		++processed;
		CatalogFailure ioFailure;
		module.fingerprint = fingerprint(io, module, ioFailure);
		if (stop.stop_requested())
		{
			break;
		}
		if (module.fingerprint.size() != 32)
		{
			report.failures.push_back(ioFailure);
			continue;
		}
		const auto key = module.key();
		if (cache.contains(key) && cache[key].fingerprint == module.fingerprint
			&& cache[key].architecture == module.architecture && cache[key].path == module.path)
		{
			module.entries = cache[key].entries;
			++report.cachedModules;
		}
		else
		{
			const auto failure = quarantined.constFind(scanPathKey(module.binary));
			if (failure != quarantined.cend() && failure->fingerprint == module.fingerprint)
			{
				auto skipped = *failure;
				skipped.quarantined = true;
				report.failures.push_back(std::move(skipped));
				continue;
			}
			const bool x86 = module.architecture == Architecture::X86;
			const auto helper = module.format == Format::Vst3
				? (x86 ? m_configuration.vst3Helper32 : m_configuration.vst3Helper64)
				: (x86 ? m_configuration.vst2Helper32 : m_configuration.vst2Helper64);
			if (helper.isEmpty())
			{
				report.failures.push_back({module.binary, "helper", Error::MissingHelper, 0, module.fingerprint});
				continue;
			}
			const auto helperInfo = io.request({{"op", "pe"}, {"path", helper}, {"dll", false}});
			if (helperInfo.error != Error::None)
			{
				report.failures.push_back(
					{module.binary, "helper", helperInfo.error, helperInfo.nativeCode, module.fingerprint});
				continue;
			}
			std::uint32_t helperArchitecture = 0;
			if (!helperInfo.object.value("exists").isBool() || !helperInfo.object.value("valid").isBool()
				|| !number(helperInfo.object.value("architecture"), helperArchitecture))
			{
				report.failures.push_back({module.binary, "helper", Error::InvalidMessage, 0, module.fingerprint});
				continue;
			}
			if (!helperInfo.object.value("exists").toBool())
			{
				report.failures.push_back({module.binary, "helper", Error::MissingHelper, 0, module.fingerprint});
				continue;
			}
			if (!helperInfo.object.value("valid").toBool()
				|| helperArchitecture != static_cast<unsigned>(module.architecture))
			{
				report.failures.push_back(
					{module.binary, "helper", Error::UnsupportedArchitecture, 0, module.fingerprint});
				continue;
			}
			Error error = Error::None;
			ProcessSupervisor::Fault fault;
			if (module.format == Format::Vst3)
			{
				const auto scan = scanVst3(helper.toStdWString(), module.path.toStdWString(), options.timeoutMs, stop);
				error = scan.error;
				fault = scan.fault;
				for (const auto& info : scan.classes)
				{
					if (!info.audioClass())
					{
						continue;
					}
					CatalogEntry entry;
					entry.identity = {module.format, module.architecture, info.cid};
					entry.locator = {module.path, module.binary, QString::fromUtf8(info.version), module.fingerprint};
					entry.name = QString::fromUtf8(info.name);
					entry.vendor = QString::fromUtf8(info.vendor);
					entry.category = QString::fromUtf8(info.category);
					entry.subcategories = QString::fromUtf8(info.subcategories);
					entry.flags = info.flags;
					module.entries.push_back(std::move(entry));
				}
			}
			else
			{
				const auto scan = scanVst2({helper.toStdWString(), {L"headless"}, options.timeoutMs},
					module.binary.toUtf8().toStdString(), options.timeoutMs, stop);
				error = scan.error;
				fault = scan.fault;
				for (const auto& info : scan.entries)
				{
					CatalogEntry entry;
					entry.identity = {module.format, module.architecture, {}, info.id, scanPathKey(module.path)};
					entry.locator
						= {module.path, module.binary, QString::fromStdString(scan.version), module.fingerprint};
					entry.name = QString::fromUtf8(info.name);
					entry.shell = scan.shell;
					entry.flags = info.flags;
					entry.category = (info.flags & 256) ? "Instrument" : "Effect";
					entry.vendor = QString::fromUtf8(scan.vendor);
					module.entries.push_back(std::move(entry));
				}
			}
			if (stop.stop_requested())
			{
				break;
			}
			if (error != Error::None)
			{
				report.failures.push_back(
					{module.binary, "native:" + QString::number(static_cast<unsigned>(fault.stage)), error,
						fault.nativeCode, module.fingerprint});
				continue;
			}
			const auto verified = fingerprint(io, module, ioFailure);
			if (verified.isEmpty())
			{
				report.failures.push_back(ioFailure);
				continue;
			}
			if (verified != module.fingerprint)
			{
				report.failures.push_back({module.binary, "replaced", Error::InvalidState, 0, module.fingerprint});
				continue;
			}
		}
		if (report.entries.size() + module.entries.size() > MaxEntries)
		{
			report.failures.push_back({module.path, "capacity", Error::InvalidMessage});
			continue;
		}
		report.entries.insert(report.entries.end(), module.entries.begin(), module.entries.end());
		successful.push_back(std::move(module));
	}
	report.cancelled = stop.stop_requested();
	if (report.cancelled)
	{
		report.entries.clear();
		return report;
	}
	if (!m_configuration.cacheFile.isEmpty())
	{
		QJsonArray modules;
		for (const auto& module : successful)
		{
			QJsonArray entries;
			for (const auto& entry : module.entries)
			{
				entries.append(entryJson(entry));
			}
			modules.append(QJsonObject{{"path", module.path}, {"binary", module.binary},
				{"format", static_cast<int>(module.format)}, {"architecture", static_cast<int>(module.architecture)},
				{"fingerprint", QString::fromLatin1(module.fingerprint.toHex())}, {"entries", entries}});
		}
		QJsonArray failures;
		for (const auto& failure : report.failures)
		{
			if (failure.fingerprint.size() != 32)
			{
				continue;
			}
			failures.append(QJsonObject{{"path", failure.path}, {"operation", failure.operation},
				{"error", static_cast<int>(failure.error)}, {"native", static_cast<double>(failure.nativeCode)},
				{"fingerprint", QString::fromLatin1(failure.fingerprint.toHex())}});
		}
		const auto published = io.request({{"op", "publish"}, {"path", m_configuration.cacheFile},
			{"success", QJsonObject{{"schema", 1}, {"host", m_configuration.hostVersion}, {"records", modules}}},
			{"failures", QJsonObject{{"schema", 1}, {"host", m_configuration.hostVersion}, {"records", failures}}}});
		if (published.error != Error::None)
		{
			report.cacheError = "Supervised catalog publication failed";
		}
		else if (!published.object.value("error").isString())
		{
			report.cacheError = "Invalid catalog publication reply";
		}
		else if (!published.object.value("error").toString().isEmpty())
		{
			report.cacheError = published.object.value("error").toString();
		}
	}
	report.cancelled = stop.stop_requested();
	if (report.cancelled)
	{
		report.entries.clear();
	}
	return report;
}
} // namespace lmms::vsthost
