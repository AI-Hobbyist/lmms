#include <QtTest>
#include <QJsonDocument>
#include <QThread>
#include "agent/mcp/McpProtocol.h"
#include "agent/CommandBus.h"
using namespace lmms::agent;
using namespace lmms::agent::mcp;
class McpQueueTest : public QObject
{
	Q_OBJECT
	HttpMcpServer server;
	McpProtocol* protocol = nullptr;
	QByteArray session;
	QList<int> executed;
	HttpResponse dispatch(const QJsonObject& message)
	{
		HttpResponse response;
		protocol->handle(request(message), [&](HttpResponse result) { response = result; });
		return response;
	}
	HttpRequest request(const QJsonObject& message)
	{
		return {"POST", "/mcp",
			{{"content-type", "application/json"}, {"accept", "application/json, text/event-stream"},
				{"mcp-session-id", session}},
			QJsonDocument(message).toJson(QJsonDocument::Compact)};
	}
	QJsonObject call(int id)
	{
		return {{"jsonrpc", "2.0"}, {"id", id}, {"method", "tools/call"},
			{"params", QJsonObject{{"name", "test.serial"}, {"arguments", QJsonObject{{"value", id}}}}}};
	}
	void initialize()
	{
		const auto response = dispatch({{"jsonrpc", "2.0"}, {"id", "init"}, {"method", "initialize"},
			{"params",
				QJsonObject{{"protocolVersion", McpProtocol::version()}, {"capabilities", QJsonObject{}},
					{"clientInfo", QJsonObject{{"name", "test"}, {"version", "1"}}}}}});
		session = response.headers.value("MCP-Session-Id");
		dispatch({{"jsonrpc", "2.0"}, {"method", "notifications/initialized"}});
	}
private slots:
	void initTestCase()
	{
		protocol = new McpProtocol(server);
		QVERIFY(server.start(0, "test-only-token-0123456789"));
		CommandDescriptor command;
		command.name = "test.serial";
		command.summary = "Verify main thread order.";
		command.argsSchema
			= {{"type", "object"}, {"properties", QJsonObject{{"value", QJsonObject{{"type", "integer"}}}}}};
		command.handler = [this](const QJsonObject& args) {
			if (QThread::currentThread() != QCoreApplication::instance()->thread())
			{
				return CommandResult::failure("wrong_thread", "Wrong thread.");
			}
			executed.append(args.value("value").toInt());
			return CommandResult::success();
		};
		QVERIFY(CommandBus::instance().registerCommand(command));
	}
	void serializesAndBoundsPendingWork()
	{
		initialize();
		executed.clear();
		int replies = 0;
		for (int i = 0; i < 16; ++i)
		{
			protocol->handle(request(call(i)), [&](HttpResponse response) {
				QVERIFY(response.body.contains("structuredContent"));
				++replies;
			});
		}
		QVERIFY(executed.isEmpty());
		QCOMPARE(dispatch(call(17)).status, 503);
		QCOMPARE(dispatch(call(0)).status, 200);
		QTRY_COMPARE(replies, 16);
		for (int i = 0; i < 16; ++i)
		{
			QCOMPARE(executed[i], i);
		}
	}
	void cancelsQueuedWorkAndStopsBeforeMutation()
	{
		initialize();
		executed.clear();
		int replies = 0;
		protocol->handle(request(call(1)), [&](HttpResponse response) {
			QVERIFY(response.body.contains("-32800"));
			++replies;
		});
		dispatch(
			{{"jsonrpc", "2.0"}, {"method", "notifications/cancelled"}, {"params", QJsonObject{{"requestId", 1}}}});
		QCOMPARE(replies, 1);
		QCoreApplication::processEvents();
		QVERIFY(executed.isEmpty());
		protocol->handle(request(call(2)), [&](HttpResponse response) {
			QCOMPARE(response.status, 503);
			++replies;
		});
		server.stop();
		QCoreApplication::processEvents();
		QCOMPARE(replies, 2);
		QVERIFY(executed.isEmpty());
	}
};
QTEST_GUILESS_MAIN(McpQueueTest)
#include "McpQueueTest.moc"
