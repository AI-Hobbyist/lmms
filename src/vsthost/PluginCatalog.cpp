#include "vsthost/PluginCatalog.h"
#include "vsthost/Vst2Scanner.h"
#include "vsthost/Vst3Scanner.h"
#include "vsthost/CatalogIo.h"
#include "vsthost/NativeVstPreset.h"
#include "vsthost/Vst3HostProxy.h"
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

void recordPresetResult(CatalogReport& report, PresetWriteResult result)
{
	switch (result)
	{
	case PresetWriteResult::Written: ++report.presetsWritten; break;
	case PresetWriteResult::AlreadyExists: ++report.presetsExisting; break;
	case PresetWriteResult::Failed: ++report.presetFailures; break;
	}
}

void recordVst3Layout(CatalogEntry& entry, const Vst3Metadata& metadata)
{
	static const QRegularExpression layoutSuffix(
		QStringLiteral(R"((?:\s+(?:[-–]\s*)?|\s*\()(Mono(?:\s*(?:to|→|[-/])\s*Stereo)?|Stereo|Multi[- ]?Out)\)?$)"),
		QRegularExpression::CaseInsensitiveOption);
	const auto moduleName = QFileInfo(entry.locator.modulePath).completeBaseName();
	const auto match = layoutSuffix.match(entry.name);
	const auto base = match.hasMatch() ? entry.name.left(match.capturedStart()).trimmed() : entry.name.trimmed();
	std::array<int, 2> channels{};
	for (const auto& bus : metadata.buses)
	{
		if (bus.media == 0 && bus.type == 0 && bus.active)
		{
			channels[bus.direction] += static_cast<int>(bus.channels);
		}
	}
	if (channels[1] > 0)
	{
		const auto declared = match.captured(1).toCaseFolded();
		const bool matchesLayout = (declared == "mono" && channels[0] <= 1 && channels[1] == 1)
			|| (declared == "stereo" && channels[0] != 1 && channels[1] == 2)
			|| (declared.startsWith("mono") && declared.endsWith("stereo") && channels[0] == 1 && channels[1] == 2)
			|| (declared.startsWith("multi") && channels[1] > 2);
		if (!base.isEmpty() && (matchesLayout
			|| (!match.hasMatch() && base.compare(moduleName, Qt::CaseInsensitive) == 0)))
		{
			entry.product = base;
		}
		entry.layoutInputs = channels[0];
		entry.layoutOutputs = channels[1];
	}
}

void probeVst3Layouts(CatalogEntry& entry, const HostSession::Configuration& configuration,
	Vst3Create create, std::stop_token stop)
{
	if (entry.layoutInputs < 0 || entry.layoutOutputs <= 0 || entry.supportedMainLayouts != 0)
	{
		return;
	}
	for (const auto layout : {Vst3MainLayout::Mono, Vst3MainLayout::Stereo, Vst3MainLayout::MonoToStereo})
	{
		if (stop.stop_requested())
		{
			return;
		}
		const auto input = entry.layoutInputs == 0 ? 0 : layout == Vst3MainLayout::Stereo ? 2 : 1;
		const auto output = layout == Vst3MainLayout::Mono ? 1 : 2;
		if ((input == entry.layoutInputs && output == entry.layoutOutputs)
			|| (input == 0 && layout == Vst3MainLayout::MonoToStereo))
		{
			continue;
		}
		create.layout = layout;
		auto probe = std::make_unique<Vst3HostProxy>();
		if (probe->open(configuration, create))
		{
			entry.supportedMainLayouts |= 1u << static_cast<unsigned>(layout);
		}
	}
	if (entry.supportedMainLayouts && entry.product.isEmpty())
	{
		entry.product = entry.name;
	}
}

