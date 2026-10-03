#include "agent/mcp/HttpMcpServer.h"
#include "agent/mcp/McpProtocol.h"
#include <QCoreApplication>
#include <QHostAddress>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPointer>
#include <QTcpServer>
#include <QTcpSocket>
#include <QThread>
#include <QTimer>
#include <utility>

namespace lmms::agent::mcp
{
namespace
{
constexpr int HeaderLimit = 16384;
constexpr int BodyLimit = 4 * 1024 * 1024;
constexpr int ConnectionLimit = 32;
constexpr int ReadTimeoutMs = 10000;
std::unique_ptr<HttpMcpServer> globalService;

QByteArray statusName(int status)
{
	switch (status)
	{
	case 200: return "OK";
	case 202: return "Accepted";
	case 400: return "Bad Request";
	case 401: return "Unauthorized";
	case 403: return "Forbidden";
	case 404: return "Not Found";
	case 405: return "Method Not Allowed";
	case 408: return "Request Timeout";
	case 413: return "Content Too Large";
	case 415: return "Unsupported Media Type";
	case 429: return "Too Many Requests";
	case 431: return "Request Header Fields Too Large";
	case 501: return "Not Implemented";
	case 503: return "Service Unavailable";
	default: return "Error";
	}
}
HttpResponse error(int status, const QString& message)
{
	return {status, {{"Content-Type", "application/json"}},
		QJsonDocument(QJsonObject{{"error", message}}).toJson(QJsonDocument::Compact)};
}
}

struct HttpMcpServer::Impl
{
	struct Client
	{
		QPointer<QTcpSocket> socket;
		QTimer* timer = nullptr;
		QByteArray pending;
		HttpRequest request;
		int length = -1;
		bool dispatched = false;
		bool responded = false;
	};
	HttpMcpServer* owner;
	QTcpServer listener;
	QMap<QTcpSocket*, std::shared_ptr<Client>> clients;
	Handler handler;
	State state = State::Stopped;
	QString lastError;
	unsigned int generation = 0;

