#ifndef LMMS_AGENT_CHAT_RESPONSE_H
#define LMMS_AGENT_CHAT_RESPONSE_H

#include <QJsonArray>
#include <QJsonObject>
#include <QMap>

#include "agent/chat/ChatConnection.h"
#include "agent/chat/ChatEventStream.h"

namespace lmms::agent::chat {

struct ChatResponse
{
	QString text;
	QString summary;
	QString finishReason;
	QString error;
	QJsonObject usage;
	QJsonArray toolCalls;
	QJsonArray nativeContent;
	bool complete = false;
};

class ChatResponseDecoder
{
public:
	explicit ChatResponseDecoder(ChatConnection connection);
	bool consume(const ChatStreamEvent& event);
	bool consumeJson(const QByteArray& bytes);
	bool finish();
	const ChatResponse& response() const;

private:
	struct PendingCall
	{
		QString id;
		QString name;
		QByteArray arguments;
		bool streamedArguments = false;
		QString providerId;
	};
	void consumeChat(const QJsonObject& object);
	void consumeResponses(const QJsonObject& object, const QString& type);
	void consumeAnthropic(const QJsonObject& object, const QString& type);
	void consumeGemini(const QJsonObject& object);
	void consumeCustom(const QJsonObject& object, bool document = false);
	bool complete();
	bool fail(const QString& error);
	ChatConnection m_connection;
	ChatResponse m_response;
	QMap<int, PendingCall> m_calls;
	QMap<int, QJsonObject> m_nativeBlocks;
};

} // namespace lmms::agent::chat
#endif
