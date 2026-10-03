#ifndef LMMS_AGENT_MCP_PROTOCOL_H
#define LMMS_AGENT_MCP_PROTOCOL_H
#include <QObject>
#include <QString>
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
	QString m_session;
	bool m_initialized = false;
};
}
#endif
