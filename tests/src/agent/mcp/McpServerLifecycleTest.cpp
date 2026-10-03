#include <QtTest>
#include <QEventLoop>
#include <QHostAddress>
#include <QPointer>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>
#include <vector>
#include "agent/mcp/HttpMcpServer.h"

using namespace lmms::agent::mcp;
namespace
{
const QByteArray TestToken = "test-only-token-0123456789";
QByteArray authorized(int port, QByteArray wire)
{
	wire.replace("Host: localhost\r\n", "Host: localhost:" + QByteArray::number(port) + "\r\n");
	QByteArray headers = "Authorization: Bearer " + TestToken + "\r\n";
	if (!wire.contains("\r\nHost:")) { headers += "Host: localhost:" + QByteArray::number(port) + "\r\n"; }
	wire.insert(wire.indexOf("\r\n") + 2, headers);
	return wire;
}
QByteArray request(int port, const QByteArray& wire)
{
	QTcpSocket socket;
	QEventLoop loop;
	QTimer deadline; deadline.setSingleShot(true);
	QObject::connect(&deadline, &QTimer::timeout, &loop, &QEventLoop::quit);
	QObject::connect(&socket, &QTcpSocket::connected, &loop, [&] { socket.write(authorized(port, wire)); });
	QObject::connect(&socket, &QTcpSocket::disconnected, &loop, &QEventLoop::quit);
	socket.connectToHost(QHostAddress::LocalHost, port);
	deadline.start(2000);
	loop.exec();
	return socket.readAll();
}
}
class McpServerLifecycleTest : public QObject
{
	Q_OBJECT
private slots:
	void defaultsToStoppedAndBindsLoopback()
	{
		HttpMcpServer server;
		QVERIFY(!server.isRunning());
		QCOMPARE(server.port(), 0);
		QVERIFY(server.endpoint().isEmpty());
		QSignalSpy states(&server, &HttpMcpServer::stateChanged);
		QVERIFY(server.start(0, TestToken));
		QVERIFY(server.port() > 0);
		QCOMPARE(server.endpoint(), QString("http://127.0.0.1:%1/mcp").arg(server.port()));
		QVERIFY(!server.start(0, TestToken));
		QVERIFY(!server.errorString().isEmpty());
		QVERIFY(request(server.port(), "POST /mcp HTTP/1.1\r\nHost: localhost\r\nContent-Length: 2\r\n\r\n{}").startsWith("HTTP/1.1 501"));
		QVERIFY(request(server.port(), "GET /mcp HTTP/1.1\r\nHost: localhost\r\n\r\n").startsWith("HTTP/1.1 405"));
		QVERIFY(request(server.port(), "GET /other HTTP/1.1\r\nHost: localhost\r\n\r\n").startsWith("HTTP/1.1 404"));
		const int port = server.port();
		server.stop();
		QVERIFY(!server.isRunning());
		QCOMPARE(server.connectionCount(), 0);
		QTcpServer reuse;
		QVERIFY(reuse.listen(QHostAddress::LocalHost, port));
		QCOMPARE(states.size(), 4);
	}
	void handlesConflictAndRepeatedRestart()
	{
		QTcpServer occupied;
		QVERIFY(occupied.listen(QHostAddress::LocalHost, 0));
		HttpMcpServer server;
		QVERIFY(!server.start(occupied.serverPort(), TestToken));
		QVERIFY(!server.isRunning());
		QVERIFY(!server.errorString().isEmpty());
		QVERIFY(!server.start(-1, TestToken));
		QVERIFY(!server.start(65536, TestToken));
		for (int iteration = 0; iteration < 20; ++iteration)
		{
			QVERIFY(server.start(0, TestToken));
			QVERIFY(server.errorString().isEmpty());
			server.stop();
			QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
			QCOMPARE(server.connectionCount(), 0);
			QVERIFY(server.findChildren<QTcpSocket*>().isEmpty());
		}
	}
	void boundsRepliesAndConnections()
	{
		HttpMcpServer server; QVERIFY(server.start(0, TestToken));
		server.setHandler([](const HttpRequest&, HttpMcpServer::Reply reply) { reply({200, {}, QByteArray(8 * 1024 * 1024 + 1, 'x')}); });
		QVERIFY(request(server.port(), "POST /mcp HTTP/1.1\r\nContent-Length: 2\r\n\r\n{}").startsWith("HTTP/1.1 503"));
		QTRY_COMPARE(server.connectionCount(), 0);
		std::vector<std::unique_ptr<QTcpSocket>> sockets;
		for (int i = 0; i < 34; ++i)
		{
			auto socket = std::make_unique<QTcpSocket>(); socket->connectToHost(QHostAddress::LocalHost, server.port());
			sockets.push_back(std::move(socket));
		}
		QTRY_COMPARE(server.connectionCount(), 32);
		server.stop(); QTRY_COMPARE(server.connectionCount(), 0);
		for (const auto& socket : sockets) { QTRY_COMPARE(socket->state(), QAbstractSocket::UnconnectedState); }
	}
	void rejectsAmbiguousOrOversizedRequests()
	{
		HttpMcpServer server;
		QVERIFY(server.start(0, TestToken));
		for (const auto& wire : {
			QByteArray("POST /mcp HTTP/1.1\r\nContent-Length: 2\r\nContent-Length: 2\r\n\r\n{}"),
			QByteArray("POST /mcp HTTP/1.1\r\nContent-Length: -1\r\n\r\n"),
			QByteArray("POST /mcp HTTP/1.1\r\nTransfer-Encoding: chunked\r\n\r\n0\r\n\r\n"),
			QByteArray("POST /mcp HTTP/1.1\r\nContent-Length: 0\r\n\r\nextra")})
		{
			QVERIFY(request(server.port(), wire).startsWith("HTTP/1.1 400"));
		}
		QVERIFY(request(server.port(), "POST /mcp HTTP/1.1\r\nContent-Length: 4194305\r\n\r\n").startsWith("HTTP/1.1 413"));
		QVERIFY(request(server.port(), QByteArray("POST /mcp HTTP/1.1\r\nX-Huge: ") + QByteArray(17000, 'x')).startsWith("HTTP/1.1 431"));
	}
	void closesPartialConnectionsAndIgnoresStaleReplies()
	{
		HttpMcpServer server;
		HttpMcpServer::Reply reply;
		server.setHandler([&](const HttpRequest&, HttpMcpServer::Reply callback) { reply = std::move(callback); });
		QVERIFY(server.start(0, TestToken));
		QTcpSocket partial;
		partial.connectToHost(QHostAddress::LocalHost, server.port());
		QTRY_COMPARE(partial.state(), QAbstractSocket::ConnectedState);
		partial.write("POST /mcp HTTP/1.1\r\nContent-Length: 100\r\n\r\n{");
		QTRY_COMPARE(server.connectionCount(), 1);
		server.stop();
		QTRY_COMPARE(partial.state(), QAbstractSocket::UnconnectedState);
		QVERIFY(server.start(0, TestToken));
		QTcpSocket pending;
		pending.connectToHost(QHostAddress::LocalHost, server.port());
		QTRY_COMPARE(pending.state(), QAbstractSocket::ConnectedState);
		pending.write(authorized(server.port(), "POST /mcp HTTP/1.1\r\nContent-Length: 2\r\n\r\n{}"));
		QTRY_VERIFY(bool(reply));
		server.stop();
		QVERIFY(server.start(0, TestToken));
		reply({200, {}, "{}"});
		QCOMPARE(server.connectionCount(), 0);
		QTRY_COMPARE(pending.state(), QAbstractSocket::UnconnectedState);
	}
	void destroysOwnedListenerAndLateCallbacksSafely()
	{
		HttpMcpServer::Reply reply;
		auto server = std::make_unique<HttpMcpServer>();
		server->setHandler([&](const HttpRequest&, HttpMcpServer::Reply callback) { reply = std::move(callback); });
		QVERIFY(server->start(0, TestToken));
		const int port = server->port();
		QTcpSocket pending;
		pending.connectToHost(QHostAddress::LocalHost, port);
		QTRY_COMPARE(pending.state(), QAbstractSocket::ConnectedState);
		pending.write(authorized(port, "POST /mcp HTTP/1.1\r\nContent-Length: 2\r\n\r\n{}"));
		QTRY_VERIFY(bool(reply));
		server.reset();
		reply({200, {}, "{}"});
		QTRY_COMPARE(pending.state(), QAbstractSocket::UnconnectedState);
		QTcpServer reused;
		QVERIFY(reused.listen(QHostAddress::LocalHost, port));
		auto& global = service();
		QVERIFY(!global.isRunning());
		QVERIFY(global.start(0, TestToken));
		QPointer<HttpMcpServer> guard(&global);
		shutdownService();
		QVERIFY(guard.isNull());
	}
};
QTEST_GUILESS_MAIN(McpServerLifecycleTest)
#include "McpServerLifecycleTest.moc"
