#include "agent/mcp/HttpMcpServer.h"
#include "agent/mcp/McpProtocol.h"
#include "ConfigManager.h"
#include <QCoreApplication>
#include <QHostAddress>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPointer>
#include <QTcpServer>
#include <QTcpSocket>
#include <QThread>
#include <QTimer>
#include <QRegularExpression>
#include <utility>

namespace lmms::agent::mcp
{
namespace
{
constexpr int HeaderLimit = 16384;
constexpr int BodyLimit = 4 * 1024 * 1024;
constexpr int ConnectionLimit = 32;
constexpr int ReadTimeoutMs = 10000;
constexpr int ResponseLimit = 8 * 1024 * 1024;
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
	QByteArray token;
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
		if (response.body.size() > ResponseLimit) { response = error(503, "MCP response exceeds 8 MiB; use a narrower query."); }
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
		client->timer->start(ReadTimeoutMs);
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
			QObject::connect(client->timer, &QTimer::timeout, owner, [this, client] {
				if (client->responded) { if (client->socket) { client->socket->abort(); } }
				else { send(client, error(408, "HTTP request timed out.")); }
			});
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
		const auto host = client->request.headers.value("host").toLower();
		const auto suffix = ":" + QByteArray::number(listener.serverPort());
		if (host != "127.0.0.1" + suffix && host != "localhost" + suffix &&
			!(listener.serverPort() == 80 && (host == "127.0.0.1" || host == "localhost")))
		{
			send(client, error(403, "Host must match the local MCP endpoint.")); return;
		}
		if (client->request.headers.contains("origin"))
		{
			const auto origin = client->request.headers.value("origin");
			if (origin != "http://127.0.0.1" + suffix && origin != "http://localhost" + suffix)
			{
				send(client, error(403, "Origin must match the local MCP endpoint.")); return;
			}
		}
		const auto authorization = client->request.headers.value("authorization");
		const auto supplied = authorization.mid(7);
		const auto& expected = token;
		unsigned int difference = authorization.left(7).toLower() == "bearer " && supplied.size() == expected.size() ? 0 : 1;
		for (int i = 0; i < expected.size(); ++i)
		{
			difference |= static_cast<unsigned char>(expected[i]) ^
				(i < supplied.size() ? static_cast<unsigned char>(supplied[i]) : 0);
		}
		if (difference)
		{
			auto response = error(401, "A valid local bearer token is required.");
			response.headers.insert("WWW-Authenticate", "Bearer"); send(client, response); return;
		}
		client->timer->start(ReadTimeoutMs);
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
		++generation;
		listener.close();
		const auto sockets = clients.keys();
		for (auto* socket : sockets) { socket->abort(); socket->deleteLater(); }
		clients.clear();
		token.fill('\0'); token.clear();
		change(State::Stopping);
		change(State::Stopped);
	}
};

HttpMcpServer::HttpMcpServer(QObject* parent) : QObject(parent), m_impl(std::make_unique<Impl>(this)) {}
HttpMcpServer::~HttpMcpServer() { m_impl->stop(); }
bool HttpMcpServer::start(int requestedPort, const QByteArray& token)
{
	if (thread() != QThread::currentThread()) { return false; }
	if (isRunning()) { m_impl->lastError = "Stop the service before changing the listener."; return false; }
	m_impl->lastError.clear();
	if (requestedPort < 0 || requestedPort > 65535)
	{
		m_impl->lastError = "The port must be between 0 and 65535."; emit stateChanged(); return false;
	}
	bool validToken = token.size() >= 16 && token.size() <= 512;
	for (const auto character : token) { validToken = validToken && character >= 0x21 && character <= 0x7e; }
	if (!validToken)
	{
		m_impl->lastError = "Provide a 16..512 character local bearer token without spaces via tokenEnv.";
		emit stateChanged(); return false;
	}
	m_impl->change(State::Starting);
	if (!m_impl->listener.listen(QHostAddress::LocalHost, static_cast<quint16>(requestedPort)))
	{
		m_impl->lastError = m_impl->listener.errorString(); m_impl->change(State::Stopped); return false;
	}
	m_impl->token = token;
	m_impl->change(State::Running);
	return true;
}
void HttpMcpServer::stop()
{
	if (thread() != QThread::currentThread()) { return; }
	const bool clearError = !m_impl->lastError.isEmpty();
	const bool wasStopped = m_impl->state == State::Stopped;
	m_impl->lastError.clear();
	m_impl->stop();
	if (clearError && wasStopped) { emit stateChanged(); }
}
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
bool applyConfiguration()
{
	auto& server = service();
	server.stop();
	const auto* config = ConfigManager::inst();
	const auto enabled = config->value("agentMcp", "enabled");
	if (enabled.isEmpty() || enabled == "false" || enabled == "0") { return true; }
	if (enabled != "true" && enabled != "1") { return server.start(-1); }
	const auto portText = config->value("agentMcp", "port");
	bool valid = portText.isEmpty();
	const int port = portText.isEmpty() ? 0 : portText.toInt(&valid);
	if (!valid || (!portText.isEmpty() && !QRegularExpression("^[0-9]+$").match(portText).hasMatch())) { return server.start(-1); }
	auto environment = config->value("agentMcp", "tokenEnv");
	if (environment.isEmpty()) { environment = "LMMS_MCP_TOKEN"; }
	if (!QRegularExpression("^[A-Za-z_][A-Za-z0-9_]{0,127}$").match(environment).hasMatch()) { return server.start(port); }
	return server.start(port, qgetenv(environment.toLatin1().constData()));
}
}
