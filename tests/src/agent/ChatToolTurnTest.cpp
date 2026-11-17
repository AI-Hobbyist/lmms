#include <QAbstractButton>
#include <QApplication>
#include <QCryptographicHash>
#include <QDataStream>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QLabel>
#include <QListWidget>
#include <QMainWindow>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QScopeGuard>
#include <QScreen>
#include <QScrollBar>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTextEdit>
#include <QTimer>
#include <QToolButton>
#include <QWidget>
#include <QtTest>
#include <functional>

#include "ConfigManager.h"
#include "Engine.h"
#include "LmmsPalette.h"
#include "LmmsStyle.h"
#include "Song.h"
#include "Track.h"
#include "agent/CommandBus.h"
#include "agent/chat/ChatController.h"
#include "agent/chat/ChatDock.h"
#include "agent/chat/ChatDownloadTask.h"
#include "agent/chat/ChatPortableTools.h"
#include "agent/chat/ChatSearchSettings.h"
#include "agent/chat/ChatTools.h"
#include "agent/chat/ChatTranscript.h"
#include "agent/chat/ChatWebSearch.h"

using namespace lmms::agent;
using namespace lmms::agent::chat;

class ToolTurnServer : public QObject
{
public:
	QTcpServer server;
	QList<QJsonObject> responses;
	QList<QJsonObject> requests;
	// CommandBus batch depth observed while each model request was in flight.
	QList<int> batchDepths;
	// Request headers (request line first) of every received request.
	QList<QByteArray> heads;
	// Complete raw HTTP responses, used before the JSON responses when present.
	QList<QByteArray> rawResponses;
	// Keep requests open without answering, to simulate a slow search.
	bool hold = false;

	ToolTurnServer()
	{
		connect(&server, &QTcpServer::newConnection, this, [this] {
			auto* socket = server.nextPendingConnection();
			connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
			connect(socket, &QTcpSocket::readyRead, socket, [this, socket] {
				auto bytes = socket->property("request").toByteArray() + socket->readAll();
				socket->setProperty("request", bytes);
				const auto separator = bytes.indexOf("\r\n\r\n");
				if (separator < 0 || socket->property("responded").toBool())
				{
					return;
				}
				int length = 0;
				for (const auto& line : bytes.left(separator).split('\n'))
				{
					if (line.toLower().startsWith("content-length:"))
					{
						length = line.mid(line.indexOf(':') + 1).trimmed().toInt();
					}
				}
				if (bytes.size() < separator + 4 + length)
				{
					return;
				}
				socket->setProperty("responded", true);
				requests.append(QJsonDocument::fromJson(bytes.mid(separator + 4, length)).object());
				batchDepths.append(CommandBus::instance().batchDepth());
				heads.append(bytes.left(separator));
				if (hold) { return; }
				if (!rawResponses.isEmpty())
				{
					socket->write(rawResponses.takeFirst());
					socket->disconnectFromHost();
					return;
				}
				const auto body = QJsonDocument(responses.isEmpty() ? QJsonObject{} : responses.takeFirst()).toJson();
				socket->write("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nConnection: close\r\n\r\n" + body);
				socket->disconnectFromHost();
			});
		});
	}

	ChatConnection connection() const
	{
		auto connection = ChatConnection::create();
		connection.baseUrl = QStringLiteral("http://127.0.0.1:%1/v1").arg(server.serverPort());
		connection.model = "tool-fixture";
		return connection;
	}

	static QJsonObject response(const QJsonObject& message, const QString& reason)
	{ return {{"choices", QJsonArray{QJsonObject{{"message", message}, {"finish_reason", reason}}}}}; }
};

class ChatToolTurnTest : public QObject
{
	Q_OBJECT
private:
	QWidget m_window;
	QString m_originalApproval;
	ChatToolCatalog m_catalog;

	QJsonObject renameCall(const QString& id, const QString& name) const
	{
		const auto arguments = QJsonDocument(QJsonObject{{"track", 0}, {"value", name}}).toJson(QJsonDocument::Compact);
		return {{"id", id},
				{"type", "function"},
				{"function",
				 QJsonObject{{"name", m_catalog.nameForCommand("track.setName")},
							 {"arguments", QString::fromUtf8(arguments)}}}};
	}

	static QStringList toolNames(const QJsonObject& request)
	{
		QStringList names;
		for (const auto& value : request.value("tools").toArray())
		{
			names.append(value.toObject().value("function").toObject().value("name").toString());
		}
		return names;
	}

	static QString searchWireName()
	{
		ChatToolCatalog catalog;
		QString error;
		catalog.load(error, {ChatWebSearch::definition()});
		return catalog.nameForCommand(ChatWebSearch::command());
	}

	static QJsonObject searchCall(const QString& id, const QString& query, int count)
	{
		const QJsonObject arguments{{"query", query}, {"count", count}};
		return {{"id", id}, {"type", "function"},
			{"function",
				QJsonObject{{"name", searchWireName()},
					{"arguments", QString::fromUtf8(QJsonDocument(arguments).toJson(QJsonDocument::Compact))}}}};
	}

	static void enableSearch(const QString& url)
	{
		ChatSearchSettings settings;
		settings.enabled = true;
		settings.searxngUrl = url;
		settings.save();
	}

	// Runs one approved search call and returns the tool result sent back to the model.
	QJsonObject runSingleSearch(
		const std::function<void()>& beforeDecide = {}, const std::function<void()>& afterDecide = {})
	{ return runSingleCall(searchCall("find", "drums", 3), beforeDecide, afterDecide); }

	// Runs one approved chat tool call and returns the tool result sent back to the model.
	QJsonObject runSingleCall(const QJsonObject& call, const std::function<void()>& beforeDecide = {},
		const std::function<void()>& afterDecide = {})
	{
		ToolTurnServer model;
		if (!model.server.listen(QHostAddress::LocalHost)) { return {}; }
		queueCalls(model, {call});
		QTemporaryDir directory;
		ChatStore store(directory.path());
		auto session = ChatStore::newSession();
		QString error;
		store.saveSession(session, error);
		ChatController controller(store);
		controller.setToolsEnabled(true);
		QSignalSpy approvals(&controller, &ChatController::approvalRequested);
		controller.send(session.value("id").toString(), model.connection(), "fixture-key", {}, "Search", error);
		if (!QTest::qWaitFor([&] { return approvals.size() == 1; }, 5000)) { return {}; }
		if (beforeDecide) { beforeDecide(); }
		controller.decideTool(approvals[0][0].toJsonObject(), true, error);
		if (afterDecide) { afterDecide(); }
		if (!QTest::qWaitFor([&] { return !controller.active(); }, 10000) || model.requests.size() != 2) { return {}; }
		const auto history = model.requests[1].value("messages").toArray();
		return QJsonDocument::fromJson(history.last().toObject().value("content").toString().toUtf8()).object();
	}

	void queueCalls(ToolTurnServer& server, const QJsonArray& calls)
	{
		server.responses.append(ToolTurnServer::response({{"role", "assistant"}, {"tool_calls", calls}}, "tool_calls"));
		server.responses.append(ToolTurnServer::response({{"role", "assistant"}, {"content", "Done"}}, "stop"));
	}

