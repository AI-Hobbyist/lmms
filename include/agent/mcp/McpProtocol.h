#ifndef LMMS_AGENT_MCP_PROTOCOL_H
#define LMMS_AGENT_MCP_PROTOCOL_H
#include <QObject>
#include <QString>
#include <QJsonObject>
#include <QJsonValue>
#include <deque>
#include "agent/mcp/HttpMcpServer.h"

namespace lmms::agent::mcp
{
// One local client session, reset on initialization or server stop. No project/session isolation.
class LMMS_EXPORT McpProtocol : public QObject
{
public:
	explicit McpProtocol(HttpMcpServer& server);
	static QString version();
private:
	void handle(const HttpRequest& request, HttpMcpServer::Reply reply);
	void reset();
	void drain();
	struct Pending
	{
		QJsonValue id;
		QString name;
		QJsonObject arguments;
		HttpMcpServer::Reply reply;
	};
	std::deque<Pending> m_queue;
	bool m_executing = false;
	bool m_scheduled = false;
	QString m_session;
	bool m_initialized = false;
};
}
#endif
