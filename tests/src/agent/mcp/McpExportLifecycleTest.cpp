#include <QtTest>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QFileInfo>
#include "agent/mcp/McpProtocol.h"
#include "agent/CommandBus.h"
#include "agent/ExportCommands.h"
#include "Engine.h"
#include "Song.h"
using namespace lmms;
using namespace lmms::agent;
using namespace lmms::agent::mcp;
class McpExportLifecycleTest : public QObject
{
	Q_OBJECT
private slots:
	void initTestCase() { Engine::init(true); }
	void cleanupTestCase() { Engine::destroy(); }
	void cancelsOwnedRenderButPreservesLocalRender()
	{
		QTemporaryDir output;
		QVERIFY(output.isValid());
		HttpMcpServer server;
		auto* protocol = new McpProtocol(server);
		QVERIFY(server.start(0, "test-only-token-0123456789"));
		QByteArray session;
		const auto send = [&](const QJsonObject& message, HttpMcpServer::Reply reply) {
			protocol->handle({"POST", "/mcp",
								 {{"content-type", "application/json"},
									 {"accept", "application/json, text/event-stream"}, {"mcp-session-id", session}},
								 QJsonDocument(message).toJson(QJsonDocument::Compact)},
				std::move(reply));
		};
		const auto initialize = [&] {
			send({{"jsonrpc", "2.0"}, {"id", 1}, {"method", "initialize"},
					 {"params",
						 QJsonObject{{"protocolVersion", McpProtocol::version()}, {"capabilities", QJsonObject{}},
							 {"clientInfo", QJsonObject{{"name", "test"}, {"version", "1"}}}}}},
				[&](HttpResponse response) { session = response.headers.value("MCP-Session-Id"); });
			send({{"jsonrpc", "2.0"}, {"method", "notifications/initialized"}}, [](HttpResponse) {});
		};
		initialize();
		const QJsonObject args{
			{"path", output.filePath("owned.wav")}, {"range", QJsonObject{{"start", 0}, {"end", 192 * 4096}}}};
		QString task;
		send({{"jsonrpc", "2.0"}, {"id", 2}, {"method", "tools/call"},
				 {"params", QJsonObject{{"name", "export.audio"}, {"arguments", args}}}},
			[&](HttpResponse response) {
				const auto value = QJsonDocument::fromJson(response.body)
									   .object()
									   .value("result")
									   .toObject()
									   .value("structuredContent")
									   .toObject();
				QVERIFY2(value.value("ok").toBool(), response.body.constData());
				task = value.value("data").toObject().value("task").toString();
				server.stop();
			});
		QTRY_VERIFY(!task.isEmpty());
		QCOMPARE(CommandBus::instance().execute("export.status", {{"task", task}}).data.value("status").toString(),
			QString("cancelled"));
		QVERIFY(!hasActiveAudioExport());
		QVERIFY(!Engine::getSong()->isExporting());
		QVERIFY(!QFileInfo::exists(output.filePath("owned.wav")));
		QVERIFY(server.start(0, "test-only-token-0123456789"));
		auto localArgs = args;
		localArgs.insert("path", output.filePath("local.wav"));
		const auto local = CommandBus::instance().execute("export.audio", localArgs);
		QVERIFY(local.ok);
		initialize();
		bool queried = false;
		send({{"jsonrpc", "2.0"}, {"id", 3}, {"method", "tools/call"},
				 {"params",
					 QJsonObject{
						 {"name", "export.status"}, {"arguments", QJsonObject{{"task", local.data.value("task")}}}}}},
			[&](HttpResponse response) {
				QVERIFY(response.body.contains("structuredContent"));
				queried = true;
			});
		QTRY_VERIFY(queried);
		server.stop();
		QVERIFY(hasActiveAudioExport());
		const auto cancelled = CommandBus::instance().execute("export.cancel", {{"task", local.data.value("task")}});
		QVERIFY(cancelled.ok);
		QVERIFY(!hasActiveAudioExport());
		QVERIFY(!QFileInfo::exists(output.filePath("local.wav")));
	}
};
QTEST_GUILESS_MAIN(McpExportLifecycleTest)
#include "McpExportLifecycleTest.moc"
