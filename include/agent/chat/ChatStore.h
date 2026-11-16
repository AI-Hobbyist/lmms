#ifndef LMMS_AGENT_CHAT_STORE_H
#define LMMS_AGENT_CHAT_STORE_H

#include <QJsonArray>
#include <QJsonObject>
#include <QString>

#include "lmms_export.h"

namespace lmms::agent::chat {

class LMMS_EXPORT ChatStore
{
public:
	explicit ChatStore(QString directory);

	QString directory() const;
	QJsonArray sessions() const;
	QJsonObject session(const QString& id) const;
	bool saveSession(QJsonObject session, QString& error);
	bool removeSession(const QString& id, QString& error);
	bool exportSession(const QString& id, const QString& destination, QString& error) const;
	QString importSession(const QString& source, QString& error);
	static QJsonObject newSession();
	static QString readPrompt(const QString& path, QString& error);
	static QString resolvePromptPath(const QString& path);

private:
	QString sessionPath(const QString& id) const;
	QString m_directory;
};

} // namespace lmms::agent::chat

#endif
