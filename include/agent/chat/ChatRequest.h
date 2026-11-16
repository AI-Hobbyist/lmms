#ifndef LMMS_AGENT_CHAT_REQUEST_H
#define LMMS_AGENT_CHAT_REQUEST_H

#include <QJsonArray>
#include <QJsonObject>
#include <QNetworkRequest>

#include "agent/chat/ChatConnection.h"

namespace lmms::agent::chat {

struct ChatRequest
{
	QNetworkRequest network;
	QJsonObject body;
};

// Builds a conversation request without network or credential-store access.
// Messages contain user/assistant content, normalized tool_calls, or tool results
// with tool_call_id/name/content/is_error. Native assistant blocks retain origin.
// Attachments must be explicitly prepared from user-selected files beforehand.
bool buildChatRequest(const ChatConnection& connection, const QString& key, const QString& systemPrompt,
					  const QJsonArray& messages, ChatRequest& request, QString& error);

} // namespace lmms::agent::chat
#endif
