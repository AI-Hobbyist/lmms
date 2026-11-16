#include "agent/chat/ChatConnection.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>
#include <QSet>
#include <QUrl>
#include <QUuid>
#include <cmath>

#ifdef Q_OS_WIN
#include <windows.h>

#include <wincred.h>
#endif

namespace lmms::agent::chat {
namespace {

bool validId(const QString& id)
{
	const QUuid uuid(id);
	return !uuid.isNull() && uuid.toString(QUuid::WithoutBraces) == id;
}

QString credentialError(unsigned long code)
{ return QCoreApplication::translate("AgentChat", "Windows Credential Manager error %1.").arg(code); }

} // namespace

ChatConnection ChatConnection::create()
{
	ChatConnection result;
	result.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
	result.name = QCoreApplication::translate("AgentChat", "New connection");
	return result;
}

QStringList ChatConnection::protocols()
{
	return {"openai-chat", "openai-responses", "anthropic-messages", "gemini",
			"azure-chat",  "azure-responses",  "openai-compatible",	 "custom-json"};
}

QJsonObject ChatConnection::toJson() const
{
	return {{"id", id},
			{"name", name},
			{"provider", provider},
			{"protocol", protocol},
			{"baseUrl", baseUrl},
			{"model", model},
			{"deployment", deployment},
			{"apiVersion", apiVersion},
			{"keyHeader", keyHeader},
			{"keyPrefix", keyPrefix},
			{"reasoning", reasoning},
			{"temperature", temperature},
			{"capabilities", capabilities},
			{"mapping", mapping}};
}

ChatConnection ChatConnection::fromJson(const QJsonObject& object)
{
	ChatConnection result;
	result.id = object.value("id").toString();
	result.name = object.value("name").toString();
	result.provider = object.value("provider").toString();
	result.protocol = object.value("protocol").toString("openai-chat");
	result.baseUrl = object.value("baseUrl").toString();
	result.model = object.value("model").toString();
	result.deployment = object.value("deployment").toString();
	result.apiVersion = object.value("apiVersion").toString();
	result.keyHeader = object.value("keyHeader").toString("Authorization");
	result.keyPrefix = object.value("keyPrefix").toString("Bearer ");
	result.reasoning = object.value("reasoning").toString("auto");
	result.temperature = object.value("temperature").toDouble(0.7);
	result.capabilities = object.value("capabilities").toObject();
	result.mapping = object.value("mapping").toObject();
	return result;
}

bool ChatConnection::validate(QString& error) const
{
	error.clear();
	const QUrl url(baseUrl);
	if (!validId(id) || name.trimmed().isEmpty() || !protocols().contains(protocol))
	{
		error = QCoreApplication::translate("AgentChat", "Invalid connection identifier, name or protocol.");
	}
	else if (!url.isValid() || (url.scheme() != "https" && url.scheme() != "http") || url.host().isEmpty()
			 || !url.userInfo().isEmpty() || url.hasQuery() || url.hasFragment())
	{
		error = QCoreApplication::translate("AgentChat",
											"API base URL must be HTTP(S), without credentials, query or fragment.");
	}
	else if (!std::isfinite(temperature) || temperature < 0 || temperature > 2)
	{
		error = QCoreApplication::translate("AgentChat", "Temperature must be between 0 and 2.");
	}
	else if (keyHeader.trimmed().isEmpty() || keyHeader.contains('\r') || keyHeader.contains('\n')
			 || keyPrefix.contains('\r') || keyPrefix.contains('\n'))
	{
		error = QCoreApplication::translate("AgentChat", "Invalid API key header configuration.");
	}
	return error.isEmpty();
}

ChatConnectionStore::ChatConnectionStore(QString directory)
	: m_directory(QDir(directory).absolutePath())
{
}

QVector<ChatConnection> ChatConnectionStore::connections(QString& error) const
{
	error.clear();
	QFile file(QDir(m_directory).filePath("connections.json"));
	if (!file.exists())
	{
		return {};
	}
	if (!file.open(QIODevice::ReadOnly))
	{
		error = file.errorString();
		return {};
	}
	QJsonParseError parseError;
	const auto document = QJsonDocument::fromJson(file.readAll(), &parseError);
	const auto root = document.object();
	if (parseError.error != QJsonParseError::NoError || root.value("version").toInt() != 1
		|| !root.value("connections").isArray())
	{
		error = QCoreApplication::translate("AgentChat", "Invalid connection settings JSON or unsupported version.");
		return {};
	}
	QVector<ChatConnection> result;
	QSet<QString> ids;
	for (const auto& value : root.value("connections").toArray())
	{
		const auto connection = ChatConnection::fromJson(value.toObject());
		if (!connection.validate(error))
		{
			return {};
		}
		if (ids.contains(connection.id))
		{
			error = QCoreApplication::translate("AgentChat", "Duplicate connection identifier.");
			return {};
		}
		ids.insert(connection.id);
		result.append(connection);
	}
	return result;
}

bool ChatConnectionStore::writeConnections(const QVector<ChatConnection>& connections, QString& error) const
{
	if (!QDir().mkpath(m_directory))
	{
		error = QCoreApplication::translate("AgentChat", "Cannot create connection settings directory.");
		return false;
	}
	QJsonArray entries;
	for (const auto& connection : connections)
	{
		entries.append(connection.toJson());
	}
	QSaveFile file(QDir(m_directory).filePath("connections.json"));
	if (!file.open(QIODevice::WriteOnly))
	{
		error = file.errorString();
		return false;
	}
	const auto bytes = QJsonDocument(QJsonObject{{"version", 1}, {"connections", entries}}).toJson();
	if (file.write(bytes) != bytes.size() || !file.commit())
	{
		error = file.errorString();
		return false;
	}
	return true;
}

bool ChatConnectionStore::saveConnection(const ChatConnection& connection, QString& error)
{
	if (!connection.validate(error))
	{
		return false;
	}
	auto entries = connections(error);
	if (!error.isEmpty())
	{
		return false;
	}
	for (auto& entry : entries)
	{
		if (entry.id == connection.id)
		{
			entry = connection;
			return writeConnections(entries, error);
		}
	}
	entries.append(connection);
	return writeConnections(entries, error);
}

bool ChatConnectionStore::removeConnection(const QString& id, QString& error)
{
	auto entries = connections(error);
	if (!error.isEmpty())
	{
		return false;
	}
	for (auto iterator = entries.begin(); iterator != entries.end(); ++iterator)
	{
		if (iterator->id == id)
		{
			if (!removeKey(id, error))
			{
				return false;
			}
			entries.erase(iterator);
			return writeConnections(entries, error);
		}
	}
	error = QCoreApplication::translate("AgentChat", "Connection not found.");
	return false;
}

QString ChatConnectionStore::credentialTarget(const QString& id) const
{
	// Separate profiles (including temporary tests) cannot overwrite each other's keys.
	const auto profile
		= QCryptographicHash::hash(m_directory.toCaseFolded().toUtf8(), QCryptographicHash::Sha256).toHex();
	return QStringLiteral("LMMS/AgentChat/%1/%2").arg(QString::fromLatin1(profile), id);
}

bool ChatConnectionStore::writeKey(const QString& id, const QString& key, QString& error) const
{
	error.clear();
	if (!validId(id) || key.isEmpty())
	{
		error = QCoreApplication::translate("AgentChat", "A valid connection and non-empty API key are required.");
		return false;
	}
#ifdef Q_OS_WIN
	auto target = credentialTarget(id).toStdWString();
	auto bytes = key.toUtf8();
	if (bytes.size() > CRED_MAX_CREDENTIAL_BLOB_SIZE)
	{
		error = QCoreApplication::translate("AgentChat", "API key exceeds the Windows credential size limit.");
		return false;
	}
	CREDENTIALW credential{};
	credential.Type = CRED_TYPE_GENERIC;
	credential.TargetName = target.data();
	credential.Persist = CRED_PERSIST_LOCAL_MACHINE;
	credential.CredentialBlobSize = static_cast<DWORD>(bytes.size());
	credential.CredentialBlob = reinterpret_cast<LPBYTE>(bytes.data());
	const bool saved = CredWriteW(&credential, 0);
	const auto code = saved ? ERROR_SUCCESS : GetLastError();
	SecureZeroMemory(bytes.data(), bytes.size());
	if (!saved)
	{
		error = credentialError(code);
	}
	return saved;
#else
	error = QCoreApplication::translate("AgentChat", "Native API key storage is available on Windows only.");
	return false;
#endif
}

QString ChatConnectionStore::readKey(const QString& id, QString& error) const
{
	error.clear();
	if (!validId(id))
	{
		error = QCoreApplication::translate("AgentChat", "Invalid connection identifier.");
		return {};
	}
#ifdef Q_OS_WIN
	const auto target = credentialTarget(id).toStdWString();
	PCREDENTIALW credential = nullptr;
	if (!CredReadW(target.c_str(), CRED_TYPE_GENERIC, 0, &credential))
	{
		const auto code = GetLastError();
		if (code != ERROR_NOT_FOUND)
		{
			error = credentialError(code);
		}
		return {};
	}
	const auto key = QString::fromUtf8(reinterpret_cast<const char*>(credential->CredentialBlob),
									   static_cast<qsizetype>(credential->CredentialBlobSize));
	SecureZeroMemory(credential->CredentialBlob, credential->CredentialBlobSize);
	CredFree(credential);
	return key;
#else
	error = QCoreApplication::translate("AgentChat", "Native API key storage is available on Windows only.");
	return {};
#endif
}

bool ChatConnectionStore::removeKey(const QString& id, QString& error) const
{
	error.clear();
	if (!validId(id))
	{
		error = QCoreApplication::translate("AgentChat", "Invalid connection identifier.");
		return false;
	}
#ifdef Q_OS_WIN
	const auto target = credentialTarget(id).toStdWString();
	if (CredDeleteW(target.c_str(), CRED_TYPE_GENERIC, 0))
	{
		return true;
	}
	const auto code = GetLastError();
	if (code == ERROR_NOT_FOUND)
	{
		return true;
	}
	error = credentialError(code);
	return false;
#else
	error = QCoreApplication::translate("AgentChat", "Native API key storage is available on Windows only.");
	return false;
#endif
}

} // namespace lmms::agent::chat
