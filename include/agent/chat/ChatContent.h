#ifndef LMMS_AGENT_CHAT_CONTENT_H
#define LMMS_AGENT_CHAT_CONTENT_H

#include <QJsonArray>

#include "agent/chat/ChatConnection.h"

namespace lmms::agent::chat {

QJsonObject chatNativeOrigin(const ChatConnection& connection);
bool validateChatToolHistory(const QJsonArray& messages, QString& error);

bool encodeChatMessage(const ChatConnection& connection, const QJsonObject& message, QJsonObject& encoded,
					   QString& error);

} // namespace lmms::agent::chat
#endif