void exportVst3Presets(CatalogEntry& entry, const QString& helper, const PluginCatalog::Options& options,
	std::stop_token stop, CatalogReport& report, bool cached, const auto& beginExport)
{
	const auto directory = nativeVstPresetDirectory(options.presetDirectory, entry.identity.format,
		isVstInstrument(entry), entry.name, entry.locator.modulePath);
	const auto classId = QString::fromLatin1(QByteArray(
		reinterpret_cast<const char*>(entry.identity.cid.data()), 16).toHex());
	const auto destination = QDir(directory).filePath(QStringLiteral("Default-%1.xpf").arg(classId));
	Vst3Create create;
	create.path = entry.locator.modulePath.toUtf8().toStdString();
	create.cid = entry.identity.cid;
	const HostSession::Configuration configuration{
		helper.toStdWString(), {}, options.timeoutMs, 0, entry.locator.modulePath.toStdWString()};
	auto host = std::make_unique<Vst3HostProxy>();
	if (!host->open(configuration, create))
	{
		++report.presetFailures;
		return;
	}
	if (!cached)
	{
		recordVst3Layout(entry, host->metadata());
		probeVst3Layouts(entry, configuration, create, stop);
		beginExport();
	}
	const auto snapshot = host->state();
	if (snapshot.error != Error::None || !validVst3State(snapshot.payload))
	{
		++report.presetFailures;
		return;
	}
	QDomDocument document;
	auto state = document.createElement("state");
	const auto key = vstCatalogKey(nullptr, entry);
	for (auto attribute = key.attributes.cbegin(); attribute != key.attributes.cend(); ++attribute)
	{
		state.setAttribute(attribute.key(), attribute.value());
	}
	state.setAttribute("modulepath", entry.locator.modulePath);
	state.setAttribute("pluginname", entry.name);
	const auto write = [&](const QString& path, std::span<const std::uint8_t> bytes) {
		state.setAttribute("vst3state", QString::fromLatin1(QByteArray(
			reinterpret_cast<const char*>(bytes.data()), static_cast<qsizetype>(bytes.size())).toBase64()));
		recordPresetResult(report, writeScannedVstPreset(path, entry, state));
	};
	write(destination, snapshot.payload);
	for (const auto& program : host->programs())
	{
		if (stop.stop_requested())
		{
			break;
		}
		if (!program.selectable())
		{
			continue;
		}
		const auto name = nativeVstPresetPathComponent(QString::fromUtf8(program.name), QStringLiteral("Program"));
		const auto path = QDir(directory).filePath(QStringLiteral("%1-%2-%3-%4.xpf")
			.arg(name, classId).arg(program.listId).arg(program.index + 1));
		if (QFileInfo::exists(path))
		{
			++report.presetsExisting;
			continue;
		}
		if (host->restoreState(snapshot.payload) != Error::None
			|| host->setParameter(program.parameterId, static_cast<double>(program.index) / program.steps).error
				!= Error::None)
		{
			++report.presetFailures;
			break;
		}
		const auto selected = host->state();
		if (selected.error != Error::None || !validVst3State(selected.payload))
		{
			++report.presetFailures;
			break;
		}
		write(path, selected.payload);
	}
}

