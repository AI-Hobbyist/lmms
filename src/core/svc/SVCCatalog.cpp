#include "SVCCatalog.h"

#ifdef _WIN32
#include <windows.h>

#include <wincred.h>
#endif

#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <algorithm>
#include <cmath>

#include "ConfigManager.h"

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
		if (values.value(it.key()) != it.value()) { return false; }
	}
	return true;
}

QJsonObject selectionContext(const QJsonObject& selection, const QJsonObject& model)
{
	auto context = model;
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
	emit connectionChanged(id);
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
	for (const auto& pair : {std::pair{"speaker_id", "speakers"}, std::pair{"weight_id", "weights"}})
	{
		const auto entries = model.value(pair.second).toArray();
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
	const auto context = selectionContext(selection, model);
	for (const auto& entry : profile.capabilities.value("parameters").toArray())
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
			? parameter.value("default")
			: saved.value("parameters").toObject().value(id);
		if (parameter.value("type") == "enum")
		{
			bool found = false;
			for (const auto& entry : parameter.value("options").toArray())
			{
				const auto option = entry.toObject();
				found |= option.value("id") == value && option.value("available").toBool();
			}
			if (!found)
			{
				error = tr("Unavailable SVC option: %1").arg(parameter.value("name").toString());
				return {};
			}
		}
		else if (!value.isDouble() || !std::isfinite(value.toDouble())
			|| (parameter.value("type") == "integer" && value.toDouble() != std::floor(value.toDouble()))
			|| (parameter.contains("minimum") && value.toDouble() < parameter.value("minimum").toDouble())
			|| (parameter.contains("maximum") && value.toDouble() > parameter.value("maximum").toDouble()))
		{
			error = tr("SVC parameter is outside the backend range: %1").arg(parameter.value("name").toString());
			return {};
		}
		parameters.insert(id, value);
	}
	selection.insert("parameters", parameters);
	return selection;
}
} // namespace lmms::svc
