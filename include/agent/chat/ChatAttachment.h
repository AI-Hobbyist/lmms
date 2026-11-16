#ifndef LMMS_AGENT_CHAT_ATTACHMENT_H
#define LMMS_AGENT_CHAT_ATTACHMENT_H

#include <QJsonObject>

#include "agent/chat/ChatConnection.h"

namespace lmms::agent::chat {

struct ChatAttachment
{
	QString path;
	QString name;
	QString mimeType;
	QString type;
	qint64 size = 0;
	QJsonObject toJson() const;
};

// Selection inspects local metadata only. prepareChatAttachment reads the bytes
// when the user sends; its canonical part is then encoded for the chosen API.
bool inspectChatAttachment(const QString& path, ChatAttachment& attachment, QString& error);
bool supportsChatAttachment(const ChatConnection& connection, const ChatAttachment& attachment, QString& error);
bool prepareChatAttachment(const ChatConnection& connection, const ChatAttachment& attachment, QJsonObject& part,
						   QString& error);

} // namespace lmms::agent::chat
#endif