void exportVst2Presets(const CatalogEntry& entry, const QString& helper,
	const PluginCatalog::Options& options, std::stop_token stop, CatalogReport& report)
{
	const auto directory = nativeVstPresetDirectory(options.presetDirectory, entry.identity.format,
		isVstInstrument(entry), entry.name, entry.locator.modulePath);
	const auto destination = QDir(directory).filePath(
		QStringLiteral("Default-%1.xpf").arg(entry.identity.vst2Id, 8, 16, QLatin1Char('0')));

	HostSession session;
	std::stop_callback cancel(stop, [&session] { session.cancel(); });
	if (session.open({helper.toStdWString(), {L"headless"}, options.timeoutMs, 0,
			entry.locator.modulePath.toStdWString()}).get().error != Error::None)
	{
		++report.presetFailures;
		return;
	}
	const auto request = [&](std::span<const LegacyCommand> commands, std::vector<LegacyCommand>& response) {
		std::vector<std::uint8_t> bytes;
		if (encodeLegacy(commands, bytes) != Error::None)
		{
			return false;
		}
		const auto reply = session.request(MessageType::Parameter, std::move(bytes), options.timeoutMs).get();
		return reply.error == Error::None && decodeLegacy(reply.payload, response) == Error::None;
	};
	std::array<LegacyCommand, 3> start{{
		{IdSampleRateInformation, {"44100"}},
		{IdBufferSizeInformation, {"512"}},
		{IdVstLoadPlugin, {entry.locator.modulePath.toUtf8().toStdString()}}
	}};
	if (entry.shell)
	{
		start.back().arguments.push_back(std::to_string(entry.identity.vst2Id));
	}
	std::vector<LegacyCommand> response;
	if (!request(start, response))
	{
		++report.presetFailures;
		return;
	}

	const auto capture = [&](const QString& path, int program) {
		QDomDocument document;
		auto state = document.createElement("state");
		QTemporaryFile chunk;
		if (chunk.open())
		{
			chunk.close();
			const auto chunkPath = QDir::toNativeSeparators(chunk.fileName()).toUtf8().toStdString();
			const std::array<LegacyCommand, 1> save{{{IdSaveSettingsToFile, {chunkPath}}}};
			if (!request(save, response))
			{
				return PresetWriteResult::Failed;
			}
			QFile file(chunk.fileName());
			if (file.open(QIODevice::ReadOnly) && file.size() > 0 && file.size() <= MaxControlBytes)
			{
				const auto payload = file.readAll();
				if (payload.size() == file.size())
				{
					state.setAttribute("chunk", QString::fromLatin1(payload.toBase64()));
				}
			}
		}
		if (!state.hasAttribute("chunk"))
		{
			const std::array<LegacyCommand, 1> dump{{{IdVstGetParameterDump, {}}}};
			if (!request(dump, response))
			{
				return PresetWriteResult::Failed;
			}
			const auto found = std::find_if(response.begin(), response.end(),
				[](const auto& reply) { return reply.id == IdVstParameterDump; });
			if (found == response.end() || found->arguments.empty() || (found->arguments.size() - 1) % 3 != 0)
			{
				return PresetWriteResult::Failed;
			}
			const auto count = (found->arguments.size() - 1) / 3;
			if (found->arguments.front() != std::to_string(count))
			{
				return PresetWriteResult::Failed;
			}
			state.setAttribute("numparams", static_cast<qulonglong>(count));
			for (std::size_t index = 0; index < count; ++index)
			{
				const auto offset = 1 + index * 3;
				const auto key = QString::number(index);
				if (found->arguments[offset] != std::to_string(index))
				{
					return PresetWriteResult::Failed;
				}
				const auto value = QString::fromStdString(found->arguments[offset + 2]);
				state.setAttribute("param" + key, key + "::" + value);
			}
		}
		if (program >= 0)
		{
			state.setAttribute("program", program);
		}
		if (entry.shell)
		{
			state.setAttribute("shellid", QString::number(entry.identity.vst2Id));
		}
		return writeScannedVstPreset(path, entry, state);
	};
	if (QFileInfo::exists(destination))
	{
		++report.presetsExisting;
	}
	else
	{
		recordPresetResult(report, capture(destination, -1));
	}

	const std::array<LegacyCommand, 1> list{{{IdVstProgramNames, {}}}};
	if (!request(list, response))
	{
		++report.presetFailures;
		return;
	}
	const auto named = std::find_if(response.begin(), response.end(),
		[](const auto& reply) { return reply.id == IdVstProgramNames && reply.arguments.size() == 1; });
	if (named == response.end())
	{
		return;
	}
	const auto names = QString::fromStdString(named->arguments.front()).split('|');
	if (names.size() <= 1)
	{
		return;
	}
	for (qsizetype program = 0; program < names.size() && !stop.stop_requested(); ++program)
	{
		const auto name = nativeVstPresetPathComponent(names[program], QStringLiteral("Program"));
		const auto path = QDir(directory).filePath(QStringLiteral("%1-%2-%3.xpf")
			.arg(name).arg(entry.identity.vst2Id, 8, 16, QLatin1Char('0')).arg(program + 1));
		if (QFileInfo::exists(path))
		{
			++report.presetsExisting;
			continue;
		}
		const std::array<LegacyCommand, 1> select{{{IdVstSetProgram, {std::to_string(program)}}}};
		if (!request(select, response))
		{
			++report.presetFailures;
			return;
		}
		recordPresetResult(report, capture(path, static_cast<int>(program)));
	}
}

