#ifndef LMMS_AGENT_CHAT_CONTROLLER_H
#define LMMS_AGENT_CHAT_CONTROLLER_H

#include <QObject>
#include <atomic>
#include <memory>

#include "agent/chat/ChatAttachment.h"
#include "agent/chat/ChatConnection.h"
#include "agent/chat/ChatStore.h"

namespace lmms::agent::chat {

class ChatProviderClient;

class ChatController : public QObject
{
	Q_OBJECT
public:
	explicit ChatController(ChatStore& store, QObject* parent = nullptr);
	~ChatController() override;
	bool send(const QString& sessionId, const ChatConnection& connection, const QString& key,
			  const QString& systemPrompt, const QString& text, QString& error,
			  const QVector<ChatAttachment>& attachments = {});
	bool retry(const QString& sessionId, const ChatConnection& connection, const QString& key,
			   const QString& systemPrompt, QString& error);
	void stop();
	bool active() const;
	QString activeSessionId() const;
	QJsonObject currentMessage() const;

signals:
	void sessionChanged(const QString& sessionId);
	void messageUpdated(const QString& sessionId, const QJsonObject& message);
	void stateChanged();
	void requestFailed(const QString& sessionId, const QString& error);
	void attachmentStatus(const QString& sessionId, const QString& status, qint64 sent, qint64 total);

private:
	bool startTurn(const QString& sessionId, const ChatConnection& connection, const QString& key,
				   const QString& systemPrompt, const QString& text, QString& error,
				   const QVector<ChatAttachment>& attachments, bool retry);
	bool savePreparedContent(const QString& userId, const QJsonArray& content, QString& error);
	bool saveResult(QString& error);
	void finish(const QString& status);
	ChatStore& m_store;
	ChatProviderClient* m_client = nullptr;
	QString m_sessionId;
	QString m_turnId;
	QJsonObject m_message;
	QJsonObject m_nativeOrigin;
	bool m_preparing = false;
	std::shared_ptr<std::atomic_bool> m_preparationCanceled;
};

} // namespace lmms::agent::chat
#endif
