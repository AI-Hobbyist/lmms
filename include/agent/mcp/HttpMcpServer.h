#ifndef LMMS_AGENT_HTTP_MCP_SERVER_H
#define LMMS_AGENT_HTTP_MCP_SERVER_H

#include <QByteArray>
#include <QMap>
#include <QObject>
#include <QString>
#include <functional>
#include <memory>
#include "lmms_export.h"

namespace lmms::agent::mcp
{
struct HttpRequest
{
	QByteArray method;
	QByteArray path;
	QMap<QByteArray, QByteArray> headers;
	QByteArray body;
};
struct HttpResponse
{
	int status = 200;
	QMap<QByteArray, QByteArray> headers;
	QByteArray body;
};

// Loopback-only MCP transport. Handlers and replies run on the owning application thread.
class LMMS_EXPORT HttpMcpServer : public QObject
{
	Q_OBJECT
public:
	enum class State { Stopped, Starting, Running, Stopping };
	Q_ENUM(State)
	using Reply = std::function<void(HttpResponse)>;
	using Handler = std::function<void(const HttpRequest&, Reply)>;

	explicit HttpMcpServer(QObject* parent = nullptr);
	~HttpMcpServer() override;
	bool start(int port = 0);
	void stop();
	bool isRunning() const;
	State state() const;
	int port() const;
	QString endpoint() const;
	QString errorString() const;
	int connectionCount() const;
	void setHandler(Handler handler);

signals:
	void stateChanged();

private:
	struct Impl;
	std::unique_ptr<Impl> m_impl;
};

LMMS_EXPORT HttpMcpServer& service();
LMMS_EXPORT void shutdownService();
}
#endif