QJsonObject entryJson(const CatalogEntry& entry)
{
	const auto cid = QByteArray(reinterpret_cast<const char*>(entry.identity.cid.data()), 16).toHex();
	return {{"cid", QString::fromLatin1(cid)}, {"id", static_cast<double>(entry.identity.vst2Id)},
		{"source", entry.identity.vst2Source}, {"version", entry.locator.version}, {"name", entry.name},
		{"vendor", entry.vendor}, {"category", entry.category}, {"subcategories", entry.subcategories},
		{"flags", static_cast<double>(entry.flags)}, {"shell", entry.shell}, {"product", entry.product},
		{"layoutInputs", entry.layoutInputs}, {"layoutOutputs", entry.layoutOutputs},
		{"supportedMainLayouts", static_cast<int>(entry.supportedMainLayouts)}};
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

bool readDocument(const CatalogIo& io, const QString& path, const QString& version, QJsonArray& records,
	QString& error, const QJsonObject* expectedRoots = nullptr)
{
	const auto reply = io.request({{"op", "read-document"}, {"path", path}});
	if (reply.error != Error::None || !reply.object.value("exists").isBool())
	{
		error = "Supervised catalog cache read failed: " + path;
		return false;
	}
	if (!reply.object.value("exists").toBool())
	{
		return expectedRoots == nullptr;
	}
	const auto object = reply.object.value("document").toObject();
	if (reply.object.value("invalid").toBool(true) || !reply.object.value("document").isObject()
		|| object.value("schema").toDouble() != 1 || object.value("host").toString() != version
		|| !object.value("records").isArray() || object.value("records").toArray().size() > MaxModules)
	{
		error = "Catalog cache schema/host mismatch or invalid JSON: " + path;
		return false;
	}
	if (expectedRoots && object.contains("scanRoots") && object.value("scanRoots") != QJsonValue(*expectedRoots))
	{
		return false;
	}
	records = object.value("records").toArray();
	return true;
}

bool readCache(
	const CatalogIo& io, const QString& path, const QString& version, QMap<QString, Module>& cache, QString& error,
	const QJsonObject* expectedRoots = nullptr, std::vector<CatalogEntry>* indexEntries = nullptr)
{
	QJsonArray records;
	if (!readDocument(io, path, version, records, error, expectedRoots))
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
			entry.product = row.value("product").toString().left(63);
			entry.layoutInputs = std::clamp(row.value("layoutInputs").toInt(-1), -1,
				static_cast<int>(MaxAudioChannels));
			entry.layoutOutputs = std::clamp(row.value("layoutOutputs").toInt(-1), -1,
				static_cast<int>(MaxAudioChannels));
			entry.supportedMainLayouts = static_cast<std::uint32_t>(row.value("supportedMainLayouts").toInt()) & 0x0e;
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
		if (indexEntries)
		{
			indexEntries->insert(indexEntries->end(), module.entries.begin(), module.entries.end());
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
	QByteArray encodedRoots;
	QString rootsError;
	const bool validRoots = encodeScanRoots(roots, encodedRoots, rootsError);
	const auto indexRoots = QJsonDocument::fromJson(encodedRoots).object();
	if (options.preferIndex && !options.force && validRoots && !m_configuration.cacheFile.isEmpty())
	{
		QMap<QString, Module> index;
		QString error;
		std::vector<CatalogEntry> entries;
		if (readCache(io, m_configuration.cacheFile, m_configuration.hostVersion, index, error, &indexRoots, &entries))
		{
			report.modules = static_cast<std::size_t>(index.size());
			report.cachedModules = report.modules;
			report.entries = std::move(entries);
			const auto failures = readFailures(io, m_configuration.cacheFile + ".failures.json",
				m_configuration.hostVersion);
			for (const auto& failure : failures)
			{
				report.failures.push_back(failure);
			}
			report.cancelled = stop.stop_requested();
			if (report.cancelled)
			{
				report.entries.clear();
			}
			return report;
		}
	}
	if (progress)
	{
		progress({});
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
	if (!m_configuration.cacheFile.isEmpty() && options.skipVerified.value_or(!options.force))
	{
		if (!readCache(io, m_configuration.cacheFile, m_configuration.hostVersion, cache, report.cacheError))
		{
			cache.clear();
			if (report.cacheError.isEmpty())
			{
				report.cacheError = "Invalid catalog cache records";
			}
		}
	}
	if (!m_configuration.cacheFile.isEmpty() && !options.force)
	{
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
			progress({module.path, processed, discovered.size(), {}});
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
		const auto cachedModule = cache.constFind(key);
		const bool cached = cachedModule != cache.cend() && cachedModule->fingerprint == module.fingerprint
			&& cachedModule->architecture == module.architecture && cachedModule->path == module.path;
		if (cached)
		{
			module.entries = cachedModule->entries;
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
				std::function<void(std::string_view, std::uint32_t, std::uint32_t)> shellProgress;
				if (progress)
				{
					shellProgress = [&](std::string_view name, std::uint32_t completed, std::uint32_t total) {
						progress({module.path, processed - 1, discovered.size(),
							QString::fromUtf8(name.data(), static_cast<qsizetype>(name.size())), completed, total});
					};
				}
				const auto scan = scanVst2({helper.toStdWString(), {L"headless"}, options.timeoutMs},
					module.binary.toUtf8().toStdString(), options.timeoutMs, stop, shellProgress);
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
					entry.product = QString::fromUtf8(info.product);
					entry.layoutInputs = info.inputs;
					entry.layoutOutputs = info.outputs;
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
		if (!options.presetDirectory.isEmpty())
		{
			const bool x86 = module.architecture == Architecture::X86;
			const auto& helper = module.format == Format::Vst3
				? (x86 ? m_configuration.vst3Helper32 : m_configuration.vst3Helper64)
				: (x86 ? m_configuration.vst2Helper32 : m_configuration.vst2Helper64);
			for (std::size_t index = 0; index < module.entries.size(); ++index)
			{
				if (stop.stop_requested())
				{
					break;
				}
				auto& entry = module.entries[index];
				const auto publish = [&](ScanStage stage) {
					if (progress)
					{
						progress({module.path, processed - 1, discovered.size(), entry.name,
							index, module.entries.size(), stage});
					}
				};
				const auto beginExport = [&] { publish(ScanStage::ExportingPresets); };
				if (module.format == Format::Vst3)
				{
					publish(cached ? ScanStage::ExportingPresets : ScanStage::Scanning);
					exportVst3Presets(entry, helper, options, stop, report, cached, beginExport);
				}
				else
				{
					beginExport();
					exportVst2Presets(entry, helper, options, stop, report);
				}
			}
		}
		for (const auto& entry : module.entries)
		{
			report.entries.push_back(entry);
		}
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
		QJsonObject index{{"schema", 1}, {"host", m_configuration.hostVersion}, {"records", modules}};
		if (validRoots)
		{
			index.insert("scanRoots", indexRoots);
		}
		const auto published = io.request({{"op", "publish"}, {"path", m_configuration.cacheFile},
			{"success", index},
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
