#ifndef LMMS_AGENT_CHAT_WEB_SEARCH_H
#define LMMS_AGENT_CHAT_WEB_SEARCH_H

#include <QJsonObject>
#include <QObject>
#include <QPointer>

#include "agent/chat/ChatSearchSettings.h"

class QNetworkAccessManager;
class QNetworkReply;

namespace lmms::agent::chat {

// Runs one external web search (SearXNG JSON API or Brave Search API) at a
// time. The result is a CommandResult-shaped JSON object so it can be stored
// and sent to the model exactly like an internal tool result.
class ChatWebSearch : public QObject
{
	Q_OBJECT
public:
	explicit ChatWebSearch(QObject* parent = nullptr);
	~ChatWebSearch() override;

	// Model-visible tool name and its definition for the chat tool catalog.
	static QString command();
	static QJsonObject definition();

	// Starts a search; any running search is aborted first (its result is dropped).
	void start(const ChatSearchSettings& settings, const QString& credential, const QString& query, int count);
	void abort();
	bool active() const;

signals:
	void finished(const QJsonObject& result);

private:
	void handleReply(QNetworkReply* reply);

	QNetworkAccessManager* m_network = nullptr;
	QPointer<QNetworkReply> m_reply;
	ChatSearchBackend m_backend = ChatSearchBackend::SearXNG;
	QString m_query;
	int m_count = 5;
};

} // namespace lmms::agent::chat

#endif // LMMS_AGENT_CHAT_WEB_SEARCH_H
