#include "SVCCatalog.h"

#ifdef _WIN32
#include <windows.h>

#include <wincred.h>
#endif

#include <QJsonArray>
#include <QJsonDocument>
#include <QLibrary>
#include <QRegularExpression>
#include <QTimer>
#include <QUrl>
#include <algorithm>
#include <cmath>

#include "ConfigManager.h"
#include "PluginFactory.h"

namespace lmms::svc {
ChunkConfig chunkDefaults()
{
	const auto* config = ConfigManager::inst();
	ChunkConfig defaults{config->value("svcDefaults", "silence", "-70").toDouble(),
		config->value("svcDefaults", "threshold", "30").toDouble(),
		config->value("svcDefaults", "forced", "10").toDouble()};
	return defaults.validate().isEmpty() ? defaults : ChunkConfig{};
}
QString setChunkDefaults(const ChunkConfig& config)
{
	const auto error = config.validate();
	if (!error.isEmpty()) { return error; }
	auto* settings = ConfigManager::inst();
	settings->setValue("svcDefaults", "silence", QString::number(config.silenceThresholdDbfs, 'g', 15));
	settings->setValue("svcDefaults", "threshold", QString::number(config.lengthThresholdSeconds, 'g', 15));
	settings->setValue("svcDefaults", "forced", QString::number(config.forcedChunkSeconds, 'g', 15));
	return {};
}

bool conditionsMatch(const QJsonObject& conditions, const QJsonObject& values)
{
	for (auto it = conditions.begin(); it != conditions.end(); ++it)
	{
		if (it.value().isObject() && it.value().toObject().contains("not"))
		{
			const auto excluded = it.value().toObject().value("not");
			const auto actual = values.value(it.key());
			if (actual.isUndefined() || (excluded.isArray() ? excluded.toArray().contains(actual) : actual == excluded))
			{
				return false;
			}
			continue;
		}
		if (it.value().isArray() ? !it.value().toArray().contains(values.value(it.key()))
								 : values.value(it.key()) != it.value())
		{
			return false;
		}
	}
	return true;
}

QJsonObject selectionContext(const QJsonObject& selection, const QJsonObject& model)
{
	auto context = model;
	for (const auto& entry : model.value("weights").toArray())
	{
		const auto weight = entry.toObject();
		if (weight.value("id") != selection.value("weight_id")) { continue; }
		for (auto it = weight.begin(); it != weight.end(); ++it)
		{
			context.insert(it.key(), it.value());
		}
	}
	for (auto it = selection.begin(); it != selection.end(); ++it)
	{
		context.insert(it.key(), it.value());
	}
	const auto parameters = selection.value("parameters").toObject();
	for (auto it = parameters.begin(); it != parameters.end(); ++it)
	{
		context.insert(it.key(), it.value());
	}
	return context;
}

QJsonArray parameterDefinitions(const EngineProfile& profile, const QJsonObject& model)
{
	return model.contains("parameters") ? model.value("parameters").toArray()
										: profile.capabilities.value("parameters").toArray();
}

Catalog::Catalog()
{
	const auto* api = svc_reference_engine(SVC_ABI_VERSION);
	EngineProfile reference;
	reference.id = "reference";
	reference.name = tr("Reference identity");
	reference.defaultAddress = "builtin://reference";
	reference.capabilities = QJsonDocument::fromJson(api->capabilities(nullptr)).object();
	reference.api = api;
	reference.inputRate = 16000;
	reference.inputIsPcm = true;
	install(std::move(reference));
	for (const auto& info : getPluginFactory()->pluginInfos())
	{
		if (info.descriptor->type != Plugin::Type::SVC) { continue; }
		const auto entry = reinterpret_cast<svc_plugin_entry>(info.library->resolve("svc_plugin_entry_v1"));
		const auto* plugin = entry ? entry(SVC_ABI_VERSION) : nullptr;
		if (!plugin || plugin->size < sizeof(svc_plugin) || !plugin->engine || !plugin->create_context
			|| !plugin->destroy_context || !plugin->error || !plugin->engine->capabilities)
		{
			continue;
		}
		EngineProfile profile;
		profile.id = QString::fromUtf8(plugin->engine->engine_id);
		profile.name = QString::fromUtf8(plugin->name);
		profile.defaultAddress = QString::fromUtf8(plugin->default_address);
		profile.capabilities
			= {{"schema_version", 1}, {"engine_id", profile.id}, {"models", QJsonArray{}}, {"parameters", QJsonArray{}},
				{"limits", QJsonObject{{"min_seconds", 1}, {"max_seconds", 1}, {"max_upload_bytes", 1}}}};
		if (!install(profile).isEmpty()) { continue; }
		m_modules.insert(profile.id, {plugin, info.library});
		auto* timer = new QTimer(this);
		timer->setSingleShot(true);
		m_retryTimers.insert(profile.id, timer);
		connect(timer, &QTimer::timeout, this, [this, id = profile.id] {
			if (m_stopping || !m_reconnecting.contains(id) || m_connecting.contains(id)) { return; }
			if (++m_retryCounts[id] > reconnectPolicy().maximumRetries)
			{
				stopRetries(id);
				emit changed();
				return;
			}
			beginDiscovery(id);
		});
		m_status.insert(profile.id, tr("Not connected"));
		QTimer::singleShot(0, this, [this, id = profile.id] { refresh(id); });
	}
}

Catalog::~Catalog()
{ shutdown(); }
void Catalog::shutdown()
{
	for (auto* timer : m_retryTimers)
	{
		timer->stop();
	}
	m_reconnecting.clear();
	{
		std::lock_guard lock(m_mutex);
		m_stopping = true;
		m_discoveries.clear();
	}
	m_wake.notify_all();
	if (m_worker.joinable()) { m_worker.join(); }
}
void Catalog::refresh(const QString& id)
{
	stopRetries(id);
	beginDiscovery(id);
}

void Catalog::beginDiscovery(const QString& id)
{
	if (!m_modules.contains(id)) { return; }
	m_connecting.insert(id);
	const auto frozen = connection(id);
	const auto version = ++m_versions[id];
	auto offline = engine(id);
	offline.api = nullptr;
	offline.context.reset();
	install(std::move(offline));
	m_status.insert(id, tr("Discovering backend capabilities"));
	emit changed();
	std::lock_guard lock(m_mutex);
	if (m_stopping) { return; }
	m_discoveries.erase(
		std::remove_if(m_discoveries.begin(), m_discoveries.end(), [&](const auto& d) { return d.id == id; }),
		m_discoveries.end());
	m_discoveries.push_back({id, frozen, version, m_modules.value(id)});
	if (!m_worker.joinable())
	{
		m_worker = std::thread([this] { discover(); });
	}
	m_wake.notify_all();
}

ReconnectPolicy Catalog::reconnectPolicy() const
{
	ReconnectPolicy policy;
	const auto* config = ConfigManager::inst();
	const auto interval = config->value("svcReconnect", "intervalSeconds", "5").toInt();
	const auto retries = config->value("svcReconnect", "maximumRetries", "3").toInt();
	if (interval >= 1 && interval <= 86400) { policy.intervalSeconds = interval; }
	if (retries >= 1 && retries <= 1000) { policy.maximumRetries = retries; }
	return policy;
}

QString Catalog::setReconnectPolicy(const ReconnectPolicy& policy)
{
	if (policy.intervalSeconds < 1 || policy.intervalSeconds > 86400 || policy.maximumRetries < 1
		|| policy.maximumRetries > 1000)
	{
		return tr("Reconnect interval must be 1–86400 seconds; maximum retries must be 1–1000");
	}
	const auto previous = reconnectPolicy();
	if (previous.intervalSeconds == policy.intervalSeconds && previous.maximumRetries == policy.maximumRetries)
	{
		return {};
	}
	for (const auto& id : m_modules.keys())
	{
		stopRetries(id);
	}
	ConfigManager::inst()->setValue("svcReconnect", "intervalSeconds", QString::number(policy.intervalSeconds));
	ConfigManager::inst()->setValue("svcReconnect", "maximumRetries", QString::number(policy.maximumRetries));
	emit changed();
	return {};
}

void Catalog::stopRetries(const QString& id)
{
	if (auto* timer = m_retryTimers.value(id)) { timer->stop(); }
	m_reconnecting.remove(id);
	m_retryCounts.remove(id);
	if (!m_connecting.contains(id) && !m_connectionErrors.value(id).isEmpty())
	{
		m_status.insert(id, tr("Offline: %1").arg(m_connectionErrors.value(id)));
	}
}

void Catalog::reconnectDisconnected()
{
	if (m_stopping) { return; }
	for (const auto& id : m_modules.keys())
	{
		if (engine(id).api) { continue; }
		stopRetries(id);
		m_reconnecting.insert(id);
		m_retryCounts.insert(id, 0);
		if (!m_connecting.contains(id)) { m_retryTimers.value(id)->start(0); }
	}
}

void Catalog::discoveryFinished(const QString& id, const QString& error)
{
	m_connecting.remove(id);
	m_connectionErrors.insert(id, error);
	if (error.isEmpty())
	{
		stopRetries(id);
		m_status.insert(id, tr("Connected"));
		return;
	}
	m_status.insert(id, tr("Offline: %1").arg(error));
	if (!m_reconnecting.contains(id)) { return; }
	const auto policy = reconnectPolicy();
	const auto attempts = m_retryCounts.value(id);
	if (attempts >= policy.maximumRetries)
	{
		stopRetries(id);
		m_status.insert(id, tr("Offline: %1 — automatic retries exhausted (%2)").arg(error).arg(attempts));
		return;
	}
	m_status.insert(id,
		tr("Offline: %1 — retry %2/%3 in %4 s")
			.arg(error)
			.arg(attempts + 1)
			.arg(policy.maximumRetries)
			.arg(policy.intervalSeconds));
	m_retryTimers.value(id)->start(policy.intervalSeconds * 1000);
}
void Catalog::discover()
{
	for (;;)
	{
		Discovery item;
		{
			std::unique_lock lock(m_mutex);
			m_wake.wait(lock, [&] { return m_stopping || !m_discoveries.empty(); });
			if (m_stopping) { return; }
			item = m_discoveries.front();
			m_discoveries.erase(m_discoveries.begin());
		}
		const auto address = item.connection.address.toUtf8(), token = item.connection.token.toUtf8();
		const auto* api = item.module.api;
		std::shared_ptr<void> context(
			api->create_context(address.constData(), token.constData()), [api, library = item.module.library](void* p) {
				if (p) { api->destroy_context(p); }
			});
		const auto* json = context ? api->engine->capabilities(context.get()) : nullptr;
		const auto capabilities = json ? QJsonDocument::fromJson(json).object() : QJsonObject{};
		auto error = json ? QString{}
			: context	  ? QString::fromUtf8(api->error(context.get()))
						  : tr("Cannot create SVC engine context");
		if (!token.isEmpty()) { error.replace(QString::fromUtf8(token), "[redacted]"); }
		QMetaObject::invokeMethod(
			this,
			[this, item, context, capabilities, error, api] {
				if (m_stopping || m_versions.value(item.id) != item.version) { return; }
				QString result = error;
				if (result.isEmpty())
				{
					auto profile = engine(item.id);
					profile.api = api->engine;
					profile.context = context;
					profile.capabilities = capabilities;
					result = install(std::move(profile));
				}
				discoveryFinished(item.id, result);
				emit changed();
			},
			Qt::QueuedConnection);
	}
}

Catalog& Catalog::instance()
{
	static Catalog catalog;
	return catalog;
}

EngineProfile Catalog::engine(const QString& id) const
{
	const auto found
		= std::find_if(m_engines.begin(), m_engines.end(), [&](const auto& entry) { return entry.id == id; });
	return found == m_engines.end() ? EngineProfile{} : *found;
}

QString Catalog::install(EngineProfile profile)
{
	if (!QRegularExpression("^[A-Za-z0-9][A-Za-z0-9_-]{0,63}$").match(profile.id).hasMatch())
	{
		return tr("Invalid SVC engine ID");
	}
	const auto json = QJsonDocument(profile.capabilities).toJson(QJsonDocument::Compact);
	char error[512]{};
	if (profile.id != profile.capabilities.value("engine_id").toString()
		|| svc_validate_capabilities(json.constData(), json.size(), error, sizeof(error)) != SVC_OK)
	{
		return error[0] ? QString::fromUtf8(error) : tr("Engine capability ID mismatch");
	}
	if (profile.api
		&& (profile.api->size < sizeof(svc_engine) || profile.api->abi_version != SVC_ABI_VERSION || !profile.api->start
			|| !profile.api->pump || !profile.api->cancel || !profile.api->destroy))
	{
		return tr("Unsupported SVC engine ABI");
	}
	const auto found
		= std::find_if(m_engines.begin(), m_engines.end(), [&](const auto& entry) { return entry.id == profile.id; });
	if (found == m_engines.end()) { m_engines.push_back(std::move(profile)); }
	else
	{
		*found = std::move(profile);
	}
	emit changed();
	return {};
}

Connection Catalog::connection(const QString& id) const
{
	Connection connection;
	connection.address = ConfigManager::inst()->value("svcConnections", id + "_address", engine(id).defaultAddress);
	connection.token = m_sessionTokens.value(id);
#ifdef _WIN32
	const auto target = (QString("LMMS/SVC/") + id).toStdWString();
	PCREDENTIALW credential = nullptr;
	if (CredReadW(target.c_str(), CRED_TYPE_GENERIC, 0, &credential))
	{
		connection.remembered = true;
		if (!m_sessionTokens.contains(id))
		{
			connection.token = QString::fromUtf8(
				reinterpret_cast<const char*>(credential->CredentialBlob), credential->CredentialBlobSize);
		}
		CredFree(credential);
	}
#endif
	return connection;
}

QString Catalog::setConnection(const QString& id, const Connection& connection)
{
	if (engine(id).id.isEmpty()) { return tr("Unknown SVC engine"); }
	if (!engine(id).defaultAddress.startsWith("builtin:"))
	{
		const QUrl url(connection.address.trimmed());
		if (!url.isValid() || (url.scheme() != "http" && url.scheme() != "https") || url.host().isEmpty()
			|| !url.userInfo().isEmpty() || url.hasQuery() || url.hasFragment())
		{
			return tr("Use an HTTP(S) API address without embedded credentials, query or fragment");
		}
	}
	const auto previous = this->connection(id);
#ifdef _WIN32
	const auto target = (QString("LMMS/SVC/") + id).toStdWString();
	if (connection.remembered && !connection.token.isEmpty())
	{
		const auto token = connection.token.toUtf8();
		CREDENTIALW credential{};
		credential.Type = CRED_TYPE_GENERIC;
		credential.TargetName = const_cast<wchar_t*>(target.c_str());
		credential.CredentialBlobSize = token.size();
		credential.CredentialBlob = reinterpret_cast<LPBYTE>(const_cast<char*>(token.constData()));
		credential.Persist = CRED_PERSIST_LOCAL_MACHINE;
		if (!CredWriteW(&credential, 0)) { return tr("Windows Credential Manager could not save the token"); }
	}
	else
	{
		CredDeleteW(target.c_str(), CRED_TYPE_GENERIC, 0);
	}
#else
	if (connection.remembered) { return tr("Persistent SVC credentials require OS credential storage"); }
#endif
	m_sessionTokens.insert(id, connection.token);
	ConfigManager::inst()->setValue("svcConnections", id + "_address", connection.address.trimmed());
	if (previous.address != connection.address.trimmed() || previous.token != connection.token)
	{
		emit connectionChanged(id);
	}
	refresh(id);
	return {};
}

QJsonObject Catalog::requestSelection(const QJsonObject& saved, QString& error) const
{
	error.clear();
	const auto profile = engine(saved.value("engine_id").toString());
	QJsonObject model;
	for (const auto& entry : profile.capabilities.value("models").toArray())
	{
		if (entry.toObject().value("id") == saved.value("model_id")) { model = entry.toObject(); }
	}
	if (model.isEmpty() || !model.value("available").toBool(true) || !profile.api)
	{
		error = tr("Selected SVC model or engine is unavailable");
		return {};
	}
	auto selection = saved;
	if (model.contains("fixed_speaker_id")) { selection.insert("speaker_id", model.value("fixed_speaker_id")); }
	for (const auto& pair : {std::pair{"weight_id", "weights"}, std::pair{"speaker_id", "speakers"}})
	{
		const auto entries = selectionContext(selection, model).value(pair.second).toArray();
		if (entries.isEmpty())
		{
			selection.remove(pair.first);
			continue;
		}
		bool found = false;
		for (const auto& entry : entries)
		{
			const auto option = entry.toObject();
			found |= option.value("id") == selection.value(pair.first) && option.value("available").toBool(true);
		}
		if (!found)
		{
			error = tr("Selected SVC weight or speaker is unavailable");
			return {};
		}
	}
	QJsonObject parameters;
	auto context = selectionContext(selection, model);
	const auto definitions = parameterDefinitions(profile, model);
	for (const auto& entry : definitions)
	{
		const auto parameter = entry.toObject();
		const auto id = parameter.value("id").toString();
		if (context.contains(id)) { continue; }
		const auto dependent = context.value(parameter.value("default_from").toString());
		context.insert(id, dependent.isUndefined() ? parameter.value("default") : dependent);
	}
	for (const auto& entry : definitions)
	{
		const auto parameter = entry.toObject();
		if (!conditionsMatch(parameter.value("visible_when").toObject(), context)
			|| !conditionsMatch(parameter.value("enabled_when").toObject(), context)
			|| !parameter.value("available").toBool(true))
		{
			continue;
		}
		const auto id = parameter.value("id").toString();
		const auto value = saved.value("parameters").toObject().value(id).isUndefined()
			? context.value(parameter.value("default_from").toString()).isUndefined()
				? parameter.value("default")
				: context.value(parameter.value("default_from").toString())
			: saved.value("parameters").toObject().value(id);
		if (parameter.value("type") == "enum")
		{
			bool found = false;
			for (const auto& entry : parameter.value("options").toArray())
			{
				const auto option = entry.toObject();
				found |= option.value("id") == value && option.value("available").toBool()
					&& conditionsMatch(option.value("enabled_when").toObject(), context);
			}
			if (!found)
			{
				error = tr("Unavailable SVC option: %1").arg(parameter.value("name").toString());
				return {};
			}
		}
		else if (!(value.isNull() && parameter.value("nullable").toBool())
			&& (!value.isDouble() || !std::isfinite(value.toDouble())
				|| (parameter.value("type") == "integer" && value.toDouble() != std::floor(value.toDouble()))
				|| (parameter.contains("minimum") && value.toDouble() < parameter.value("minimum").toDouble())
				|| (parameter.contains("maximum") && value.toDouble() > parameter.value("maximum").toDouble())))
		{
			error = tr("SVC parameter is outside the backend range: %1").arg(parameter.value("name").toString());
			return {};
		}
		parameters.insert(id, value);
		const auto excluded = parameter.value("excluded_range").toArray();
		if (excluded.size() == 2 && value.toDouble() >= excluded[0].toDouble()
			&& value.toDouble() <= excluded[1].toDouble())
		{
			error = tr("Invalid special-value range: %1").arg(parameter.value("name").toString());
			return {};
		}
	}
	selection.insert("parameters", parameters);
	return selection;
}
} // namespace lmms::svc
