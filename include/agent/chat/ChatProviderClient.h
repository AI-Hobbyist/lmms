#ifndef LMMS_AGENT_CHAT_PROVIDER_CLIENT_H
#define LMMS_AGENT_CHAT_PROVIDER_CLIENT_H

#include <QObject>
#include <QPointer>
#include <memory>

#include "agent/chat/ChatResponse.h"

class QNetworkAccessManager;
class QNetworkReply;

namespace lmms::agent::chat {

class ChatProviderClient : public QObject
{
	Q_OBJECT
public:
	explicit ChatProviderClient(QObject* parent = nullptr);
	~ChatProviderClient() override;
	bool start(const ChatConnection& connection, const QString& key, const QString& systemPrompt,
			   const QJsonArray& messages, QString& error, const QJsonArray& tools = {});
	void cancel();
	bool active() const;
	ChatResponse response() const;

signals:
	void updated(const QString& text, const QString& summary);
	void finished();
	void canceled();
	void uploadProgress(qint64 sent, qint64 total);

private:
	bool readAvailable(QNetworkReply* reply);
	void finishRequest(const QString& error = {});
	void releaseReply();
	QString redact(QString text) const;
	QNetworkAccessManager* m_network = nullptr;
	QPointer<QNetworkReply> m_reply;
	ChatEventStream m_stream;
	QByteArray m_jsonBytes;
	bool m_jsonResponse = false;
	std::unique_ptr<ChatResponseDecoder> m_decoder;
	ChatResponse m_result;
	QString m_key;
};

} // namespace lmms::agent::chat
#endif
