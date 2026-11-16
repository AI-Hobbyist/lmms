#include "agent/chat/ChatStore.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QSaveFile>
#include <QUuid>

namespace lmms::agent::chat {
namespace {

bool writeJson(const QString& path, const QJsonObject& object, QString& error)
{
	QSaveFile file(path);
	if (!file.open(QIODevice::WriteOnly))
	{
		error = file.errorString();
		return false;
	}
	const auto bytes = QJsonDocument(object).toJson();
	if (file.write(bytes) != bytes.size() || !file.commit())
	{
		error = file.errorString();
		return false;
	}
	return true;
}

} // namespace

ChatStore::ChatStore(QString directory)
	: m_directory(std::move(directory))
{
}

QString ChatStore::directory() const
{ return m_directory; }

QString ChatStore::sessionPath(const QString& id) const
{
	const auto uuid = QUuid(id);
	if (uuid.isNull() || uuid.toString(QUuid::WithoutBraces) != id)
	{
		return {};
	}
	return QDir(m_directory).filePath(id + ".json");
}

QJsonObject ChatStore::session(const QString& id) const
{
	const auto path = sessionPath(id);
	if (path.isEmpty())
	{
		return {};
	}
	QFile file(path);
	if (!file.open(QIODevice::ReadOnly))
	{
		return {};
	}
	const auto object = QJsonDocument::fromJson(file.readAll()).object();
	return object.value("id").toString() == id && object.value("version").toInt() == 1 ? object : QJsonObject{};
}

QJsonArray ChatStore::sessions() const
{
	QJsonArray result;
	for (const auto& entry : QDir(m_directory).entryInfoList({"*.json"}, QDir::Files, QDir::Time))
	{
		const auto object = session(entry.completeBaseName());
		if (!object.isEmpty())
		{
			result.append(object);
		}
	}
	return result;
}

bool ChatStore::saveSession(QJsonObject object, QString& error)
{
	const auto path = sessionPath(object.value("id").toString());
	if (path.isEmpty() || object.value("version").toInt() != 1)
	{
		error = QStringLiteral("Invalid session identifier or version.");
		return false;
	}
	if (!QDir().mkpath(m_directory))
	{
		error = QStringLiteral("Cannot create session directory: %1").arg(m_directory);
		return false;
	}
	object.insert("updatedAt", QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs));
	return writeJson(path, object, error);
}

bool ChatStore::removeSession(const QString& id, QString& error)
{
	const auto path = sessionPath(id);
	if (path.isEmpty() || !QFile::remove(path))
	{
		error = QStringLiteral("Cannot remove session: %1").arg(id);
		return false;
	}
	return true;
}

bool ChatStore::exportSession(const QString& id, const QString& destination, QString& error) const
{
	const auto source = session(id);
	if (source.isEmpty())
	{
		error = QStringLiteral("Session not found.");
		return false;
	}
	// Export only the public session fields, never future connection credentials.
	QJsonObject exported;
	for (const auto* key : {"version", "id", "title", "createdAt", "updatedAt", "pinned", "archived", "messages"})
	{
		exported.insert(key, source.value(key));
	}
	return writeJson(destination, exported, error);
}

QString ChatStore::importSession(const QString& source, QString& error)
{
	QFile file(source);
	if (!file.open(QIODevice::ReadOnly))
	{
		error = file.errorString();
		return {};
	}
	QJsonParseError parseError;
	const auto document = QJsonDocument::fromJson(file.readAll(), &parseError);
	const auto imported = document.object();
	if (parseError.error != QJsonParseError::NoError || !document.isObject() || imported.value("version").toInt() != 1
		|| !imported.value("title").isString() || !imported.value("messages").isArray())
	{
		error = QStringLiteral("Invalid session JSON or unsupported version.");
		return {};
	}
	// Import conversation data only. A fresh ID prevents overwriting an existing
	// session; connection credentials and transient tool execution state stay out.
	auto session = newSession();
	for (const auto* key : {"title", "messages", "pinned", "archived"})
	{
		if (imported.contains(key))
		{
			session.insert(key, imported.value(key));
		}
	}
	return saveSession(session, error) ? session.value("id").toString() : QString{};
}

QJsonObject ChatStore::newSession()
{
	return {{"version", 1},
			{"id", QUuid::createUuid().toString(QUuid::WithoutBraces)},
			{"title", QStringLiteral("新会话")},
			{"createdAt", QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)},
			{"pinned", false},
			{"archived", false},
			{"messages", QJsonArray{}}};
}

QString ChatStore::resolvePromptPath(const QString& path)
{ return QFileInfo(path).isAbsolute() ? path : QDir(QCoreApplication::applicationDirPath()).absoluteFilePath(path); }

QString ChatStore::readPrompt(const QString& path, QString& error)
{
	QFile file(resolvePromptPath(path));
	if (!file.open(QIODevice::ReadOnly))
	{
		error = QStringLiteral("%1: %2").arg(file.fileName(), file.errorString());
		return {};
	}
	const auto text = QString::fromUtf8(file.readAll());
	if (text.trimmed().isEmpty())
	{
		error = QStringLiteral("System prompt is empty: %1").arg(file.fileName());
	}
	return text;
}

} // namespace lmms::agent::chat