	QJsonObject protocolToolResponse(const QString& protocol, const QString& id, const QString& value) const
	{
		const auto name = m_catalog.nameForCommand("track.setName");
		const QJsonObject arguments{{"track", 0}, {"value", value}};
		if (protocol == "gemini")
		{
			const QJsonObject part{{"functionCall", QJsonObject{{"name", name}, {"args", arguments}}},
								   {"thoughtSignature", "opaque-" + id}};
			return {{"candidates",
					 QJsonArray{QJsonObject{{"content", QJsonObject{{"parts", QJsonArray{part}}}},
											{"finishReason", "STOP"}}}}};
		}
		if (protocol == "anthropic-messages")
		{
			return {{"content",
					 QJsonArray{QJsonObject{{"type", "thinking"}, {"thinking", "Plan"}, {"signature", "opaque-" + id}},
								QJsonObject{{"type", "tool_use"}, {"id", id}, {"name", name}, {"input", arguments}}}},
					{"stop_reason", "tool_use"}};
		}
		if (protocol.endsWith("responses"))
		{
			return {{"status", "completed"},
					{"output",
					 QJsonArray{
						 QJsonObject{
							 {"type", "reasoning"}, {"encrypted_content", "opaque-" + id}, {"summary", QJsonArray{}}},
						 QJsonObject{{"type", "function_call"},
									 {"call_id", id},
									 {"name", name},
									 {"arguments",
									  QString::fromUtf8(QJsonDocument(arguments).toJson(QJsonDocument::Compact))}}}}};
		}
		if (protocol == "custom-json")
		{
			return {{"answer", ""},
					{"calls", QJsonArray{QJsonObject{{"id", id}, {"name", name}, {"arguments", arguments}}}}};
		}
		return ToolTurnServer::response({{"tool_calls", QJsonArray{renameCall(id, value)}}}, "tool_calls");
	}

	static QJsonObject protocolTextResponse(const QString& protocol)
	{
		const QJsonObject text{{"text", "Done"}};
		if (protocol == "gemini")
		{
			const QJsonObject content{{"parts", QJsonArray{text}}};
			const QJsonObject candidate{{"content", content}, {"finishReason", "STOP"}};
			return {{"candidates", QJsonArray{candidate}}};
		}
		if (protocol == "anthropic-messages")
		{
			return {{"content", QJsonArray{QJsonObject{{"type", "text"}, {"text", "Done"}}}},
					{"stop_reason", "end_turn"}};
		}
		if (protocol.endsWith("responses"))
		{
			const QJsonArray content{QJsonObject{{"type", "output_text"}, {"text", "Done"}}};
			const QJsonObject message{{"type", "message"}, {"role", "assistant"}, {"content", content}};
			return {{"status", "completed"}, {"output", QJsonArray{message}}};
		}
		if (protocol == "custom-json")
		{
			return {{"answer", "Done"}};
		}
		return ToolTurnServer::response({{"content", "Done"}}, "stop");
	}

private slots:
	void initTestCase()
	{
		lmms::Engine::init(true);
		m_originalApproval = lmms::ConfigManager::inst()->value("agentchat", "approval", "request");
		m_window.setWindowTitle("LMMS chat tool turn validation");
		m_window.resize(480, 160);
		m_window.show();
		QVERIFY(QTest::qWaitForWindowExposed(&m_window));
		QString error;
		QVERIFY2(m_catalog.load(error), qPrintable(error));
	}

	void cleanupTestCase()
	{
		lmms::ConfigManager::inst()->setValue("agentchat", "approval", m_originalApproval);
		m_window.close();
		lmms::Engine::destroy();
	}

	void init()
	{
		lmms::Engine::getSong()->clearProject();
		lmms::ConfigManager::inst()->setValue("agentchat", "approval", "request");
		ChatSearchSettings{}.save();
		ChatDownloadSettings downloads;
		downloads.aria2Path = deployedTool("aria2/aria2c.exe");
		downloads.sevenZipPath = deployedTool("7zip/7z.exe");
		downloads.save();
		QVERIFY(CommandBus::instance().execute("track.create", {{"type", "sample"}, {"name", "Original"}}).ok);
	}

	void webSearchOfferedOnlyWhenEnabled()
	{
		ToolTurnServer server;
		QVERIFY(server.server.listen(QHostAddress::LocalHost));
		server.responses.append(ToolTurnServer::response({{"role", "assistant"}, {"content", "Done"}}, "stop"));
		server.responses.append(ToolTurnServer::response({{"role", "assistant"}, {"content", "Done"}}, "stop"));
		QTemporaryDir directory;
		ChatStore store(directory.path());
		auto session = ChatStore::newSession();
		QString error;
		QVERIFY(store.saveSession(session, error));
		ChatController controller(store);
		controller.setToolsEnabled(true);
		const auto id = session.value("id").toString();
		QVERIFY(controller.send(id, server.connection(), "fixture-key", {}, "Off", error));
		QTRY_VERIFY(!controller.active());
		QVERIFY(!toolNames(server.requests[0]).contains(searchWireName()));
		QVERIFY(toolNames(server.requests[0]).contains(m_catalog.nameForCommand("track.setName")));
		enableSearch("http://127.0.0.1:1");
		QVERIFY(controller.send(id, server.connection(), "fixture-key", {}, "On", error));
		QTRY_VERIFY(!controller.active());
		QVERIFY(toolNames(server.requests[1]).contains(searchWireName()));
		// The model-hosted source is not an external search tool.
		auto settings = ChatSearchSettings::load();
		settings.backend = ChatSearchBackend::Model;
		settings.save();
		server.responses.append(ToolTurnServer::response({{"role", "assistant"}, {"content", "Done"}}, "stop"));
		QVERIFY(controller.send(id, server.connection(), "fixture-key", {}, "Model", error));
		QTRY_VERIFY(!controller.active());
		QVERIFY(!toolNames(server.requests[2]).contains(searchWireName()));
	}

	void webSearchSearxngRoundTrip()
	{
		ToolTurnServer search;
		QVERIFY(search.server.listen(QHostAddress::LocalHost));
		search.responses.append(QJsonObject{{"results",
			QJsonArray{QJsonObject{{"title", "Lofi kit"}, {"url", "https://samples.example.org/kit"}, {"content", "A"}},
				QJsonObject{{"title", "Dusty drums"}, {"url", "https://samples.example.org/dusty"}, {"content", "B"}},
				QJsonObject{{"title", "Third"}, {"url", "https://samples.example.org/3"}, {"content", "C"}}}}});
		enableSearch(QStringLiteral("http://127.0.0.1:%1/searx").arg(search.server.serverPort()));
		ToolTurnServer model;
		QVERIFY(model.server.listen(QHostAddress::LocalHost));
		queueCalls(model, {searchCall("find", "lofi drums", 2)});
		QTemporaryDir directory;
		ChatStore store(directory.path());
		auto session = ChatStore::newSession();
		QString error;
		QVERIFY(store.saveSession(session, error));
		const auto id = session.value("id").toString();
		ChatController controller(store);
		controller.setToolsEnabled(true);
		QSignalSpy approvals(&controller, &ChatController::approvalRequested);
		QVERIFY(controller.send(id, model.connection(), "fixture-key", {}, "Search", error));
		QTRY_COMPARE(approvals.size(), 1);

		// Approval shows the query and source before anything is sent.
		const auto approval = approvals[0][0].toJsonObject();
		QCOMPARE(approval.value("command").toString(), QStringLiteral("chat.webSearch"));
		QCOMPARE(approval.value("targets").toArray()[0].toObject().value("name").toString(),
			QStringLiteral("searxng · lofi drums"));
		QVERIFY(search.heads.isEmpty());
		QVERIFY(controller.decideTool(approval, true, error));
		QTRY_VERIFY(!controller.active());

		QCOMPARE(search.heads.size(), 1);
		QVERIFY2(
			search.heads[0].startsWith("GET /searx/search?q=lofi%20drums&format=json"), search.heads[0].constData());
		QCOMPARE(search.batchDepths, QList<int>{0});
		QCOMPARE(model.requests.size(), 2);
		const auto history = model.requests[1].value("messages").toArray();
		const auto result
			= QJsonDocument::fromJson(history.last().toObject().value("content").toString().toUtf8()).object();
		QVERIFY(result.value("ok").toBool());
		const auto results = result.value("data").toObject().value("results").toArray();
		QCOMPARE(results.size(), 2);
		QCOMPARE(results[0].toObject().value("url").toString(), QStringLiteral("https://samples.example.org/kit"));
		QCOMPARE(lmms::Engine::getSong()->tracks()[0]->name(), QStringLiteral("Original"));
	}

