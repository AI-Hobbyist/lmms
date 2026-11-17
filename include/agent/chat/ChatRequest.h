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
// Tools are an explicit catalog snapshot; this function never loads or runs them.
// hostedSearch adds the provider's own web search tool; without it the body
// carries no hosted search, grounding or related configuration.
bool buildChatRequest(const ChatConnection& connection, const QString& key, const QString& systemPrompt,
	const QJsonArray& messages, ChatRequest& request, QString& error, const QJsonArray& tools = {},
	bool hostedSearch = false);

// Whether the connection's protocol and declared capability support hosted web search.
bool supportsHostedWebSearch(const ChatConnection& connection);

} // namespace lmms::agent::chat
#endif
