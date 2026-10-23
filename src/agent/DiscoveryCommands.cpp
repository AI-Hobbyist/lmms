#include "DiscoveryCommands.h"

#include "agent/CommandBus.h"
#include "DiscoverySupport.h"

#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonObject>
#include <QStringList>

#include <algorithm>
#include <vector>

#include "ConfigManager.h"
#include "Engine.h"
#include "Plugin.h"
#include "PluginFactory.h"
#include "SVCCatalog.h"
#include "SVSModel.h"

#ifdef LMMS_BUILD_WIN32
#include "vsthost/CatalogJobs.h"
#endif

namespace lmms::agent {
namespace {

using discovery::Page;

CommandResult invalidArguments(const QString& message)
{
	return CommandResult::failure("invalid_arguments", message);
}

QJsonArray toJsonArray(const QStringList& values)
{
	QJsonArray result;
	for (const auto& value : values)
	{
		result.append(value);
	}
	return result;
}

bool readOptionalText(const QJsonObject& arguments, const char* name, QString& value, QString& error)
{
	value.clear();
	if (!arguments.contains(name))
	{
		return true;
	}
	const auto item = arguments.value(name);
	if (!item.isString())
	{
		error = QStringLiteral("'%1' must be a string.").arg(QString::fromUtf8(name));
		return false;
	}
	value = item.toString().trimmed();
	return true;
}

bool readOptionalBool(const QJsonObject& arguments, const char* name, bool& value, QString& error)
{
	if (!arguments.contains(name))
	{
		return true;
	}
	const auto item = arguments.value(name);
	if (!item.isBool())
	{
		error = QStringLiteral("'%1' must be a boolean.").arg(QString::fromUtf8(name));
		return false;
	}
	value = item.toBool();
	return true;
}

bool readSource(
	const QJsonObject& arguments, const QStringList& allowed, QString& source, QString& error)
{
	source = allowed.first();
	if (!arguments.contains("source"))
	{
		return true;
	}
	const auto item = arguments.value("source");
	if (!item.isString())
	{
		error = "'source' must be a string.";
		return false;
	}
	source = item.toString();
	if (!allowed.contains(source, Qt::CaseInsensitive))
	{
		error = QStringLiteral("'source' must be one of: %1.").arg(allowed.join(", "));
		return false;
	}
	return true;
}

struct Root
{
	QString label;
	QString path;
	int order = 0;
};

struct ScannedEntry
{
	QFileInfo info;
	QString relativePath;
	QString source;
	int sourceOrder = 0;
};

void scanDirectory(const QDir& directory, const QString& relative, int depth, const QStringList& extensions,
	const QString& keyword, int& visited, QVector<ScannedEntry>& entries, bool& truncated, const QString& source,
	int sourceOrder)
{
	if (truncated || depth > discovery::MaxScanDepth)
	{
		return;
	}
	const auto files = directory.entryInfoList(QDir::Files, QDir::Name | QDir::IgnoreCase);
	for (const auto& file : files)
	{
		if (++visited > discovery::MaxVisitedFiles)
		{
			truncated = true;
			return;
		}
		const auto extension = file.suffix().toLower();
		if (!extensions.contains(extension))
		{
			continue;
		}
		const auto relativePath = relative + file.fileName();
		if (!discovery::matchesKeyword(keyword, {relativePath, file.completeBaseName()}))
		{
			continue;
		}
		entries.append({file, relativePath, source, sourceOrder});
		if (entries.size() >= discovery::MaxScanEntries)
		{
			truncated = true;
			return;
		}
	}
	const auto directories
		= directory.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot | QDir::NoSymLinks, QDir::Name | QDir::IgnoreCase);
	for (const auto& subdirectory : directories)
	{
		scanDirectory(QDir(subdirectory.absoluteFilePath()), relative + subdirectory.fileName() + "/", depth + 1,
			extensions, keyword, visited, entries, truncated, source, sourceOrder);
		if (truncated)
		{
			return;
		}
	}
}

void sortScannedEntries(QVector<ScannedEntry>& entries)
{
	std::sort(entries.begin(), entries.end(), [](const ScannedEntry& left, const ScannedEntry& right) {
		if (left.sourceOrder != right.sourceOrder)
		{
			return left.sourceOrder < right.sourceOrder;
		}
		const int relative = QString::compare(left.relativePath, right.relativePath, Qt::CaseInsensitive);
		if (relative != 0)
		{
			return relative < 0;
		}
		return left.relativePath < right.relativePath;
	});
}

template <typename T>
QVector<T> pageSlice(const QVector<T>& values, const Page& page)
{
	const qint64 offset = static_cast<qint64>(page.page) * page.pageSize;
	if (offset >= values.size())
	{
		return {};
	}
	const int begin = static_cast<int>(offset);
	const int end = std::min(begin + page.pageSize, static_cast<int>(values.size()));
	return values.mid(begin, end - begin);
}

QString pluginSource(const QString& plugin)
{
	if (plugin == "vestige")
	{
		return "vst";
	}
	if (plugin == "lv2instrument")
	{
		return "lv2";
	}
	return "builtin";
}

QJsonObject vstCatalogSummary()
{
	QJsonObject result{{"available", false}, {"scanned", false}, {"busy", false}, {"entries", 0}, {"failures", 0},
		{"published", 0}};
#ifdef LMMS_BUILD_WIN32
	if (auto* jobs = Engine::vstCatalog())
	{
		const auto snapshot = jobs->snapshot();
		result.insert("available", true);
		result.insert("scanned", snapshot.report != nullptr);
		result.insert("busy", snapshot.busy);
		result.insert("published", static_cast<double>(snapshot.published));
		if (snapshot.report)
		{
			result.insert("entries", static_cast<int>(snapshot.report->entries.size()));
			result.insert("failures", static_cast<int>(snapshot.report->failures.size()));
		}
		else
		{
			result.insert("hint", "Scan VST plugins in LMMS before selecting a VST instrument.");
		}
	}
#endif
	return result;
}

CommandResult executeInstrumentListPlugins(const QJsonObject& arguments)
{
	Page page;
	QString keyword;
	QString error;
	QString source;
	if (!discovery::readPage(arguments, page, error) || !discovery::readKeyword(arguments, keyword, error)
		|| !readSource(arguments, {"all", "builtin", "vst", "lv2"}, source, error))
	{
		return invalidArguments(error);
	}
	bool includeSubKeys = true;
	if (!readOptionalBool(arguments, "includeSubKeys", includeSubKeys, error))
	{
		return invalidArguments(error);
	}

	auto* pluginFactory = PluginFactory::instance();
	if (pluginFactory == nullptr)
	{
		return CommandResult::failure("plugin_factory_unavailable", "The plugin factory is unavailable.");
	}

	QVector<QJsonObject> plugins;
	for (const auto* descriptor : pluginFactory->descriptors(Plugin::Type::Instrument))
	{
		if (descriptor == nullptr)
		{
			continue;
		}
		const auto plugin = QString::fromUtf8(descriptor->name);
		const auto entrySource = pluginSource(plugin);
		if (source != "all" && source.compare(entrySource, Qt::CaseInsensitive) != 0)
		{
			continue;
		}
		const auto name = QString::fromUtf8(descriptor->displayName ? descriptor->displayName : descriptor->name);
		const auto description
			= QString::fromUtf8(descriptor->description ? descriptor->description : "");
		const auto author = QString::fromUtf8(descriptor->author ? descriptor->author : "");
		QStringList supportedFileTypes;
		if (descriptor->supportedFileTypes)
		{
			for (const auto& item : QString::fromUtf8(descriptor->supportedFileTypes).split(',', Qt::SkipEmptyParts))
			{
				supportedFileTypes.append(item.trimmed().toLower());
			}
		}

		QJsonArray subKeys;
		QStringList search{plugin, name, description, author};
		const bool hasSubPlugins = descriptor->subPluginFeatures != nullptr;
		if (hasSubPlugins)
		{
			Plugin::Descriptor::SubPluginFeatures::KeyList keys;
			descriptor->subPluginFeatures->listSubPluginKeys(descriptor, keys);
			for (const auto& key : keys)
			{
				QJsonObject attributes;
				for (auto it = key.attributes.cbegin(); it != key.attributes.cend(); ++it)
				{
					attributes.insert(it.key(), it.value());
					search.append(it.value());
				}
				if (includeSubKeys)
				{
					subKeys.append(QJsonObject{{"name", key.name}, {"attributes", attributes}});
				}
				search.append(key.name);
			}
		}
		if (!discovery::matchesKeyword(keyword, search))
		{
			continue;
		}

		QJsonObject entry{{"plugin", plugin}, {"name", name}, {"description", description}, {"author", author},
			{"version", descriptor->version}, {"supportedFileTypes", toJsonArray(supportedFileTypes)},
			{"source", entrySource}, {"hasSubPlugins", hasSubPlugins}, {"subKeys", subKeys}};
		plugins.append(entry);
	}

	std::sort(plugins.begin(), plugins.end(), [](const QJsonObject& left, const QJsonObject& right) {
		const int byName = QString::compare(left.value("name").toString(), right.value("name").toString(),
			Qt::CaseInsensitive);
		if (byName != 0)
		{
			return byName < 0;
		}
		return left.value("plugin").toString() < right.value("plugin").toString();
	});

	const auto pageItems = pageSlice(plugins, page);
	QJsonArray items;
	for (const auto& plugin : pageItems)
	{
		items.append(plugin);
	}
	auto result = discovery::pageFields(plugins.size(), page, items.size());
	result.insert("plugins", items);
	result.insert("vstCatalog", vstCatalogSummary());
	return CommandResult::success(result);
}

CommandResult executePresetList(const QJsonObject& arguments)
{
	Page page;
	QString keyword;
	QString source;
	QStringList extensions;
	QString error;
	if (!discovery::readPage(arguments, page, error) || !discovery::readKeyword(arguments, keyword, error)
		|| !readSource(arguments, {"all", "user", "factory"}, source, error)
		|| !discovery::readExtensions(arguments, {"xpf", "xml", "xiz", "lv2"}, extensions, error))
	{
		return invalidArguments(error);
	}

	auto* config = ConfigManager::inst();
	QVector<Root> roots;
	if (source.compare("factory", Qt::CaseInsensitive) != 0)
	{
		roots.append({"user", config->userPresetsDir(), 0});
	}
	if (source.compare("user", Qt::CaseInsensitive) != 0)
	{
		roots.append({"factory", config->factoryPresetsDir(), 1});
	}

	QVector<ScannedEntry> entries;
	bool truncated = false;
	QJsonArray rootDetails;
	for (const auto& root : roots)
	{
		const QFileInfo info(root.path);
		rootDetails.append(QJsonObject{{"source", root.label}, {"path", info.absoluteFilePath()},
			{"exists", info.isDir()}});
		if (!info.isDir())
		{
			continue;
		}
		int visited = 0;
		scanDirectory(QDir(info.absoluteFilePath()), "", 0, extensions, keyword, visited, entries, truncated,
			root.label, root.order);
	}
	sortScannedEntries(entries);

	const auto pageItems = pageSlice(entries, page);
	QJsonArray presets;
	for (const auto& entry : pageItems)
	{
		const auto extension = entry.info.suffix().toLower();
		const bool loadableByHost = extension == "xpf" || extension == "xml";
		const auto firstSeparator = entry.relativePath.indexOf('/');
		const auto pluginHint = firstSeparator < 0 ? QString{} : entry.relativePath.left(firstSeparator);
		presets.append(QJsonObject{{"name", entry.info.completeBaseName()},
			{"path", entry.info.absoluteFilePath()}, {"relativePath", entry.relativePath},
			{"source", entry.source}, {"extension", extension},
			{"size", static_cast<double>(entry.info.size())},
			{"modified", discovery::isoTime(entry.info.lastModified())}, {"pluginHint", pluginHint},
			{"loadableBy", QJsonArray{loadableByHost ? "instrument.loadPreset" : "pluginPreset"}}});
	}

	auto result = discovery::pageFields(entries.size(), page, presets.size());
	result.insert("truncated", truncated);
	result.insert("roots", rootDetails);
	result.insert("presets", presets);
	return CommandResult::success(result);
}

CommandResult executeSampleList(const QJsonObject& arguments)
{
	Page page;
	QString keyword;
	QString source;
	QStringList extensions;
	QString error;
	QStringList defaults{"wav", "ogg", "flac", "ds", "spx", "voc", "aif", "aiff", "au", "raw"};
#ifdef LMMS_HAVE_SNDFILE_MP3
	defaults.append("mp3");
#endif
	if (!discovery::readPage(arguments, page, error) || !discovery::readKeyword(arguments, keyword, error)
		|| !readSource(arguments, {"all", "user", "factory"}, source, error)
		|| !discovery::readExtensions(arguments, defaults, extensions, error))
	{
		return invalidArguments(error);
	}

	auto* config = ConfigManager::inst();
	QVector<Root> roots;
	if (source.compare("factory", Qt::CaseInsensitive) != 0)
	{
		roots.append({"user", config->userSamplesDir(), 0});
	}
	if (source.compare("user", Qt::CaseInsensitive) != 0)
	{
		roots.append({"factory", config->factorySamplesDir(), 1});
	}

	QVector<ScannedEntry> entries;
	bool truncated = false;
	QJsonArray rootDetails;
	for (const auto& root : roots)
	{
		const QFileInfo info(root.path);
		rootDetails.append(QJsonObject{{"source", root.label}, {"path", info.absoluteFilePath()},
			{"exists", info.isDir()}});
		if (!info.isDir())
		{
			continue;
		}
		int visited = 0;
		scanDirectory(QDir(info.absoluteFilePath()), "", 0, extensions, keyword, visited, entries, truncated,
			root.label, root.order);
	}
	sortScannedEntries(entries);

	const auto pageItems = pageSlice(entries, page);
	QJsonArray samples;
	for (const auto& entry : pageItems)
	{
		samples.append(QJsonObject{{"name", entry.info.completeBaseName()},
			{"path", entry.info.absoluteFilePath()}, {"relativePath", entry.relativePath},
			{"source", entry.source}, {"extension", entry.info.suffix().toLower()},
			{"size", static_cast<double>(entry.info.size())},
			{"modified", discovery::isoTime(entry.info.lastModified())},
			{"loadableBy", QJsonArray{"sample.setFile", "import.sampleToTrack"}}});
	}

	auto result = discovery::pageFields(entries.size(), page, samples.size());
	result.insert("truncated", truncated);
	result.insert("roots", rootDetails);
	result.insert("samples", samples);
	return CommandResult::success(result);
}

CommandResult executeSvsListVoices(const QJsonObject& arguments)
{
	Page page;
	QString keyword;
	QString engine;
	QString language;
	QString error;
	bool includeMetadata = false;
	if (!discovery::readPage(arguments, page, error) || !discovery::readKeyword(arguments, keyword, error)
		|| !readOptionalText(arguments, "engine", engine, error)
		|| !readOptionalText(arguments, "language", language, error)
		|| !readOptionalBool(arguments, "includeMetadata", includeMetadata, error))
	{
		return invalidArguments(error);
	}

	auto& registry = svs::Registry::instance();
	const auto voices = registry.voices();
	const auto engines = registry.engines();

	QVector<QJsonObject> engineItems;
	QSet<QString> engineIds;
	for (const auto& item : engines)
	{
		if (!engine.isEmpty() && item.id != engine)
		{
			continue;
		}
		engineIds.insert(item.id);
		int voiceCount = 0;
		for (const auto& voice : voices)
		{
			if (voice.pluginId == item.id)
			{
				++voiceCount;
			}
		}
		engineItems.append(QJsonObject{{"id", item.id}, {"name", item.name}, {"type", item.type},
			{"package", item.package}, {"voiceCount", voiceCount}, {"scanning", registry.scanning(item.id)}});
	}

	QVector<QJsonObject> voiceItems;
	for (const auto& voice : voices)
	{
		if (!engine.isEmpty() && voice.pluginId != engine)
		{
			continue;
		}
		if (!language.isEmpty() && voice.language.compare(language, Qt::CaseInsensitive) != 0)
		{
			continue;
		}
		if (!discovery::matchesKeyword(keyword, {voice.name, voice.id, voice.pluginId}))
		{
			continue;
		}
		QJsonObject item{{"pluginId", voice.pluginId}, {"id", voice.id}, {"name", voice.name},
			{"version", voice.version}, {"language", voice.language}, {"defaultLyric", voice.defaultLyric},
			{"avatar", voice.avatar}, {"portrait", voice.portrait}, {"package", voice.package},
			{"selection", QJsonObject{{"pluginId", voice.pluginId}, {"voiceId", voice.id}}}};
		if (includeMetadata)
		{
			item.insert("metadata", voice.metadata);
		}
		voiceItems.append(item);
	}

	std::sort(engineItems.begin(), engineItems.end(), [](const QJsonObject& left, const QJsonObject& right) {
		const int byName = QString::compare(left.value("name").toString(), right.value("name").toString(),
			Qt::CaseInsensitive);
		return byName != 0 ? byName < 0 : left.value("id").toString() < right.value("id").toString();
	});
	std::sort(voiceItems.begin(), voiceItems.end(), [](const QJsonObject& left, const QJsonObject& right) {
		const int byPlugin = QString::compare(left.value("pluginId").toString(), right.value("pluginId").toString(),
			Qt::CaseInsensitive);
		if (byPlugin != 0)
		{
			return byPlugin < 0;
		}
		const int byName = QString::compare(left.value("name").toString(), right.value("name").toString(),
			Qt::CaseInsensitive);
		return byName != 0 ? byName < 0 : left.value("id").toString() < right.value("id").toString();
	});

	const auto pageItems = pageSlice(voiceItems, page);
	QJsonArray voicesJson;
	for (const auto& item : pageItems)
	{
		voicesJson.append(item);
	}
	QJsonArray enginesJson;
	for (const auto& item : engineItems)
	{
		enginesJson.append(item);
	}
	QJsonArray scanningEngines;
	for (const auto& item : engineItems)
	{
		if (item.value("scanning").toBool())
		{
			scanningEngines.append(item.value("id"));
		}
	}
	QJsonArray diagnostics;
	for (const auto& item : registry.diagnostics())
	{
		diagnostics.append(item);
	}

	auto result = discovery::pageFields(voiceItems.size(), page, voicesJson.size());
	result.insert("engines", enginesJson);
	result.insert("voices", voicesJson);
	result.insert("catalog", QJsonObject{{"scanning", registry.scanning()}, {"scanningEngines", scanningEngines}});
	result.insert("diagnostics", diagnostics);
	if (engine.isEmpty() && voices.isEmpty())
	{
		result.insert("hint", "No voicebanks found. Check the SVS voicebank directories and rescan in Settings.");
	}
	else if (registry.scanning())
	{
		result.insert("hint", "Voicebank scan in progress; retry after scanning is false.");
	}
	else if (voiceItems.isEmpty() && !keyword.isEmpty())
	{
		result.insert("hint", "No voices match the filter.");
	}
	else if (!engine.isEmpty() && engineIds.isEmpty())
	{
		result.insert("hint", "The requested SVS engine is not installed.");
	}
	return CommandResult::success(result);
}

CommandResult executeSvcListModels(const QJsonObject& arguments)
{
	Page page;
	QString keyword;
	QString engine;
	QString category;
	QString error;
	bool includeParameters = false;
	if (!discovery::readPage(arguments, page, error) || !discovery::readKeyword(arguments, keyword, error)
		|| !readOptionalText(arguments, "engine", engine, error)
		|| !readOptionalText(arguments, "category", category, error)
		|| !readOptionalBool(arguments, "includeParameters", includeParameters, error))
	{
		return invalidArguments(error);
	}

	auto& catalog = svc::Catalog::instance();
	auto engines = catalog.engines();
	std::sort(engines.begin(), engines.end(), [](const svc::EngineProfile& left, const svc::EngineProfile& right) {
		const int byName = QString::compare(left.name, right.name, Qt::CaseInsensitive);
		return byName != 0 ? byName < 0 : left.id < right.id;
	});

	struct ModelEntry
	{
		QJsonObject json;
		QString category;
		QString name;
		QString id;
		int engineOrder = 0;
	};

	QVector<QJsonObject> engineItems;
	QVector<ModelEntry> models;
	int engineOrder = 0;
	for (const auto& profile : engines)
	{
		if (!engine.isEmpty() && profile.id != engine)
		{
			continue;
		}
		const auto modelArray = profile.capabilities.value("models").toArray();
		engineItems.append(QJsonObject{{"id", profile.id}, {"name", profile.name},
			{"status", catalog.status(profile.id)}, {"connected", profile.api != nullptr},
			{"defaultAddress", profile.defaultAddress}, {"modelCount", modelArray.size()}});

		QVector<ModelEntry> engineModels;
		for (const auto& value : modelArray)
		{
			const auto model = value.toObject();
			const auto id = model.value("id").toString();
			if (id.isEmpty())
			{
				continue;
			}
			QStringList categories;
			for (const auto& item : model.value("category_path").toArray())
			{
				categories.append(item.toString());
			}
			if (!category.isEmpty())
			{
				bool matches = false;
				for (const auto& item : categories)
				{
					if (item.contains(category, Qt::CaseInsensitive))
					{
						matches = true;
						break;
					}
				}
				if (!matches)
				{
					continue;
				}
			}
			const auto name = model.value("name").toString(id);
			QStringList search{id, name, profile.name, categories.join('/')};
			for (const auto& item : model.value("weights").toArray())
			{
				const auto option = item.toObject();
				search.append(option.value("id").toString());
				search.append(option.value("name").toString());
			}
			for (const auto& item : model.value("speakers").toArray())
			{
				const auto option = item.toObject();
				search.append(option.value("id").toString());
				search.append(option.value("name").toString());
			}
			if (!discovery::matchesKeyword(keyword, search))
			{
				continue;
			}

			auto item = model;
			item.insert("engine_id", profile.id);
			item.insert("available", model.value("available").toBool(true));
			item.insert("reason", model.value("reason").toString());
			const auto weights = model.value("weights").toArray();
			const auto speakers = model.value("speakers").toArray();
			item.insert("weights", weights);
			item.insert("speakers", speakers);
			const bool multiSpeaker = model.value("multi_speaker").toBool(speakers.size() > 1);
			item.insert("multi_speaker", multiSpeaker);
			if (includeParameters)
			{
				item.insert("parameters", svc::parameterDefinitions(profile, model));
			}
			else
			{
				item.remove("parameters");
			}

			QJsonObject selection{{"engine_id", profile.id}, {"model_id", id}};
			if (weights.size() == 1 && !model.contains("parameters"))
			{
				const auto weightId = weights.first().toObject().value("id");
				if (weightId.isString())
				{
					selection.insert("weight_id", weightId);
				}
			}
			if (!multiSpeaker && !speakers.isEmpty())
			{
				const auto speakerId = speakers.first().toObject().value("id");
				if (speakerId.isString())
				{
					selection.insert("speaker_id", speakerId);
				}
			}
			item.insert("selection", selection);
			engineModels.append({item, categories.join('/'), name, id, engineOrder});
		}
		std::sort(engineModels.begin(), engineModels.end(), [](const ModelEntry& left, const ModelEntry& right) {
			const int byCategory = QString::compare(left.category, right.category, Qt::CaseInsensitive);
			if (byCategory != 0)
			{
				return byCategory < 0;
			}
			const int byName = QString::compare(left.name, right.name, Qt::CaseInsensitive);
			return byName != 0 ? byName < 0 : left.id < right.id;
		});
		for (const auto& item : engineModels)
		{
			models.append(item);
		}
		++engineOrder;
	}

	const auto pageItems = pageSlice(models, page);
	QJsonArray modelsJson;
	for (const auto& item : pageItems)
	{
		modelsJson.append(item.json);
	}
	QJsonArray enginesJson;
	for (const auto& item : engineItems)
	{
		enginesJson.append(item);
	}

	auto result = discovery::pageFields(models.size(), page, modelsJson.size());
	result.insert("engines", enginesJson);
	result.insert("models", modelsJson);
	result.insert("diagnostics", QJsonArray{});
	if (models.isEmpty())
	{
		result.insert("hint", "No SVC models discovered. Check the engine connection and retry after discovery.");
	}
	return CommandResult::success(result);
}

CommandResult executeFsListRoots(const QJsonObject&)
{
	QJsonArray roots;
	const auto addRoot = [&roots](const QString& name, const QString& path, const QString& type) {
		roots.append(QJsonObject{{"name", name}, {"path", path}, {"type", type}});
	};
	for (const auto& drive : QDir::drives())
	{
		auto path = drive.absolutePath();
		auto name = path;
		if (name.endsWith('/') && name.size() > 1)
		{
			name.chop(1);
		}
		addRoot(name, path, "drive");
	}
#ifdef Q_OS_MACOS
	if (QDir(QStringLiteral("/Volumes")).exists())
	{
		addRoot("Volumes", "/Volumes", "volume");
	}
#endif
	addRoot("Home", QDir::homePath(), "home");

	QJsonArray knownBases;
	auto* config = ConfigManager::inst();
	const auto addBase = [&knownBases](const QString& name, const QString& path, const QString& type) {
		if (!path.isEmpty())
		{
			knownBases.append(QJsonObject{{"name", name}, {"path", path}, {"type", type}});
		}
	};
	addBase("Working directory", config->workingDir(), "working");
	const auto dataPaths = QDir::searchPaths("data");
	if (!dataPaths.isEmpty())
	{
		addBase("Data directory", dataPaths.first(), "data");
	}
	addBase("My Projects", config->userProjectsDir(), "projects");
	addBase("My Samples", config->userSamplesDir(), "samples");
	addBase("My Presets", config->userPresetsDir(), "presets");

	return CommandResult::success(QJsonObject{{"roots", roots}, {"knownBases", knownBases}});
}

CommandResult executeFsListDirectory(const QJsonObject& arguments)
{
	Page page;
	QString error;
	if (!discovery::readPage(arguments, page, error))
	{
		return invalidArguments(error);
	}
	if (!arguments.contains("path") || !arguments.value("path").isString())
	{
		return invalidArguments("'path' is required and must be a string.");
	}
	const auto requested = arguments.value("path").toString();
	if (requested.isEmpty())
	{
		return invalidArguments("'path' is required.");
	}
	const QFileInfo info(requested);
	if (!info.isAbsolute())
	{
		return invalidArguments("'path' must be an absolute directory path.");
	}
	if (!info.exists())
	{
		return CommandResult::failure("directory_not_found", "The requested directory does not exist.");
	}
	if (!info.isDir())
	{
		return CommandResult::failure("not_a_directory", "The requested path is not a directory.");
	}

	bool showHidden = false;
	bool directoriesOnly = false;
	QStringList extensions;
	if (!readOptionalBool(arguments, "showHidden", showHidden, error)
		|| !readOptionalBool(arguments, "directoriesOnly", directoriesOnly, error)
		|| !discovery::readExtensions(arguments, {}, extensions, error))
	{
		return invalidArguments(error);
	}

	const QDir directory(info.absoluteFilePath());
	if (!directory.isReadable())
	{
		return CommandResult::failure("access_denied", "The requested directory is not readable.");
	}
	QDir::Filters filters = QDir::AllEntries | QDir::NoDotAndDotDot;
	if (showHidden)
	{
		filters |= QDir::Hidden;
	}
	auto items = directory.entryInfoList(filters, QDir::Name | QDir::IgnoreCase);

	QVector<QFileInfo> entries;
	bool truncated = false;
	for (const auto& entry : items)
	{
		if (entries.size() >= discovery::MaxScanEntries)
		{
			truncated = true;
			break;
		}
		if (directoriesOnly && !entry.isDir())
		{
			continue;
		}
		if (!entry.isDir() && !extensions.isEmpty()
			&& !extensions.contains(entry.suffix().toLower()))
		{
			continue;
		}
		entries.append(entry);
	}

	std::sort(entries.begin(), entries.end(), [](const QFileInfo& left, const QFileInfo& right) {
		if (left.isDir() != right.isDir())
		{
			return left.isDir();
		}
		const int byName = QString::compare(left.fileName(), right.fileName(), Qt::CaseInsensitive);
		return byName != 0 ? byName < 0 : left.fileName() < right.fileName();
	});

	const auto pageItems = pageSlice(entries, page);
	QJsonArray jsonEntries;
	for (const auto& entry : pageItems)
	{
		jsonEntries.append(discovery::fileEntry(entry, entry.isDir() ? "directory" : "file"));
	}

	auto result = discovery::pageFields(entries.size(), page, jsonEntries.size());
	result.insert("path", info.absoluteFilePath());
	result.insert("parent", QFileInfo(info.absolutePath()).absolutePath());
	result.insert("exists", true);
	result.insert("truncated", truncated);
	result.insert("entries", jsonEntries);
	return CommandResult::success(result);
}

} // namespace

