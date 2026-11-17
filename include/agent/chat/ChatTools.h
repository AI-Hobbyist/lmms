#ifndef LMMS_AGENT_CHAT_TOOLS_H
#define LMMS_AGENT_CHAT_TOOLS_H

#include <QHash>
#include <QJsonArray>
#include <QJsonObject>

#include "agent/chat/ChatConnection.h"

namespace lmms::agent::chat {

struct ChatToolCall
{
	QString id;
	QString wireName;
	QString command;
	QJsonObject arguments;
};

// A turn owns its catalog snapshot. Loading and resolving never execute commands.
class ChatToolCatalog
{
public:
	// Local tools are chat-owned (for example web search) and never run through CommandBus.
	bool load(QString& error, const QJsonArray& localTools = {});
	QJsonArray definitions() const;
	QString nameForCommand(const QString& command) const;
	bool isLocal(const QString& command) const;
	QString description(const QString& command) const;
	bool resolve(const QJsonObject& call, ChatToolCall& resolved, QString& error) const;

private:
	QJsonArray m_definitions;
	QHash<QString, QJsonObject> m_byWireName;
};

bool encodeChatToolDefinitions(const ChatConnection& connection, const QJsonArray& definitions, QJsonArray& encoded,
							   QString& error);

} // namespace lmms::agent::chat
#endif
