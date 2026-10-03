#include <QtTest>
#include <QEventLoop>
#include <QJsonDocument>
#include <QTcpSocket>
#include <QTimer>
#include "agent/mcp/McpProtocol.h"
#include "agent/ToolRegistry.h"

using namespace lmms::agent::mcp;
class McpProtocolTest : public QObject
{
	Q_OBJECT
	HttpMcpServer server;
	QByteArray session;
	QByteArray exchange(const QByteArray& body, QByteArray extra = {}, QByteArray method = "POST")
	{
		QTcpSocket socket;
		QEventLoop loop;
		QTimer deadline; deadline.setSingleShot(true);
		connect(&deadline, &QTimer::timeout, &loop, &QEventLoop::quit);
		connect(&socket, &QTcpSocket::connected, &loop, [&] {
			socket.write(method + " /mcp HTTP/1.1\r\nHost: localhost\r\nContent-Type: application/json\r\n"
				"Accept: application/json, text/event-stream\r\n" +
				(session.isEmpty() ? QByteArray() : "MCP-Session-Id: " + session + "\r\n") + extra +
				"Content-Length: " + QByteArray::number(body.size()) + "\r\n\r\n" + body);
		});
		connect(&socket, &QTcpSocket::disconnected, &loop, &QEventLoop::quit);
		socket.connectToHost("127.0.0.1", server.port()); deadline.start(2000); loop.exec(); return socket.readAll();
	}
	QJsonObject body(const QByteArray& response) { return QJsonDocument::fromJson(response.mid(response.indexOf("\r\n\r\n") + 4)).object(); }
	QByteArray initialize()
	{
		session.clear();
		const auto response = exchange(R"({"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"future","capabilities":{},"clientInfo":{"name":"test","version":"1"}}})");
		for (const auto& line : response.split('\n'))
		{
			if (line.startsWith("MCP-Session-Id: ")) { session = line.mid(16).trimmed(); }
		}
		return response;
	}
private slots:
	void initTestCase() { new McpProtocol(server); QVERIFY(server.start()); }
	void discoveryLifecycle()
	{
		const auto response = initialize();
		QVERIFY(!session.isEmpty());
		const auto result = body(response).value("result").toObject();
		QCOMPARE(result.value("protocolVersion").toString(), McpProtocol::version());
		QCOMPARE(result.value("capabilities").toObject().keys(), QStringList{"tools"});
		QVERIFY(body(exchange(R"({"jsonrpc":"2.0","id":2,"method":"tools/list"})")).contains("error"));
		const auto initialized = exchange(R"({"jsonrpc":"2.0","method":"notifications/initialized"})");
		QVERIFY(initialized.startsWith("HTTP/1.1 202"));
		QVERIFY(initialized.endsWith("\r\n\r\n"));
		const auto listed = body(exchange(R"({"jsonrpc":"2.0","id":"tools","method":"tools/list"})"));
		QCOMPARE(listed.value("id").toString(), QString("tools"));
		QCOMPARE(listed.value("result").toObject().value("tools").toArray(), lmms::agent::ToolRegistry::tools());
		QVERIFY(exchange({}, {}, "GET").startsWith("HTTP/1.1 405"));
		QVERIFY(body(exchange(R"({"jsonrpc":"2.0","id":3,"method":"ping"})")).contains("result"));
	}
	void errorsAndInvalidSessions()
	{
		QCOMPARE(body(exchange("{")).value("error").toObject().value("code").toInt(), -32700);
		for (const auto& input : {QByteArray("[]"), QByteArray(R"({"jsonrpc":"1.0","id":1,"method":"ping"})"),
			QByteArray(R"({"jsonrpc":"2.0","id":null,"method":"ping"})"), QByteArray(R"({"jsonrpc":"2.0","id":1,"method":"ping","params":[]})")})
		{
			QVERIFY(exchange(input).startsWith("HTTP/1.1 400"));
		}
		QVERIFY(exchange(R"({"jsonrpc":"2.0","id":1,"method":"ping"})", "MCP-Protocol-Version: wrong\r\n").startsWith("HTTP/1.1 400"));
		QCOMPARE(body(exchange(R"({"jsonrpc":"2.0","id":9,"method":"unknown"})")).value("error").toObject().value("code").toInt(), -32601);
		const auto old = session;
		initialize();
		session = old;
		QVERIFY(exchange(R"({"jsonrpc":"2.0","id":1,"method":"ping"})").startsWith("HTTP/1.1 404"));
		session.clear();
		QVERIFY(exchange(R"({"jsonrpc":"2.0","id":1,"method":"ping"})").startsWith("HTTP/1.1 400"));
	}
	void stopInvalidatesSession()
	{
		initialize();
		server.stop(); QVERIFY(server.start());
		QVERIFY(exchange(R"({"jsonrpc":"2.0","id":1,"method":"ping"})").startsWith("HTTP/1.1 404"));
	}
};
QTEST_GUILESS_MAIN(McpProtocolTest)
#include "McpProtocolTest.moc"