void registerDiscoveryCommands(CommandBus& commandBus)
{
	const auto add = [&commandBus](const QString& name, const QString& summary, const QJsonObject& schema,
						  CommandHandler handler) {
		CommandDescriptor descriptor;
		descriptor.name = name;
		descriptor.summary = summary;
		descriptor.argsSchema = schema;
		descriptor.mutability = Mutability::ReadOnly;
		descriptor.scope = TxScope::None;
		descriptor.handler = std::move(handler);
		commandBus.registerCommand(descriptor);
	};

	QJsonObject instrumentProperties = discovery::pageProperties();
	instrumentProperties.insert("keyword", discovery::stringSchema());
	instrumentProperties.insert("source",
		QJsonObject{{"type", "string"}, {"enum", QJsonArray{"all", "builtin", "vst", "lv2"}}});
	instrumentProperties.insert("includeSubKeys", discovery::booleanSchema());
	add("instrument.listPlugins",
		"List instrument plugins and sub-plugin keys for instrument.load; read-only and paginated.",
		discovery::objectSchema(instrumentProperties), executeInstrumentListPlugins);

	QJsonObject presetProperties = discovery::pageProperties();
	presetProperties.insert("keyword", discovery::stringSchema());
	presetProperties.insert("source",
		QJsonObject{{"type", "string"}, {"enum", QJsonArray{"all", "user", "factory"}}});
	presetProperties.insert("extensions", discovery::stringArraySchema());
	add("preset.list",
		"List user and factory presets with absolute paths for instrument.loadPreset; read-only.",
		discovery::objectSchema(presetProperties), executePresetList);

	QJsonObject sampleProperties = discovery::pageProperties();
	sampleProperties.insert("keyword", discovery::stringSchema());
	sampleProperties.insert("source",
		QJsonObject{{"type", "string"}, {"enum", QJsonArray{"all", "user", "factory"}}});
	sampleProperties.insert("extensions", discovery::stringArraySchema());
	add("sample.list",
		"List user and factory audio samples with absolute paths for sample.setFile; read-only.",
		discovery::objectSchema(sampleProperties), executeSampleList);

	QJsonObject voiceProperties = discovery::pageProperties();
	voiceProperties.insert("engine", discovery::stringSchema());
	voiceProperties.insert("keyword", discovery::stringSchema());
	voiceProperties.insert("language", discovery::stringSchema());
	voiceProperties.insert("includeMetadata", discovery::booleanSchema());
	add("svs.listVoices",
		"List SVS engines and voicebanks; scanning is asynchronous, retry while scanning is true.",
		discovery::objectSchema(voiceProperties), executeSvsListVoices);

	QJsonObject modelProperties = discovery::pageProperties();
	modelProperties.insert("engine", discovery::stringSchema());
	modelProperties.insert("keyword", discovery::stringSchema());
	modelProperties.insert("category", discovery::stringSchema());
	modelProperties.insert("includeParameters", discovery::booleanSchema());
	add("svc.listModels",
		"List SVC engines, models, weights and speakers for selection; read-only.",
		discovery::objectSchema(modelProperties), executeSvcListModels);

	add("fs.listRoots", "List drive roots, home and known LMMS directories; read-only.",
		discovery::objectSchema(), executeFsListRoots);

	QJsonObject directoryProperties = discovery::pageProperties();
	directoryProperties.insert("path", discovery::stringSchema());
	directoryProperties.insert("showHidden", discovery::booleanSchema());
	directoryProperties.insert("directoriesOnly", discovery::booleanSchema());
	directoryProperties.insert("extensions", discovery::stringArraySchema());
	add("fs.listDirectory",
		"List direct children of an explicit absolute directory path; read-only, non-recursive.",
		discovery::objectSchema(directoryProperties, QJsonArray{"path"}), executeFsListDirectory);
}

} // namespace lmms::agent