	void webSearchSearxngWithoutJsonExplainsFix()
	{
		ToolTurnServer search;
		QVERIFY(search.server.listen(QHostAddress::LocalHost));
		search.rawResponses.append("HTTP/1.1 403 Forbidden\r\nContent-Type: text/html\r\nConnection: close\r\n\r\n"
								   "<html>Forbidden</html>");
		enableSearch(QStringLiteral("http://127.0.0.1:%1").arg(search.server.serverPort()));
		const auto result = runSingleSearch();
		QCOMPARE(result.value("error").toObject().value("code").toString(), QStringLiteral("searxng_json_unavailable"));
		QVERIFY(result.value("error").toObject().value("hint").toString().contains("json"));
	}

	void webSearchRefusedAfterTurnedOff()
	{
		ToolTurnServer search;
		QVERIFY(search.server.listen(QHostAddress::LocalHost));
		enableSearch(QStringLiteral("http://127.0.0.1:%1").arg(search.server.serverPort()));
		const auto result = runSingleSearch([] { ChatSearchSettings{}.save(); });
		QCOMPARE(result.value("error").toObject().value("code").toString(), QStringLiteral("web_search_disabled"));
		QVERIFY(search.heads.isEmpty());
	}

	void webSearchBraveUsesStoredKey()
	{
		ToolTurnServer search;
		QVERIFY(search.server.listen(QHostAddress::LocalHost));
		search.responses.append(QJsonObject{{"web",
			QJsonObject{{"results",
				QJsonArray{QJsonObject{
					{"title", "Brave kit"}, {"url", "https://samples.example.org/brave"}, {"description", "D"}}}}}}});
		ChatSearchSettings settings;
		settings.enabled = true;
		settings.backend = ChatSearchBackend::Brave;
		settings.braveUrl = QStringLiteral("http://127.0.0.1:%1/res/v1/web/search").arg(search.server.serverPort());
		settings.save();
		const ChatConnectionStore keys(
			QDir(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)).filePath("agent"));
		QString error;
		QVERIFY(keys.removeKey(ChatSearchSettings::braveKeyId(), error));

		// Without a key the call is refused before any request is sent.
		const auto missing = runSingleSearch();
		QCOMPARE(missing.value("error").toObject().value("code").toString(), QStringLiteral("search_not_configured"));
		QVERIFY(search.heads.isEmpty());