	explicit Impl(HttpMcpServer* parent) : owner(parent), listener(parent)
	{
		QObject::connect(&listener, &QTcpServer::newConnection, owner, [this] { accept(); });
	}
	void change(State next)
	{
		state = next;
		emit owner->stateChanged();
	}
	void send(const std::shared_ptr<Client>& client, HttpResponse response)
	{
		if (!client->socket || client->responded || client->socket->state() != QAbstractSocket::ConnectedState) { return; }
		client->responded = true;
		client->timer->stop();
		QByteArray wire = "HTTP/1.1 " + QByteArray::number(response.status) + " " + statusName(response.status) + "\r\n";
		for (auto header = response.headers.begin(); header != response.headers.end(); ++header)
		{
			if (header.key().contains('\r') || header.key().contains('\n') || header.value().contains('\r') || header.value().contains('\n')) { continue; }
			if (header.key().toLower() == "content-length" || header.key().toLower() == "connection") { continue; }
			wire += header.key() + ": " + header.value() + "\r\n";
		}
		wire += "Content-Length: " + QByteArray::number(response.body.size()) + "\r\nConnection: close\r\nCache-Control: no-store\r\n\r\n";
		client->socket->write(wire + response.body);
		client->socket->disconnectFromHost();
	}
	void accept()
	{
		while (auto* socket = listener.nextPendingConnection())
		{
			socket->setParent(owner);
			if (clients.size() >= ConnectionLimit) { socket->abort(); socket->deleteLater(); continue; }
			socket->setReadBufferSize(HeaderLimit + BodyLimit + 1);
			auto client = std::make_shared<Client>();
			client->socket = socket;
			client->timer = new QTimer(socket);
			client->timer->setSingleShot(true);
			clients.insert(socket, client);
			QObject::connect(client->timer, &QTimer::timeout, owner, [this, client] { send(client, error(408, "HTTP request timed out.")); });
			QObject::connect(socket, &QTcpSocket::readyRead, owner, [this, client] { read(client); });
			QObject::connect(socket, &QTcpSocket::disconnected, owner, [this, socket] { clients.remove(socket); socket->deleteLater(); });
			client->timer->start(ReadTimeoutMs);
			if (socket->bytesAvailable()) { read(client); }
		}
	}
	void read(const std::shared_ptr<Client>& client)
	{
		if (client->dispatched || client->responded || !client->socket) { return; }
		client->pending += client->socket->readAll();
		if (client->length < 0)
		{
			const int boundary = client->pending.indexOf("\r\n\r\n");
			if (boundary < 0)
			{
				if (client->pending.size() > HeaderLimit) { send(client, error(431, "HTTP headers exceed the limit.")); }
				return;
			}
			if (boundary > HeaderLimit) { send(client, error(431, "HTTP headers exceed the limit.")); return; }
			const auto lines = client->pending.left(boundary).split('\n');
			const auto first = lines[0].trimmed().split(' ');
			if (first.size() != 3 || first[2] != "HTTP/1.1") { send(client, error(400, "Expected an HTTP/1.1 request.")); return; }
			client->request.method = first[0];
			client->request.path = first[1];
			for (int i = 1; i < lines.size(); ++i)
			{
				const auto line = lines[i].trimmed();
				const int colon = line.indexOf(':');
				if (colon < 1) { send(client, error(400, "Invalid HTTP header.")); return; }
				const auto name = line.left(colon).toLower();
				if (client->request.headers.contains(name)) { send(client, error(400, "Duplicate HTTP header.")); return; }
				client->request.headers.insert(name, line.mid(colon + 1).trimmed());
			}
			if (client->request.headers.contains("transfer-encoding"))
			{
				send(client, error(400, "Transfer-Encoding is unsupported; send Content-Length.")); return;
			}
			client->length = 0;
			if (client->request.headers.contains("content-length"))
			{
				bool valid = false;
				const auto raw = client->request.headers.value("content-length");
				const auto length = raw.toLongLong(&valid);
				bool digits = !raw.isEmpty();
				for (const auto character : raw) { digits = digits && character >= '0' && character <= '9'; }
				if (!valid || !digits || length < 0) { send(client, error(400, "Invalid Content-Length.")); return; }
				if (length > BodyLimit) { send(client, error(413, "HTTP body exceeds the limit.")); return; }
				client->length = static_cast<int>(length);
			}
			client->pending.remove(0, boundary + 4);
		}
		if (client->pending.size() > client->length) { send(client, error(400, "HTTP pipelining or excess body bytes are unsupported.")); return; }
		if (client->pending.size() < client->length) { return; }
		client->dispatched = true;
		client->timer->stop();
		client->request.body = std::move(client->pending);
		if (handler)
		{
			const unsigned int current = generation;
			const QPointer<HttpMcpServer> guard(owner);
			handler(client->request, [this, guard, current, client](HttpResponse response) {
				if (guard && current == generation && state == State::Running) { send(client, std::move(response)); }
			});
			return;
		}
		if (client->request.path != "/mcp") { send(client, error(404, "Use the /mcp endpoint.")); return; }
		if (client->request.method == "GET")
		{
			auto response = error(405, "This endpoint returns JSON; no standalone SSE stream is provided.");
			response.headers.insert("Allow", "POST");
			send(client, response); return;
		}
		if (client->request.method != "POST") { send(client, error(405, "Use POST.")); return; }
		send(client, error(501, "MCP protocol handling is not connected."));
	}
	void stop()
	{
		if (state == State::Stopped) { return; }
		change(State::Stopping);
		++generation;
		listener.close();
		const auto sockets = clients.keys();
		for (auto* socket : sockets) { socket->abort(); socket->deleteLater(); }
		clients.clear();
		change(State::Stopped);
	}
};

HttpMcpServer::HttpMcpServer(QObject* parent) : QObject(parent), m_impl(std::make_unique<Impl>(this)) {}
HttpMcpServer::~HttpMcpServer() { m_impl->stop(); }
bool HttpMcpServer::start(int requestedPort)
{
	if (thread() != QThread::currentThread()) { return false; }
	if (isRunning()) { m_impl->lastError = "Stop the service before changing the listener."; return false; }
	m_impl->lastError.clear();
	if (requestedPort < 0 || requestedPort > 65535)
	{
		m_impl->lastError = "The port must be between 0 and 65535."; emit stateChanged(); return false;
	}
	m_impl->change(State::Starting);
	if (!m_impl->listener.listen(QHostAddress::LocalHost, static_cast<quint16>(requestedPort)))
	{
		m_impl->lastError = m_impl->listener.errorString(); m_impl->change(State::Stopped); return false;
	}
	m_impl->change(State::Running);
	return true;
}
void HttpMcpServer::stop() { if (thread() == QThread::currentThread()) { m_impl->stop(); } }
bool HttpMcpServer::isRunning() const { return m_impl->state == State::Running; }
HttpMcpServer::State HttpMcpServer::state() const { return m_impl->state; }
int HttpMcpServer::port() const { return m_impl->listener.serverPort(); }
QString HttpMcpServer::endpoint() const { return isRunning() ? QString("http://127.0.0.1:%1/mcp").arg(port()) : QString(); }
QString HttpMcpServer::errorString() const { return m_impl->lastError; }
int HttpMcpServer::connectionCount() const { return m_impl->clients.size(); }
void HttpMcpServer::setHandler(Handler handler) { m_impl->handler = std::move(handler); }
HttpMcpServer& service()
{
	if (!globalService)
	{
		globalService = std::make_unique<HttpMcpServer>();
		new McpProtocol(*globalService);
	}
	return *globalService;
}
void shutdownService() { globalService.reset(); }
}
