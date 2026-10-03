#include <QtTest>
#include <QEventLoop>
#include <QTcpSocket>
#include <QTcpServer>
#include <QHostAddress>
#include <QTimer>
#include "agent/mcp/HttpMcpServer.h"
#include "ConfigManager.h"
using namespace lmms::agent::mcp;
namespace { const QByteArray Token = "test-only-token-0123456789"; }
class McpSecurityTest : public QObject
{
	Q_OBJECT
	QByteArray exchange(HttpMcpServer& server, QByteArray host, QByteArray auth, QByteArray origin = {}, QByteArray method = "GET")
	{
		QTcpSocket socket; QEventLoop loop; QTimer timer; timer.setSingleShot(true);
		connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
		connect(&socket, &QTcpSocket::disconnected, &loop, &QEventLoop::quit);
		connect(&socket, &QTcpSocket::connected, &loop, [&] {
			socket.write(method + " /mcp HTTP/1.1\r\nHost: " + host + "\r\nAuthorization: " + auth + "\r\n" +
				(origin.isNull() ? QByteArray() : "Origin: " + origin + "\r\n") + "Content-Length: 0\r\n\r\n");
		});
		socket.connectToHost("127.0.0.1", server.port()); timer.start(2000); loop.exec(); return socket.readAll();
	}
private slots:
	void refusesMissingAndInvalidTokenBeforeListening()
	{
		HttpMcpServer server;
		for (const auto& token : {QByteArray(), QByteArray("short"), QByteArray(513, 'a'), QByteArray("a long token with spaces")})
		{
			QVERIFY(!server.start(0, token)); QVERIFY(!server.isRunning()); QVERIFY(!server.errorString().isEmpty());
		}
		QVERIFY(server.start(0, Token));
	}
	void validatesEveryHttpMethod()
	{
		HttpMcpServer server; QVERIFY(server.start(0, Token));
		const auto host = "127.0.0.1:" + QByteArray::number(server.port());
		const auto auth = "Bearer " + Token;
		for (const auto& method : {QByteArray("GET"), QByteArray("POST"), QByteArray("DELETE")})
		{
			QVERIFY(exchange(server, "attacker.example", auth, {}, method).startsWith("HTTP/1.1 403"));
			QVERIFY(exchange(server, host, auth, "https://attacker.example", method).startsWith("HTTP/1.1 403"));
			QVERIFY(exchange(server, host, auth, "null", method).startsWith("HTTP/1.1 403"));
			QVERIFY(exchange(server, host, {}, {}, method).startsWith("HTTP/1.1 401"));
			const auto rejected = exchange(server, host, "Bearer wrong-token-0123456789", {}, method);
			QVERIFY(rejected.startsWith("HTTP/1.1 401")); QVERIFY(!rejected.contains(Token));
		}
		QVERIFY(exchange(server, host, auth).startsWith("HTTP/1.1 405"));
		QVERIFY(exchange(server, host, "bearer " + Token).startsWith("HTTP/1.1 405"));
		QVERIFY(exchange(server, host, auth, "http://" + host).startsWith("HTTP/1.1 405"));
		QVERIFY(exchange(server, host, auth, "").startsWith("HTTP/1.1 403"));
		QVERIFY(exchange(server, "localhost:" + QByteArray::number(server.port()), auth).startsWith("HTTP/1.1 405"));
	}
	void appliesLocalConfigurationAndReleasesPort()
	{
		auto* config = lmms::ConfigManager::inst();
		config->setValue("agentMcp", "enabled", "false");
		QVERIFY(applyConfiguration()); QVERIFY(!service().isRunning());
		config->setValue("agentMcp", "enabled", "true");
		config->setValue("agentMcp", "tokenEnv", "LMMS_MCP_TEST_TOKEN");
		qunsetenv("LMMS_MCP_TEST_TOKEN");
		QVERIFY(!applyConfiguration()); QVERIFY(!service().isRunning());
		qputenv("LMMS_MCP_TEST_TOKEN", Token);
		config->setValue("agentMcp", "port", "bad"); QVERIFY(!applyConfiguration());
		config->setValue("agentMcp", "port", "0"); QVERIFY(applyConfiguration());
		const auto port = service().port();
		config->setValue("agentMcp", "enabled", "false"); QVERIFY(applyConfiguration());
		QTcpServer reuse; QVERIFY(reuse.listen(QHostAddress::LocalHost, port));
		QVERIFY(!service().isRunning());
		config->setValue("agentMcp", "port", QString::number(port));
		config->setValue("agentMcp", "enabled", "true"); QVERIFY(!applyConfiguration());
		QVERIFY(!service().errorString().isEmpty());
		reuse.close(); QVERIFY(applyConfiguration());
		shutdownService(); qunsetenv("LMMS_MCP_TEST_TOKEN");
	}
};
QTEST_GUILESS_MAIN(McpSecurityTest)
#include "McpSecurityTest.moc"