		QVERIFY2(keys.writeKey(ChatSearchSettings::braveKeyId(), "brave-fixture-key", error), qPrintable(error));
		const auto removeKey = qScopeGuard([&] { keys.removeKey(ChatSearchSettings::braveKeyId(), error); });
		const auto result = runSingleSearch();
		QVERIFY(result.value("ok").toBool());
		QCOMPARE(result.value("data").toObject().value("results").toArray()[0].toObject().value("snippet").toString(),
			QStringLiteral("D"));
		QCOMPARE(search.heads.size(), 1);
		QVERIFY(search.heads[0].startsWith("GET /res/v1/web/search?q=drums&count=3"));
		QVERIFY(search.heads[0].contains("X-Subscription-Token: brave-fixture-key"));
	}

	void webSearchCanceledWhenTurnedOff()
	{
		ToolTurnServer search;
		QVERIFY(search.server.listen(QHostAddress::LocalHost));
		search.hold = true;
		enableSearch(QStringLiteral("http://127.0.0.1:%1").arg(search.server.serverPort()));
		const auto result = runSingleSearch({}, [&] {
			QTRY_COMPARE(search.heads.size(), 1);
			ChatSearchSettings{}.save();
		});
		QCOMPARE(result.value("error").toObject().value("code").toString(), QStringLiteral("web_search_disabled"));
	}

	void protocolMultipleRounds_data()
	{
		QTest::addColumn<QString>("protocol");
		for (const auto& protocol : ChatConnection::protocols())
		{
			QTest::newRow(qPrintable(protocol)) << protocol;
		}
	}

	void protocolMultipleRounds()
	{
		QFETCH(QString, protocol);
		ToolTurnServer server;
		QVERIFY(server.server.listen(QHostAddress::LocalHost));
		server.responses = {protocolToolResponse(protocol, "a", "First"), protocolToolResponse(protocol, "b", "Second"),
							protocolTextResponse(protocol)};
		auto connection = server.connection();
		connection.protocol = protocol;
		connection.mapping.insert("stream", false);
		if (protocol == "custom-json")
		{
			connection.mapping = {{"stream", false},
								  {"path", "generate"},
								  {"responseTextPath", "answer"},
								  {"responseToolCallsPath", "calls"},
								  {"toolHistoryFormat", "openai-chat"},
								  {"toolFormat", "openai-chat"}};
		}
		QTemporaryDir directory;
		ChatStore store(directory.path());
		auto session = ChatStore::newSession();
		const auto sessionId = session.value("id").toString();
		QString error;
		QVERIFY(store.saveSession(session, error));
		ChatController controller(store);
		controller.setToolsEnabled(true);
		QSignalSpy approvals(&controller, &ChatController::approvalRequested);
		QVERIFY2(controller.send(sessionId, connection, "fixture-key", {}, "Rename twice", error), qPrintable(error));
		QTRY_COMPARE(approvals.size(), 1);
		const auto first = approvals[0][0].toJsonObject();
		QVERIFY(controller.decideTool(first, true, error));
		QTRY_COMPARE(approvals.size(), 2);
		QCOMPARE(lmms::Engine::getSong()->tracks()[0]->name(), QStringLiteral("First"));
		const auto second = approvals[1][0].toJsonObject();
		QVERIFY(first.value("id") != second.value("id"));
		QVERIFY(controller.decideTool(second, true, error));
		QTRY_VERIFY(!controller.active());
		QCOMPARE(server.requests.size(), 3);
		QCOMPARE(lmms::Engine::getSong()->tracks()[0]->name(), QStringLiteral("Second"));
		QCOMPARE(controller.currentMessage().value("text").toString(), QStringLiteral("Done"));
		const auto stored = store.session(sessionId).value("messages").toArray();
		QCOMPARE(stored.size(), 6);
		QCOMPARE(stored[2].toObject().value("tool_call_id"), first.value("id"));
		QCOMPARE(stored[4].toObject().value("tool_call_id"), second.value("id"));
		const auto outbound = QJsonDocument(server.requests.last()).toJson();
		if (protocol == "gemini" || protocol == "anthropic-messages" || protocol.endsWith("responses"))
		{
			QVERIFY(outbound.contains("opaque-a"));
			QVERIFY(outbound.contains("opaque-b"));
		}
		if (protocol == "gemini")
		{
			for (const auto& content : server.requests.last().value("contents").toArray())
			{
				for (const auto& partValue : content.toObject().value("parts").toArray())
				{
					const auto part = partValue.toObject();
					for (const auto* field : {"functionCall", "functionResponse"})
					{
						if (part.contains(field))
						{
							QVERIFY(!part.value(field).toObject().contains("id"));
						}
					}
				}
			}
		}
	}

	void nativeDockApprovalAndScriptDetails()
	{
		QStandardPaths::setTestModeEnabled(true);
		const auto previousName = QCoreApplication::applicationName();
		QCoreApplication::setApplicationName("LMMS-ToolTurn-" + ChatConnection::create().id);
		const auto restoreName = qScopeGuard([&] { QCoreApplication::setApplicationName(previousName); });
		const auto profile = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
		const auto removeProfile = qScopeGuard([&] { QDir(profile).removeRecursively(); });
		QTemporaryDir config;
		lmms::ConfigManager::inst()->loadConfigFile(config.filePath("config.xml"));
		QDir::addSearchPath("resources", lmms::ConfigManager::inst()->themeDir());
		QDir::addSearchPath("resources", lmms::ConfigManager::inst()->defaultThemeDir());
		QDir::addSearchPath("artwork", lmms::ConfigManager::inst()->themeDir());
		QDir::addSearchPath("artwork", lmms::ConfigManager::inst()->defaultThemeDir());
		auto* style = new lmms::gui::LmmsStyle;
		QApplication::setStyle(style);
		lmms::gui::LmmsPalette palette(nullptr, style);
		QApplication::setPalette(palette.palette());
		lmms::ConfigManager::inst()->setValue("agentchat", "approval", "request");
		QFile prompt(config.filePath("prompt.txt"));
		QVERIFY(prompt.open(QIODevice::WriteOnly));
		prompt.write("Only use explicit context");
		prompt.close();
		lmms::ConfigManager::inst()->setValue("agentchat", "promptPath", prompt.fileName());
		ToolTurnServer server;
		QVERIFY(server.server.listen(QHostAddress::LocalHost));
		const QJsonObject script{
			{"name", "Full reviewed script"},
			{"steps",
			 QJsonArray{QJsonObject{{"cmd", "track.setName"},
									{"args", QJsonObject{{"track", 0}, {"value", "Scripted from dock"}}}}}}};
		const QJsonObject arguments{{"script", script}, {"vars", QJsonObject{{"bars", 8}}}, {"seed", 42}};
		const QJsonObject scriptCall{
			{"id", "script"},
			{"type", "function"},
			{"function",
			 QJsonObject{{"name", m_catalog.nameForCommand("agent.runScript")},
						 {"arguments", QString::fromUtf8(QJsonDocument(arguments).toJson(QJsonDocument::Compact))}}}};
		queueCalls(server, {renameCall("denied", "Never"), scriptCall});
		auto connection = server.connection();
		connection.name = "Local tools";
		ChatConnectionStore connections(QDir(profile).filePath("agent"));
		QString error;
		QVERIFY(connections.saveConnection(connection, error));
		QVERIFY(connections.writeKey(connection.id, "fixture-dock-key", error));
		const auto removeKey = qScopeGuard([&] { connections.removeKey(connection.id, error); });
		lmms::ConfigManager::inst()->setValue("agentchat", "connection", connection.id);
		QMainWindow window;
		window.resize(1600, 878);
		window.setCentralWidget(new QWidget(&window));
		lmms::ConfigManager::inst()->setValue("agentchat", "collapsed", "0");
		auto* dock = new ChatDock(&window);
		window.show();
		QVERIFY(QTest::qWaitForWindowExposed(&window));
		window.resizeDocks({dock}, {640}, Qt::Horizontal);
		auto* controller = dock->findChild<ChatController*>();
		auto* transcript = dock->findChild<ChatTranscript*>("agentMessages");
		auto* input = dock->findChild<QPlainTextEdit*>("agentInput");
		auto* send = dock->findChild<QToolButton*>("agentSend");
		QVERIFY(controller && transcript && input && send);
		QSignalSpy approvals(controller, &ChatController::approvalRequested);
		input->setPlainText("Rename using the reviewed script");
		QTest::mouseClick(send, Qt::LeftButton);
		QTRY_COMPARE(approvals.size(), 1);
		auto* card = transcript->findChild<QWidget*>("agentApprovalCard");
		QVERIFY(card && card->isVisible());
		QCOMPARE(lmms::Engine::getSong()->tracks()[0]->name(), QStringLiteral("Original"));
		QTest::mouseClick(card->findChild<QToolButton*>("agentApprovalDeny"), Qt::LeftButton);
		QTRY_COMPARE(approvals.size(), 2);
		QCOMPARE(lmms::Engine::getSong()->tracks()[0]->name(), QStringLiteral("Original"));
		QVERIFY(approvals[1][0].toJsonObject().value("arguments").toObject() == arguments);
		QTimer::singleShot(0, dock, [&] {
			auto* details = dock->findChild<QMessageBox*>("agentToolDetails");
			QVERIFY(details);
			const auto closeDetails = qScopeGuard([&] { details->done(QMessageBox::Close); });
			QVERIFY(details->detailedText().contains("Full reviewed script"));
			QVERIFY(details->detailedText().contains("track.setName"));
			QVERIFY(details->detailedText().contains("\"vars\""));
			QVERIFY(details->detailedText().contains("\"seed\": 42"));
			QVERIFY(!details->detailedText().contains("\"token\""));
			for (auto* button : details->buttons())
			{
				if (details->buttonRole(button) == QMessageBox::ActionRole)
				{
					button->click();
				}
			}
			auto* text = details->findChild<QTextEdit*>();
			QVERIFY(text && text->isVisible() && text->isReadOnly());
			QVERIFY(text->toPlainText().contains("Full reviewed script"));
		});
		QTest::mouseClick(card->findChild<QToolButton*>("agentApprovalDetails"), Qt::LeftButton);
		QVERIFY(controller->active());
		QTest::mouseClick(card->findChild<QToolButton*>("agentApprovalApprove"), Qt::LeftButton);
		QTRY_VERIFY(!controller->active());
		QCOMPARE(lmms::Engine::getSong()->tracks()[0]->name(), QStringLiteral("Scripted from dock"));
		QVERIFY(!card->isVisible());
		QCOMPARE(server.requests.size(), 2);
		const auto id = approvals[1][0].toJsonObject().value("sessionId").toString();
		ChatStore store(QDir(profile).filePath("agent/sessions"));
		QCOMPARE(store.session(id).value("messages").toArray().size(), 5);
		QVERIFY(transcript->toPlainText().contains("Done"));

		// Penpot 01 result cards: denied call stays unexecuted, approved script ran.
		QList<QFrame*> results;
		for (auto* frame : transcript->findChildren<QFrame*>())
		{
			if (frame->property("agentToolResult").toBool() && frame->isVisible())
			{
				results.append(frame);
			}
		}
		QCOMPARE(results.size(), 2);
		for (auto* result : results)
		{
			QCOMPARE(result->height(), 50);
			auto* icon = result->findChild<QLabel*>(QString(), Qt::FindDirectChildrenOnly);
			QVERIFY(icon);
			QCOMPARE(icon->geometry(), QRect(12, 16, 18, 18));
		}
		const auto plain = transcript->toPlainText();
		QVERIFY2(plain.contains("Denied · track.setName"), qPrintable(plain));
		QVERIFY(plain.contains("Approved · agent.runScript"));
		QVERIFY(plain.contains("Not executed; project unchanged"));
		QVERIFY(plain.contains("Completed"));
		QVERIFY(!plain.contains("\"ok\""));
		const auto evidence = qEnvironmentVariable("LMMS_CHAT_SCREENSHOT");
		if (!evidence.isEmpty())
		{
			transcript->verticalScrollBar()->setValue(transcript->verticalScrollBar()->maximum());
			QTest::qWait(300);
			QVERIFY(window.screen()
						->grabWindow(window.winId(), dock->x(), dock->y(), dock->width(), dock->height())
						.save(QFileInfo(evidence).absolutePath() + "/agent-chat-tool-result-native.png"));
		}
		window.close();
	}

	void sequentialApprovalAndActualResults()
	{
		ToolTurnServer server;
		QVERIFY(server.server.listen(QHostAddress::LocalHost));
		queueCalls(server, {renameCall("one", "Denied"), renameCall("two", "Approved")});
		QTemporaryDir directory;
		ChatStore store(directory.path());
		auto session = ChatStore::newSession();
		QString error;
		QVERIFY(store.saveSession(session, error));
		const auto id = session.value("id").toString();
		ChatController controller(store);
		controller.setToolsEnabled(true);
		QSignalSpy approvals(&controller, &ChatController::approvalRequested);
		QVERIFY2(controller.send(id, server.connection(), "fixture-key", "Only explicit context", "Rename", error),
				 qPrintable(error));
		QTRY_COMPARE(approvals.size(), 1);
		QVERIFY(controller.active());
		QCOMPARE(lmms::Engine::getSong()->tracks()[0]->name(), QStringLiteral("Original"));
		QVERIFY(!server.requests[0].value("tools").toArray().isEmpty());
		QCOMPARE(server.requests[0].value("messages").toArray().size(), 2);
		QVERIFY(controller.decideTool(approvals[0][0].toJsonObject(), false, error));
		QTRY_COMPARE(approvals.size(), 2);
		QCOMPARE(server.requests.size(), 1);
		QVERIFY(controller.decideTool(approvals[1][0].toJsonObject(), true, error));
		QTRY_VERIFY(!controller.active());
		QCOMPARE(lmms::Engine::getSong()->tracks()[0]->name(), QStringLiteral("Approved"));
		QCOMPARE(server.requests.size(), 2);
		// No project batch is held while waiting for the model, before or after execution.
		QCOMPARE(server.batchDepths, (QList<int>{0, 0}));
		const auto history = server.requests[1].value("messages").toArray();
		QCOMPARE(history.size(), 5);
		const auto denied
			= QJsonDocument::fromJson(history[3].toObject().value("content").toString().toUtf8()).object();
		QCOMPARE(denied.value("error").toObject().value("code").toString(), QStringLiteral("permission_denied"));
		const auto approved
			= QJsonDocument::fromJson(history[4].toObject().value("content").toString().toUtf8()).object();
		QVERIFY(approved.value("ok").toBool());
		const auto stored = store.session(id).value("messages").toArray();
		QCOMPARE(stored.size(), 5);

		// Local tool messages keep execution metadata; the provider never sees it.
		const auto deniedExecution = stored[2].toObject().value("execution").toObject();
		QVERIFY(!deniedExecution.value("command").toString().isEmpty());
		QVERIFY(deniedExecution.value("approvalRequired").toBool());
		QVERIFY(!deniedExecution.value("executed").toBool());
		const auto approvedExecution = stored[3].toObject().value("execution").toObject();
		QVERIFY(approvedExecution.value("approvalRequired").toBool());
		QVERIFY(approvedExecution.value("executed").toBool());
		QCOMPARE(approvedExecution.value("arguments").toObject(),
			approvals[1][0].toJsonObject().value("arguments").toObject());
		QVERIFY(!history[3].toObject().contains("execution"));
		QVERIFY(!history[4].toObject().contains("execution"));

		QVERIFY(!controller.decideTool(approvals[1][0].toJsonObject(), true, error));
	}

	static QJsonObject readCall(const QString& url)
	{
		ChatToolCatalog catalog;
		QString error;
		catalog.load(error, {ChatWebSearch::readDefinition()});
		const auto arguments = QJsonDocument(QJsonObject{{"url", url}}).toJson(QJsonDocument::Compact);
		return {{"id", "read"}, {"type", "function"},
			{"function",
				QJsonObject{{"name", catalog.nameForCommand(ChatWebSearch::readCommand())},
					{"arguments", QString::fromUtf8(arguments)}}}};
	}

	void readPageExtractsReadableText()
	{
		ToolTurnServer page;
		QVERIFY(page.server.listen(QHostAddress::LocalHost));
		page.rawResponses.append(
			"HTTP/1.1 200 OK\r\nContent-Type: text/html; charset=utf-8\r\nConnection: close\r\n\r\n"
			"<html><head><title>Lofi &amp; Dusty</title><style>p{color:red}</style>"
			"<script>var secret = 1;</script></head><body><h1>Kit notes</h1>"
			"<p>Tempo 80 BPM.</p><p>  </p><p>License: CC0</p></body></html>");
		enableSearch("http://127.0.0.1:1");
		const auto url = QStringLiteral("http://127.0.0.1:%1/kit").arg(page.server.serverPort());
		const auto result = runSingleCall(readCall(url));
		QVERIFY(result.value("ok").toBool());
		const auto data = result.value("data").toObject();
		QCOMPARE(data.value("url").toString(), url);
		QCOMPARE(data.value("title").toString(), QStringLiteral("Lofi & Dusty"));
		const auto text = data.value("text").toString();
		QVERIFY2(text.contains("Kit notes") && text.contains("Tempo 80 BPM.") && text.contains("License: CC0"),
			qPrintable(text));
		QVERIFY(!text.contains("secret") && !text.contains("color"));
		QVERIFY(!data.value("truncated").toBool());
		QVERIFY(page.heads[0].startsWith("GET /kit"));
	}

	void readPageRejectsNonTextAndNonHttp()
	{
		ToolTurnServer page;
		QVERIFY(page.server.listen(QHostAddress::LocalHost));
		page.rawResponses.append(
			"HTTP/1.1 200 OK\r\nContent-Type: application/zip\r\nConnection: close\r\n\r\nPK\x03\x04");
		enableSearch("http://127.0.0.1:1");
		const auto binary
			= runSingleCall(readCall(QStringLiteral("http://127.0.0.1:%1/a.zip").arg(page.server.serverPort())));
		QCOMPARE(binary.value("error").toObject().value("code").toString(), QStringLiteral("unsupported_content"));
		const auto local = runSingleCall(readCall("file:///C:/Windows/win.ini"));
		QCOMPARE(local.value("error").toObject().value("code").toString(), QStringLiteral("invalid_url"));
	}

	static bool requestsGoogleSearch(const QJsonObject& request)
	{
		for (const auto& value : request.value("tools").toArray())
		{
			if (value.toObject().contains("google_search")) { return true; }
		}
		return false;
	}

	void hostedSearchApprovedBeforeSending_data()
	{
		QTest::addColumn<QString>("approval");
		QTest::addColumn<bool>("enabled");
		QTest::addColumn<int>("decision"); // 1 approve, 0 deny, -1 no card expected
		QTest::addColumn<bool>("hosted");
		QTest::newRow("approved") << "request" << true << 1 << true;
		QTest::newRow("denied") << "request" << true << 0 << false;
		QTest::newRow("switched off") << "request" << false << -1 << false;
		QTest::newRow("no approval mode") << "none" << true << -1 << true;
	}

	void hostedSearchApprovedBeforeSending()
	{
		QFETCH(QString, approval);
		QFETCH(bool, enabled);
		QFETCH(int, decision);
		QFETCH(bool, hosted);
		lmms::ConfigManager::inst()->setValue("agentchat", "approval", approval);
		ChatSearchSettings settings;
		settings.enabled = enabled;
		settings.backend = ChatSearchBackend::Model;
		settings.save();
		ToolTurnServer server;
		QVERIFY(server.server.listen(QHostAddress::LocalHost));
		auto text = protocolTextResponse("gemini");
		auto candidates = text.value("candidates").toArray();
		auto candidate = candidates[0].toObject();
		candidate.insert("groundingMetadata",
			QJsonObject{{"groundingChunks",
				QJsonArray{QJsonObject{{"web", QJsonObject{{"uri", "https://c.example"}, {"title", "Kit C"}}}}}}});
		candidates[0] = candidate;
		text.insert("candidates", candidates);
		server.responses.append(text);
		auto connection = server.connection();
		connection.protocol = "gemini";
		connection.mapping.insert("stream", false);
		connection.capabilities.insert("hostedWebSearch", true);
		QTemporaryDir directory;
		ChatStore store(directory.path());
		auto session = ChatStore::newSession();
		QString error;
		QVERIFY(store.saveSession(session, error));
		const auto id = session.value("id").toString();
		ChatController controller(store);
		QSignalSpy approvals(&controller, &ChatController::approvalRequested);
		QVERIFY2(controller.send(id, connection, "fixture-key", {}, "Find drums", error), qPrintable(error));
		if (decision >= 0)
		{
			// Nothing is sent before the turn-scoped hosted search decision.
			QTRY_COMPARE(approvals.size(), 1);
			const auto card = approvals[0][0].toJsonObject();
			QVERIFY(card.value("hosted").toBool());
			QCOMPARE(card.value("command").toString(), QStringLiteral("model.webSearch"));
			QVERIFY(controller.active());
			QTest::qWait(100);
			QVERIFY(server.requests.isEmpty());
			QCOMPARE(controller.pendingApproval(), card);
			QVERIFY(controller.decideTool(card, decision == 1, error));
		}
		QTRY_VERIFY(!controller.active());
		QCOMPARE(approvals.size(), decision >= 0 ? 1 : 0);
		QCOMPARE(server.requests.size(), 1);
		QCOMPARE(requestsGoogleSearch(server.requests[0]), hosted);
		const auto messages = store.session(id).value("messages").toArray();
		const auto answer = messages.last().toObject();
		QCOMPARE(answer.value("status").toString(), QStringLiteral("complete"));
		QCOMPARE(answer.value("sources").toArray().size(), 1);

		// The approval belongs to that turn only: the next turn asks again.
		if (decision == 1)
		{
			server.responses.append(protocolTextResponse("gemini"));
			QVERIFY(controller.send(id, connection, "fixture-key", {}, "Again", error));
			QTRY_COMPARE(approvals.size(), 2);
			controller.stop();
			QVERIFY(!controller.active());
			QCOMPARE(server.requests.size(), 1);
		}
	}

	// Tools deployed by buildtools/Get-AgentChatTools.ps1 beside build/Release/lmms.exe.
	static QString deployedTool(const QString& relative)
	{
		return QDir::cleanPath(
			QDir(QCoreApplication::applicationDirPath()).absoluteFilePath("../../Release/tools/" + relative));
	}

	static QJsonObject downloadCall(const QJsonObject& arguments)
	{
		ChatToolCatalog catalog;
		QString error;
		catalog.load(error, {ChatDownloadTask::definition()});
		return {{"id", "download"}, {"type", "function"},
			{"function",
				QJsonObject{{"name", catalog.nameForCommand(ChatDownloadTask::command())},
					{"arguments", QString::fromUtf8(QJsonDocument(arguments).toJson(QJsonDocument::Compact))}}}};
	}

	static QByteArray fileResponse(const QByteArray& body)
	{
		return "HTTP/1.1 200 OK\r\nContent-Type: application/octet-stream\r\nContent-Length: "
			+ QByteArray::number(body.size()) + "\r\nConnection: close\r\n\r\n" + body;
	}

	void downloadMovesVerifiedFileIntoTarget()
	{
		if (!QFileInfo(deployedTool("aria2/aria2c.exe")).isFile())
		{
			QSKIP("build/Release/tools is not deployed; run buildtools/Get-AgentChatTools.ps1");
		}
		ToolTurnServer files;
		QVERIFY(files.server.listen(QHostAddress::LocalHost));
		const QByteArray body = QByteArray("RIFF-fixture-kick-").repeated(512);
		files.rawResponses.append(fileResponse(body));
		QTemporaryDir target;
		const auto url = QStringLiteral("http://127.0.0.1:%1/kits/kick.wav").arg(files.server.serverPort());
		const auto result = runSingleCall(downloadCall({{"url", url}, {"directory", target.path() + "/drums"}}));
		QVERIFY2(result.value("ok").toBool(), qPrintable(QJsonDocument(result).toJson()));
		const auto data = result.value("data").toObject();
		const auto path = QDir(target.path()).absoluteFilePath("drums/kick.wav");
		QCOMPARE(QDir::fromNativeSeparators(data.value("path").toString()), path);
		QFile file(path);
		QVERIFY(file.open(QIODevice::ReadOnly));
		QCOMPARE(file.readAll(), body);
		QCOMPARE(data.value("sha256").toString(),
			QString::fromLatin1(QCryptographicHash::hash(body, QCryptographicHash::Sha256).toHex()));
		QVERIFY(!data.value("overwritten").toBool());
		// The temporary download folder is gone; only the approved file remains.
		QCOMPARE(QDir(target.path() + "/drums").entryList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden),
			QStringList{"kick.wav"});
		QVERIFY(files.heads[0].startsWith("GET /kits/kick.wav"));
	}

	void downloadSameNameNeedsOverwrite()
	{
		if (!QFileInfo(deployedTool("aria2/aria2c.exe")).isFile())
		{
			QSKIP("build/Release/tools is not deployed; run buildtools/Get-AgentChatTools.ps1");
		}
		ToolTurnServer files;
		QVERIFY(files.server.listen(QHostAddress::LocalHost));
		files.rawResponses.append(fileResponse("new content"));
		QTemporaryDir target;
		QFile existing(target.filePath("loop.wav"));
		QVERIFY(existing.open(QIODevice::WriteOnly));
		existing.write("original");
		existing.close();
		const auto url = QStringLiteral("http://127.0.0.1:%1/loop.wav").arg(files.server.serverPort());

		const auto refused = runSingleCall(downloadCall({{"url", url}, {"directory", target.path()}}));
		QCOMPARE(refused.value("error").toObject().value("code").toString(), QStringLiteral("file_exists"));
		QVERIFY(files.heads.isEmpty());
		QVERIFY(existing.open(QIODevice::ReadOnly));
		QCOMPARE(existing.readAll(), QByteArray("original"));
		existing.close();

		const auto replaced
			= runSingleCall(downloadCall({{"url", url}, {"directory", target.path()}, {"overwrite", true}}));
		QVERIFY2(replaced.value("ok").toBool(), qPrintable(QJsonDocument(replaced).toJson()));
		QVERIFY(replaced.value("data").toObject().value("overwritten").toBool());
		QVERIFY(existing.open(QIODevice::ReadOnly));
		QCOMPARE(existing.readAll(), QByteArray("new content"));
	}

	void downloadFailuresLeaveNoFile()
	{
		if (!QFileInfo(deployedTool("aria2/aria2c.exe")).isFile())
		{
			QSKIP("build/Release/tools is not deployed; run buildtools/Get-AgentChatTools.ps1");
		}
		ToolTurnServer files;
		QVERIFY(files.server.listen(QHostAddress::LocalHost));
		files.rawResponses.append("HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\nConnection: close\r\n\r\n");
		QTemporaryDir target;
		const auto url = QStringLiteral("http://127.0.0.1:%1/missing.wav").arg(files.server.serverPort());
		const auto missing = runSingleCall(downloadCall({{"url", url}, {"directory", target.path()}}));
		QCOMPARE(missing.value("error").toObject().value("code").toString(), QStringLiteral("download_failed"));
		QVERIFY(QDir(target.path()).isEmpty(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden));

		// Turned off after approval: refused at run time, nothing requested.
		const auto disabled = runSingleCall(downloadCall({{"url", url}, {"directory", target.path()}}), [] {
			auto settings = ChatDownloadSettings::load();
			settings.enabled = false;
			settings.save();
		});
		QCOMPARE(disabled.value("error").toObject().value("code").toString(), QStringLiteral("downloads_disabled"));
		QCOMPARE(files.heads.size(), 1);

		// A relative folder needs a saved project; the call never reaches approval.
		ChatDownloadSettings{}.save();
		const auto relative = runSingleCall(downloadCall({{"url", url}, {"directory", "assets/drums"}}));
		QVERIFY(relative.isEmpty());
	}

	static quint32 crc32(const QByteArray& data)
	{
		quint32 crc = 0xFFFFFFFFu;
		for (const auto byte : data)
		{
			crc ^= static_cast<quint8>(byte);
			for (int bit = 0; bit < 8; ++bit)
			{
				crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
			}
		}
		return ~crc;
	}

	// A minimal stored zip whose member names are written verbatim, so tests
	// can build archives that normal tools refuse to create.
	static QByteArray storedZip(const QList<QPair<QByteArray, QByteArray>>& members)
	{
		QByteArray local;
		QByteArray central;
		QDataStream localStream(&local, QIODevice::WriteOnly);
		QDataStream centralStream(&central, QIODevice::WriteOnly);
		localStream.setByteOrder(QDataStream::LittleEndian);
		centralStream.setByteOrder(QDataStream::LittleEndian);
		for (const auto& [name, data] : members)
		{
			const quint32 offset = local.size();
			const quint32 crc = crc32(data);
			localStream << quint32(0x04034b50) << quint16(10) << quint16(0) << quint16(0) << quint16(0) << quint16(0)
						<< crc << quint32(data.size()) << quint32(data.size()) << quint16(name.size()) << quint16(0);
			localStream.writeRawData(name.constData(), name.size());
			localStream.writeRawData(data.constData(), data.size());
			centralStream << quint32(0x02014b50) << quint16(20) << quint16(10) << quint16(0) << quint16(0) << quint16(0)
						  << quint16(0) << crc << quint32(data.size()) << quint32(data.size()) << quint16(name.size())
						  << quint16(0) << quint16(0) << quint16(0) << quint16(0) << quint32(0) << offset;
			centralStream.writeRawData(name.constData(), name.size());
		}
		QByteArray end;
		QDataStream endStream(&end, QIODevice::WriteOnly);
		endStream.setByteOrder(QDataStream::LittleEndian);
		endStream << quint32(0x06054b50) << quint16(0) << quint16(0) << quint16(members.size())
				  << quint16(members.size()) << quint32(central.size()) << quint32(local.size()) << quint16(0);
		return local + central + end;
	}

	void downloadExtractsCheckedArchive()
	{
		if (!QFileInfo(deployedTool("7zip/7z.exe")).isFile())
		{
			QSKIP("build/Release/tools is not deployed; run buildtools/Get-AgentChatTools.ps1");
		}
		const auto archive = storedZip({{"kit/kick.wav", "kick"}, {"kit/snare.wav", "snare"}});
		ToolTurnServer files;
		QVERIFY(files.server.listen(QHostAddress::LocalHost));
		files.rawResponses.append(fileResponse(archive));
		files.rawResponses.append(fileResponse(archive));
		QTemporaryDir target;
		const auto url = QStringLiteral("http://127.0.0.1:%1/drums.zip").arg(files.server.serverPort());
		const auto result
			= runSingleCall(downloadCall({{"url", url}, {"directory", target.path()}, {"extract", true}}));
		QVERIFY2(result.value("ok").toBool(), qPrintable(QJsonDocument(result).toJson()));
		const auto data = result.value("data").toObject();
		QCOMPARE(data.value("extractedFiles").toInt(), 2);
		QCOMPARE(QDir::fromNativeSeparators(data.value("extractedTo").toString()), target.filePath("drums"));
		QFile kick(target.filePath("drums/kit/kick.wav"));
		QVERIFY(kick.open(QIODevice::ReadOnly));
		QCOMPARE(kick.readAll(), QByteArray("kick"));
		// The archive is kept; no temporary folder remains.
		QCOMPARE(QDir(target.path()).entryList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden),
			(QStringList{"drums", "drums.zip"}));

		// A non-empty extraction folder is not merged into without overwrite.
		const auto again = runSingleCall(downloadCall({{"url", url}, {"directory", target.path()},
			{"fileName", "drums-2.zip"}, {"extract", true}, {"extractTo", "drums"}}));
		QCOMPARE(again.value("error").toObject().value("code").toString(), QStringLiteral("extract_target_exists"));
		QVERIFY(again.value("error").toObject().value("message").toString().contains("drums-2.zip"));
	}

	void downloadRefusesUnsafeArchive()
	{
		if (!QFileInfo(deployedTool("7zip/7z.exe")).isFile())
		{
			QSKIP("build/Release/tools is not deployed; run buildtools/Get-AgentChatTools.ps1");
		}
		ToolTurnServer files;
		QVERIFY(files.server.listen(QHostAddress::LocalHost));
		files.rawResponses.append(fileResponse(storedZip({{"ok.wav", "fine"}, {"../escape.txt", "escaped"}})));
		QTemporaryDir parent;
		QDir().mkpath(parent.filePath("target"));
		const auto url = QStringLiteral("http://127.0.0.1:%1/evil.zip").arg(files.server.serverPort());
		const auto result
			= runSingleCall(downloadCall({{"url", url}, {"directory", parent.filePath("target")}, {"extract", true}}));
		QCOMPARE(result.value("error").toObject().value("code").toString(), QStringLiteral("unsafe_archive"));
		QVERIFY(result.value("error").toObject().value("message").toString().contains("escape.txt"));
		// Nothing was extracted anywhere; only the downloaded archive remains.
		QVERIFY(!QFileInfo::exists(parent.filePath("escape.txt")));
		QVERIFY(!QFileInfo::exists(parent.filePath("target/evil/ok.wav")));
		QCOMPARE(QDir(parent.filePath("target")).entryList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden),
			QStringList{"evil.zip"});
	}

	void downloadStopRemovesPartialFiles()
	{
		if (!QFileInfo(deployedTool("aria2/aria2c.exe")).isFile())
		{
			QSKIP("build/Release/tools is not deployed; run buildtools/Get-AgentChatTools.ps1");
		}
		ToolTurnServer files;
		QVERIFY(files.server.listen(QHostAddress::LocalHost));
		files.hold = true;
		QTemporaryDir target;
		ToolTurnServer model;
		QVERIFY(model.server.listen(QHostAddress::LocalHost));
		const auto url = QStringLiteral("http://127.0.0.1:%1/slow.wav").arg(files.server.serverPort());
		queueCalls(model, {downloadCall({{"url", url}, {"directory", target.path()}})});
		QTemporaryDir directory;
		ChatStore store(directory.path());
		auto session = ChatStore::newSession();
		QString error;
		QVERIFY(store.saveSession(session, error));
		ChatController controller(store);
		controller.setToolsEnabled(true);
		QSignalSpy approvals(&controller, &ChatController::approvalRequested);
		QVERIFY(controller.send(session.value("id").toString(), model.connection(), "fixture-key", {}, "Get", error));
		QTRY_COMPARE(approvals.size(), 1);
		QVERIFY(controller.decideTool(approvals[0][0].toJsonObject(), true, error));
		QTRY_COMPARE_WITH_TIMEOUT(files.heads.size(), 1, 10000);
		controller.stop();
		QVERIFY(!controller.active());
		QVERIFY(QDir(target.path()).isEmpty(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden));
		QCOMPARE(model.requests.size(), 1);
	}

	void commandSelfResetKeepsResultCancelsRest()
	{
		ToolTurnServer server;
		QVERIFY(server.server.listen(QHostAddress::LocalHost));
		const QJsonObject clearCall{
			{"id", "clear"},
			{"type", "function"},
			{"function", QJsonObject{{"name", m_catalog.nameForCommand("song.clearProject")}, {"arguments", "{}"}}}};
		queueCalls(server, {clearCall, renameCall("after", "Never")});
		QTemporaryDir directory;
		ChatStore store(directory.path());
		auto session = ChatStore::newSession();
		QString error;
		QVERIFY(store.saveSession(session, error));
		const auto id = session.value("id").toString();
		ChatController controller(store);
		controller.setToolsEnabled(true);
		QSignalSpy approvals(&controller, &ChatController::approvalRequested);
		QVERIFY(controller.send(id, server.connection(), "fixture-key", {}, "Clear", error));
		QTRY_COMPARE(approvals.size(), 1);
		QVERIFY(controller.decideTool(approvals[0][0].toJsonObject(), true, error));
		QTRY_VERIFY(!controller.active());

		// The command really reset the project; its actual result is kept,
		// the remaining call is canceled and never reaches approval or the model.
		QVERIFY(lmms::Engine::getSong()->tracks().empty());
		QCOMPARE(approvals.size(), 1);
		QCOMPARE(server.requests.size(), 1);
		const auto stored = store.session(id).value("messages").toArray();
		QCOMPARE(stored.size(), 4);
		const auto cleared = stored[2].toObject();
		QVERIFY(QJsonDocument::fromJson(cleared.value("content").toString().toUtf8()).object().value("ok").toBool());
		QVERIFY(cleared.value("execution").toObject().value("executed").toBool());
		const auto skipped
			= QJsonDocument::fromJson(stored[3].toObject().value("content").toString().toUtf8()).object();
		QCOMPARE(skipped.value("error").toObject().value("code").toString(), QStringLiteral("canceled"));
		QCOMPARE(CommandBus::instance().batchDepth(), 0);
	}

	void stopPairsAllUnexecutedCalls()
	{
		ToolTurnServer server;
		QVERIFY(server.server.listen(QHostAddress::LocalHost));
		queueCalls(server, {renameCall("one", "Never one"), renameCall("two", "Never two")});
		QTemporaryDir directory;
		ChatStore store(directory.path());
		auto session = ChatStore::newSession();
		QString error;
		QVERIFY(store.saveSession(session, error));
		const auto id = session.value("id").toString();
		ChatController controller(store);
		controller.setToolsEnabled(true);
		QSignalSpy approvals(&controller, &ChatController::approvalRequested);
		QVERIFY(controller.send(id, server.connection(), "fixture-key", {}, "Rename", error));
		QTRY_COMPARE(approvals.size(), 1);
		controller.stop();
		QVERIFY(!controller.active());
		QCOMPARE(lmms::Engine::getSong()->tracks()[0]->name(), QStringLiteral("Original"));
		QCOMPARE(server.requests.size(), 1);
		const auto stored = store.session(id).value("messages").toArray();
		QCOMPARE(stored.size(), 4);
		QCOMPARE(stored[1].toObject().value("status").toString(), QStringLiteral("complete"));
		QCOMPARE(stored[2].toObject().value("role").toString(), QStringLiteral("tool"));
		QCOMPARE(stored[3].toObject().value("role").toString(), QStringLiteral("tool"));
		QVERIFY(controller.send(id, server.connection(), "fixture-key", {}, "Continue", error));
		QTRY_VERIFY(!controller.active());
		QCOMPARE(server.requests.size(), 2);
	}

	void noApprovalRunsAndContinues()
	{
		lmms::ConfigManager::inst()->setValue("agentchat", "approval", "none");
		ToolTurnServer server;
		QVERIFY(server.server.listen(QHostAddress::LocalHost));
		queueCalls(server, {renameCall("one", "Automatic")});
		QTemporaryDir directory;
		ChatStore store(directory.path());
		auto session = ChatStore::newSession();
		QString error;
		QVERIFY(store.saveSession(session, error));
		ChatController controller(store);
		controller.setToolsEnabled(true);
		QSignalSpy approvals(&controller, &ChatController::approvalRequested);
		QVERIFY(
			controller.send(session.value("id").toString(), server.connection(), "fixture-key", {}, "Rename", error));
		QTRY_COMPARE(server.requests.size(), 2);
		QTRY_VERIFY(!controller.active());
		QCOMPARE(approvals.size(), 0);
		QCOMPARE(lmms::Engine::getSong()->tracks()[0]->name(), QStringLiteral("Automatic"));
	}

	void projectReplacementCancelsPending()
	{
		ToolTurnServer server;
		QVERIFY(server.server.listen(QHostAddress::LocalHost));
		queueCalls(server, {renameCall("one", "Never")});
		QTemporaryDir directory;
		ChatStore store(directory.path());
		auto session = ChatStore::newSession();
		QString error;
		QVERIFY(store.saveSession(session, error));
		const auto id = session.value("id").toString();
		ChatController controller(store);
		controller.setToolsEnabled(true);
		QSignalSpy approvals(&controller, &ChatController::approvalRequested);
		QVERIFY(controller.send(id, server.connection(), "fixture-key", {}, "Rename", error));
		QTRY_COMPARE(approvals.size(), 1);
		lmms::Engine::getSong()->clearProject();
		QVERIFY(CommandBus::instance().execute("track.create", {{"type", "sample"}, {"name", "Replacement"}}).ok);
		QVERIFY(!controller.active());
		QVERIFY(!controller.decideTool(approvals[0][0].toJsonObject(), true, error));
		QCOMPARE(lmms::Engine::getSong()->tracks()[0]->name(), QStringLiteral("Replacement"));
		QCOMPARE(server.requests.size(), 1);
		QCOMPARE(store.session(id).value("messages").toArray().size(), 3);
	}

	void retryContinuationDoesNotReplayTools()
	{
		ToolTurnServer server;
		QVERIFY(server.server.listen(QHostAddress::LocalHost));
		queueCalls(server, {renameCall("one", "Approved")});
		server.responses[1] = {{"error", QJsonObject{{"message", "Fixture continuation failure"}}}};
		server.responses.append(ToolTurnServer::response({{"role", "assistant"}, {"content", "Recovered"}}, "stop"));
		QTemporaryDir directory;
		ChatStore store(directory.path());
		auto session = ChatStore::newSession();
		QString error;
		QVERIFY(store.saveSession(session, error));
		const auto id = session.value("id").toString();
		ChatController controller(store);
		controller.setToolsEnabled(true);
		QSignalSpy approvals(&controller, &ChatController::approvalRequested);
		QVERIFY(controller.send(id, server.connection(), "fixture-key", {}, "Rename", error));
		QTRY_COMPARE(approvals.size(), 1);
		QVERIFY(controller.decideTool(approvals[0][0].toJsonObject(), true, error));
		QTRY_VERIFY(!controller.active());
		QCOMPARE(server.requests.size(), 2);
		QCOMPARE(controller.currentMessage().value("status").toString(), QStringLiteral("error"));
		QCOMPARE(lmms::Engine::getSong()->tracks()[0]->name(), QStringLiteral("Approved"));
		QVERIFY(CommandBus::instance().execute("track.setName", {{"track", 0}, {"value", "Manual"}}).ok);
		QVERIFY2(controller.retry(id, server.connection(), "fixture-key", {}, error), qPrintable(error));
		QTRY_VERIFY(!controller.active());
		QCOMPARE(server.requests.size(), 3);
		QCOMPARE(approvals.size(), 1);
		QCOMPARE(lmms::Engine::getSong()->tracks()[0]->name(), QStringLiteral("Manual"));
		QCOMPARE(server.requests[2].value("messages").toArray().size(), 3);
	}

	void deletedSessionNeverExecutes()
	{
		ToolTurnServer server;
		QVERIFY(server.server.listen(QHostAddress::LocalHost));
		queueCalls(server, {renameCall("one", "Never")});
		QTemporaryDir directory;
		ChatStore store(directory.path());
		auto session = ChatStore::newSession();
		QString error;
		QVERIFY(store.saveSession(session, error));
		const auto id = session.value("id").toString();
		ChatController controller(store);
		controller.setToolsEnabled(true);
		QSignalSpy approvals(&controller, &ChatController::approvalRequested);
		QVERIFY(controller.send(id, server.connection(), "fixture-key", {}, "Rename", error));
		QTRY_COMPARE(approvals.size(), 1);
		QVERIFY(store.removeSession(id, error));
		QVERIFY(!controller.decideTool(approvals[0][0].toJsonObject(), true, error));
		QVERIFY(!controller.active());
		QVERIFY(store.session(id).isEmpty());
		QCOMPARE(lmms::Engine::getSong()->tracks()[0]->name(), QStringLiteral("Original"));
		QCOMPARE(server.requests.size(), 1);
	}
};

QTEST_MAIN(ChatToolTurnTest)
#include "ChatToolTurnTest.moc"
