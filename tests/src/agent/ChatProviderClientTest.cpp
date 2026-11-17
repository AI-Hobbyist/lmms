#include "agent/chat/ChatProviderClient.h"

#include <QFile>
#include <QJsonDocument>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTimer>
#include <QtTest>

#include "agent/chat/ChatContent.h"
#include "agent/chat/ChatController.h"
#include "agent/chat/ChatTools.h"

using namespace lmms::agent::chat;

class HttpFixture : public QObject
{
public:
	QTcpServer server;
	QByteArray body;
	QByteArray received;
	QByteArray contentType = "text/event-stream";
	int status = 200;
	bool stall = false;
	int requests = 0;

	HttpFixture()
	{
		connect(&server, &QTcpServer::newConnection, this, [this] {
			auto* socket = server.nextPendingConnection();
			connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
			connect(socket, &QTcpSocket::readyRead, socket, [this, socket] {
				received += socket->readAll();
				const auto separator = received.indexOf("\r\n\r\n");
				if (separator < 0 || socket->property("responded").toBool())
				{
					return;
				}
				int length = 0;
				for (const auto& line : received.left(separator).split('\n'))
				{
					if (line.toLower().startsWith("content-length:"))
					{
						length = line.mid(line.indexOf(':') + 1).trimmed().toInt();
					}
				}
				if (received.size() < separator + 4 + length)
				{
					return;
				}
				socket->setProperty("responded", true);
				++requests;
				socket->write("HTTP/1.1 " + QByteArray::number(status) + " Result\r\nContent-Type: " + contentType
							  + "\r\nConnection: close\r\n\r\n");
				const auto midpoint = body.size() / 2;
				socket->write(body.left(midpoint));
				QTimer::singleShot(20, socket, [this, socket, midpoint] {
					socket->write(body.mid(midpoint));
					if (!stall)
					{
						socket->disconnectFromHost();
					}
				});
			});
		});
	}

	ChatConnection connection() const
	{
		auto result = ChatConnection::create();
		result.baseUrl = QStringLiteral("http://127.0.0.1:%1/v1").arg(server.serverPort());
		result.model = "fixture-model";
		return result;
	}
};

class ChatProviderClientTest : public QObject
{
	Q_OBJECT
private:
	const QJsonArray m_messages{QJsonObject{{"role", "user"}, {"content", "Hello"}}};
	const QByteArray m_text = QStringLiteral("data: {\"choices\":[{\"delta\":{\"content\":\"旋律\"}}]}\n\n").toUtf8();
	const QByteArray m_done = "data: {\"choices\":[{\"delta\":{},\"finish_reason\":\"stop\"}]}\n\ndata: [DONE]\n\n";

private slots:
	void toolCatalogOverHttp()
	{
		HttpFixture fixture;
		QVERIFY(fixture.server.listen(QHostAddress::LocalHost));
		fixture.body = m_text + m_done;
		ChatToolCatalog catalog;
		QString error;
		QVERIFY2(catalog.load(error), qPrintable(error));
		ChatProviderClient client;
		QSignalSpy finished(&client, &ChatProviderClient::finished);
		QVERIFY(client.start(fixture.connection(), "fake-key", "Prompt", m_messages, error, catalog.definitions()));
		QTRY_COMPARE(finished.count(), 1);
		QVERIFY(client.response().complete);
		const auto body
			= QJsonDocument::fromJson(fixture.received.mid(fixture.received.indexOf("\r\n\r\n") + 4)).object();
		const auto tools = body.value("tools").toArray();
		QCOMPARE(tools.size(), catalog.definitions().size());
		bool found = false;
		for (const auto& value : tools)
		{
			const auto function = value.toObject().value("function").toObject();
			if (function.value("name") == catalog.nameForCommand("query.songSummary"))
			{
				found = true;
				QVERIFY(function.value("parameters").isObject());
			}
		}
		QVERIFY(found);
		// Only the caller's message and prompt are sent; listing schemas does not
		// execute query.songSummary or add a project snapshot to the conversation.
		QCOMPARE(body.value("messages").toArray().size(), 2);
		QCOMPARE(body.value("messages").toArray().last().toObject().value("content").toString(),
				 QStringLiteral("Hello"));
	}

	void retryFailedRequest()
	{
		QTemporaryDir directory;
		ChatStore store(directory.filePath("sessions"));
		auto session = ChatStore::newSession();
		const auto id = session.value("id").toString();
		QString error;
		QVERIFY(store.saveSession(session, error));
		QFile file(directory.filePath("reference.png"));
		const auto bytes = QByteArray::fromBase64(
			"iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAQAAAC1HAwCAAAAC0lEQVR42mP8/x8AAwMCAO+j9s0AAAAASUVORK5CYII=");
		QVERIFY(file.open(QIODevice::WriteOnly));
		QCOMPARE(file.write(bytes), bytes.size());
		file.close();
		ChatAttachment attachment;
		QVERIFY(inspectChatAttachment(file.fileName(), attachment, error));
		HttpFixture fixture;
		QVERIFY(fixture.server.listen(QHostAddress::LocalHost));
		fixture.status = 503;
		fixture.body = "Unavailable";
		auto connection = fixture.connection();
		connection.capabilities.insert("inputTypes", QJsonArray{"image"});
		{
			ChatController controller(store);
			QVERIFY(!controller.retry(id, connection, "fake-key", "Prompt", error));
			QVERIFY(controller.send(id, connection, "fake-key", "Prompt", "Describe", error, {attachment}));
			QTRY_VERIFY(!controller.active());
			QCOMPARE(controller.currentMessage().value("status").toString(), QStringLiteral("error"));
			QCOMPARE(fixture.requests, 1);
		}
		QVERIFY(file.remove());
		const auto originalMessages = store.session(id).value("messages").toArray();
		session = store.session(id);
		session.insert("draft", "Keep this unsent draft");
		QVERIFY(store.saveSession(session, error));
		ChatController restored(store);
		fixture.received.clear();
		QVERIFY(restored.retry(id, connection, "fake-key", "Prompt", error));
		QVERIFY(!restored.retry(id, connection, "fake-key", "Prompt", error));
		QTRY_VERIFY(!restored.active());
		QCOMPARE(fixture.requests, 2);
		QCOMPARE(store.session(id).value("messages").toArray().size(), 3);
		fixture.received.clear();
		fixture.status = 200;
		fixture.body = m_text + m_done;
		QVERIFY(restored.retry(id, connection, "fake-key", "Prompt", error));
		QTRY_VERIFY(!restored.active());
		QCOMPARE(fixture.requests, 3);
		const auto messages = store.session(id).value("messages").toArray();
		QCOMPARE(messages.size(), 4);
		QCOMPARE(messages.first(), originalMessages.first());
		QCOMPARE(messages.last().toObject().value("status").toString(), QStringLiteral("complete"));
		QCOMPARE(messages.last().toObject().value("retryOf"), messages[2].toObject().value("id"));
		QCOMPARE(store.session(id).value("draft").toString(), QStringLiteral("Keep this unsent draft"));
		const auto body
			= QJsonDocument::fromJson(fixture.received.mid(fixture.received.indexOf("\r\n\r\n") + 4)).object();
		const auto history = body.value("messages").toArray();
		QCOMPARE(history.size(), 2);
		const auto parts = history[1].toObject().value("content").toArray();
		const auto uri = parts[1].toObject().value("image_url").toObject().value("url").toString();
		QCOMPARE(QByteArray::fromBase64(uri.mid(uri.indexOf(',') + 1).toLatin1()), bytes);
		QVERIFY(!restored.retry(id, connection, "fake-key", "Prompt", error));
		QCOMPARE(fixture.requests, 3);
		// Metadata alone does not authorize rereading an imported local file.
		session = store.session(id);
		auto missing = originalMessages;
		auto user = missing[0].toObject();
		user.remove("content");
		missing[0] = user;
		session.insert("messages", missing);
		QVERIFY(store.saveSession(session, error));
		QVERIFY(!restored.retry(id, connection, "fake-key", "Prompt", error));
		QVERIFY(error.contains("Select the files again"));
		QCOMPARE(store.session(id).value("messages").toArray(), missing);
	}

	void customTemplateAndErrorMapping()
	{
		for (const bool stream : {false, true})
		{
			HttpFixture fixture;
			QVERIFY(fixture.server.listen(QHostAddress::LocalHost));
			fixture.contentType = stream ? "text/event-stream" : "application/json";
			const QByteArray json = R"({"result":{"text":"Done","done":true}})";
			fixture.body = stream ? "data: " + json + "\n\n" : json;
			auto connection = fixture.connection();
			connection.protocol = "custom-json";
			connection.mapping
				= {{"path", "generate"},
				   {"stream", stream},
				   {"requestTemplate",
					QJsonObject{{"payload", QJsonObject{{"history", "${messages}"}}},
								{"options", QJsonObject{{"model_id", "${model}"}, {"streaming", "${stream}"}}}}},
				   {"responseTextPath", "result.text"},
				   {"responseDonePath", "result.done"},
				   {"responseErrorPath", "fault.detail"}};
			ChatProviderClient client;
			QSignalSpy finished(&client, &ChatProviderClient::finished);
			QString error;
			QVERIFY2(client.start(connection, "secret-key", "Prompt", m_messages, error), qPrintable(error));
			QTRY_COMPARE(finished.count(), 1);
			QVERIFY2(client.response().complete, qPrintable(client.response().error));
			QCOMPARE(client.response().text, QStringLiteral("Done"));
			const auto body
				= QJsonDocument::fromJson(fixture.received.mid(fixture.received.indexOf("\r\n\r\n") + 4)).object();
			QCOMPARE(body.value("payload").toObject().value("history").toArray().size(), 2);
			QCOMPARE(body.value("options").toObject().value("streaming"), QJsonValue(stream));
			QCOMPARE(body.value("options").toObject().value("model_id").toString(), connection.model);
			QVERIFY(!body.contains("messages"));
			fixture.received.clear();
			const QByteArray failure = R"({"fault":{"detail":"Invalid secret-key"}})";
			fixture.body = stream ? "data: " + failure + "\n\n" : failure;
			QVERIFY(client.start(connection, "secret-key", "Prompt", m_messages, error));
			QTRY_COMPARE(finished.count(), 2);
			QVERIFY(!client.response().complete);
			QVERIFY(client.response().error.contains("Invalid"));
			QVERIFY(!client.response().error.contains("secret-key"));
			QVERIFY(client.response().toolCalls.isEmpty());
		}
	}

	void streamedPost()
	{
		HttpFixture fixture;
		QVERIFY(fixture.server.listen(QHostAddress::LocalHost));
		fixture.body = m_text + m_done;
		ChatProviderClient client;
		QSignalSpy finished(&client, &ChatProviderClient::finished);
		QSignalSpy updated(&client, &ChatProviderClient::updated);
		QString error;
		QVERIFY(client.start(fixture.connection(), "fake-key", "Prompt", m_messages, error));
		QVERIFY(!client.start(fixture.connection(), "fake-key", "Prompt", m_messages, error));
		QTRY_COMPARE(finished.count(), 1);
		QVERIFY(!client.active());
		QVERIFY2(client.response().complete, qPrintable(client.response().error));
		QCOMPARE(client.response().text, QStringLiteral("旋律"));
		QVERIFY(!updated.isEmpty());
		QVERIFY(fixture.received.startsWith("POST /v1/chat/completions HTTP/1.1\r\n"));
		QVERIFY(fixture.received.contains("Bearer fake-key"));
		const auto body
			= QJsonDocument::fromJson(fixture.received.mid(fixture.received.indexOf("\r\n\r\n") + 4)).object();
		QCOMPARE(body.value("model").toString(), QStringLiteral("fixture-model"));
		QCOMPARE(fixture.requests, 1);
	}

	void jsonResponses()
	{
		for (const auto& protocol : ChatConnection::protocols())
		{
			HttpFixture fixture;
			QVERIFY(fixture.server.listen(QHostAddress::LocalHost));
			fixture.contentType = "application/json; charset=utf-8";
			auto connection = fixture.connection();
			connection.protocol = protocol;
			connection.mapping.insert("stream", false);
			if (protocol == "anthropic-messages")
			{
				fixture.body
					= R"({"type":"message","content":[{"type":"text","text":"Melody"}],"stop_reason":"end_turn","usage":{"output_tokens":3}})";
			}
			else if (protocol == "gemini")
			{
				fixture.body
					= R"({"candidates":[{"content":{"parts":[{"text":"Melody"}]},"finishReason":"STOP"}],"usageMetadata":{"totalTokenCount":3}})";
			}
			else if (protocol.endsWith("responses"))
			{
				fixture.body
					= R"({"status":"completed","output":[{"type":"message","content":[{"type":"output_text","text":"Melody"}]}],"usage":{"output_tokens":3}})";
			}
			else if (protocol == "custom-json")
			{
				connection.mapping.insert("path", "generate");
				connection.mapping.insert("responseTextPath", "result.answer");
				connection.mapping.insert("responseDonePath", "done");
				fixture.body = R"({"result":{"answer":"Melody"},"done":true})";
			}
			else
			{
				fixture.body
					= R"({"choices":[{"message":{"role":"assistant","content":"Melody"},"finish_reason":"stop"}],"usage":{"completion_tokens":3}})";
			}
			ChatProviderClient client;
			QSignalSpy finished(&client, &ChatProviderClient::finished);
			QString error;
			QVERIFY2(client.start(connection, "fake-key", "Prompt", m_messages, error), qPrintable(error));
			QTRY_COMPARE(finished.count(), 1);
			QVERIFY2(client.response().complete, qPrintable(protocol + ": " + client.response().error));
			QCOMPARE(client.response().text, QStringLiteral("Melody"));
			QCOMPARE(fixture.requests, 1);
			QVERIFY(fixture.received.contains("Accept: application/json"));
			if (protocol == "gemini")
			{
				QVERIFY(fixture.received.startsWith("POST /v1/models/fixture-model:generateContent HTTP/1.1"));
			}
			else
			{
				const auto request
					= QJsonDocument::fromJson(fixture.received.mid(fixture.received.indexOf("\r\n\r\n") + 4)).object();
				QCOMPARE(request.value("stream"), QJsonValue(false));
			}
		}
	}

	void jsonDespiteStreamRequest()
	{
		HttpFixture fixture;
		QVERIFY(fixture.server.listen(QHostAddress::LocalHost));
		fixture.contentType = "application/json";
		fixture.body = R"({"choices":[{"message":{"content":"Answer"},"finish_reason":"stop"}]})";
		ChatProviderClient client;
		QSignalSpy finished(&client, &ChatProviderClient::finished);
		QString error;
		QVERIFY(client.start(fixture.connection(), "fake-key", "Prompt", m_messages, error));
		QTRY_COMPARE(finished.count(), 1);
		QVERIFY(client.response().complete);
		QCOMPARE(client.response().text, QStringLiteral("Answer"));
		QVERIFY(fixture.received.contains("Accept: text/event-stream"));
	}

	void jsonErrorsAndCancellation()
	{
		for (const QByteArray body :
			 {QByteArray("{bad"), QByteArray("{}"), QByteArray(R"({"error":{"message":"failed fake-key"}})")})
		{
			HttpFixture fixture;
			QVERIFY(fixture.server.listen(QHostAddress::LocalHost));
			fixture.contentType = "application/problem+json";
			fixture.body = body;
			ChatProviderClient client;
			QSignalSpy finished(&client, &ChatProviderClient::finished);
			QString error;
			QVERIFY(client.start(fixture.connection(), "fake-key", "Prompt", m_messages, error));
			QTRY_COMPARE(finished.count(), 1);
			QVERIFY(!client.response().complete);
			QVERIFY(!client.response().error.isEmpty());
			QVERIFY(!client.response().error.contains("fake-key"));
			QVERIFY(client.response().toolCalls.isEmpty());
		}
		HttpFixture stalled;
		QVERIFY(stalled.server.listen(QHostAddress::LocalHost));
		stalled.contentType = "application/json";
		stalled.body = R"({"choices":[{"message":{"content":"Answer"},"finish_reason":"stop"}]})";
		stalled.stall = true;
		ChatProviderClient client;
		QSignalSpy finished(&client, &ChatProviderClient::finished);
		QString error;
		QVERIFY(client.start(stalled.connection(), "fake-key", "Prompt", m_messages, error));
		QTRY_COMPARE(stalled.requests, 1);
		client.cancel();
		QVERIFY(!client.active());
		QVERIFY(!client.response().complete);
		QCOMPARE(finished.count(), 0);
	}

	void interruptionAndHttpErrors()
	{
		for (const int status : {200, 401, 302})
		{
			HttpFixture fixture;
			QVERIFY(fixture.server.listen(QHostAddress::LocalHost));
			fixture.body = m_text;
			fixture.status = status;
			ChatProviderClient client;
			QSignalSpy finished(&client, &ChatProviderClient::finished);
			QString error;
			QVERIFY(client.start(fixture.connection(), "fake-key", "Prompt", m_messages, error));
			QTRY_COMPARE(finished.count(), 1);
			QVERIFY(!client.response().complete);
			QVERIFY(!client.response().error.isEmpty());
			QVERIFY(client.response().toolCalls.isEmpty());
			QVERIFY(!client.response().error.contains("fake-key"));
			if (status == 200)
			{
				QCOMPARE(client.response().text, QStringLiteral("旋律"));
			}
			QCOMPARE(fixture.requests, 1);
		}
	}

	void controllerSessionBinding()
	{
		QTemporaryDir directory;
		ChatStore store(directory.path());
		auto first = ChatStore::newSession();
		auto second = ChatStore::newSession();
		const auto firstId = first.value("id").toString();
		const auto secondId = second.value("id").toString();
		QString error;
		QVERIFY(store.saveSession(first, error));
		QVERIFY(store.saveSession(second, error));
		HttpFixture fixture;
		QVERIFY(fixture.server.listen(QHostAddress::LocalHost));
		fixture.body = m_text + m_done;
		ChatController controller(store);
		QSignalSpy changed(&controller, &ChatController::sessionChanged);
		QVERIFY(controller.send(firstId, fixture.connection(), "fake-key", "Prompt", "Question", error));
		QCOMPARE(controller.activeSessionId(), firstId);
		QVERIFY(!controller.send(secondId, fixture.connection(), "fake-key", "Prompt", "Other", error));
		first = store.session(firstId);
		first.insert("draft", "Next unsent message");
		first.insert("title", "Renamed while streaming");
		QVERIFY(store.saveSession(first, error));
		second.insert("draft", "Other session draft");
		QVERIFY(store.saveSession(second, error));
		QTRY_VERIFY(!controller.active());
		QVERIFY(changed.count() >= 2);
		first = store.session(firstId);
		QCOMPARE(first.value("draft").toString(), QStringLiteral("Next unsent message"));
		QCOMPARE(first.value("title").toString(), QStringLiteral("Renamed while streaming"));
		const auto messages = first.value("messages").toArray();
		QCOMPARE(messages.size(), 2);
		QCOMPARE(messages[0].toObject().value("text").toString(), QStringLiteral("Question"));
		QCOMPARE(messages[1].toObject().value("text").toString(), QStringLiteral("旋律"));
		QCOMPARE(messages[1].toObject().value("status").toString(), QStringLiteral("complete"));
		QCOMPARE(store.session(secondId).value("messages").toArray().size(), 0);
		QCOMPARE(store.session(secondId).value("draft").toString(), QStringLiteral("Other session draft"));
	}

	void controllerCancellationAndDeletion()
	{
		QTemporaryDir directory;
		ChatStore store(directory.path());
		auto session = ChatStore::newSession();
		const auto id = session.value("id").toString();
		QString error;
		QVERIFY(store.saveSession(session, error));
		HttpFixture stalled;
		QVERIFY(stalled.server.listen(QHostAddress::LocalHost));
		stalled.body = m_text;
		stalled.stall = true;
		ChatController controller(store);
		QSignalSpy updated(&controller, &ChatController::messageUpdated);
		QSignalSpy failed(&controller, &ChatController::requestFailed);
		QVERIFY(controller.send(id, stalled.connection(), "fake-key", "Prompt", "Question", error));
		QTRY_VERIFY(!updated.isEmpty());
		controller.stop();
		QVERIFY(!controller.active());
		const auto canceled = store.session(id).value("messages").toArray().last().toObject();
		QCOMPARE(canceled.value("status").toString(), QStringLiteral("canceled"));
		QCOMPARE(canceled.value("text").toString(), QStringLiteral("旋律"));
		HttpFixture next;
		QVERIFY(next.server.listen(QHostAddress::LocalHost));
		next.body = m_text + m_done;
		QVERIFY(controller.send(id, next.connection(), "fake-key", "Prompt", "Next question", error));
		QVERIFY(store.removeSession(id, error));
		QTRY_VERIFY(!controller.active());
		QCOMPARE(failed.count(), 1);
		QVERIFY(store.session(id).isEmpty());
	}

	void attachmentPreparationLifecycle()
	{
		QTemporaryDir directory;
		ChatStore store(directory.filePath("sessions"));
		const auto session = ChatStore::newSession();
		const auto id = session.value("id").toString();
		QString error;
		QVERIFY(store.saveSession(session, error));
		QFile file(directory.filePath("reference.png"));
		QVERIFY(file.open(QIODevice::WriteOnly));
		file.write("attachment fixture");
		file.close();
		ChatAttachment attachment;
		QVERIFY(inspectChatAttachment(file.fileName(), attachment, error));
		HttpFixture fixture;
		QVERIFY(fixture.server.listen(QHostAddress::LocalHost));
		fixture.body = m_text + m_done;
		auto connection = fixture.connection();
		ChatController controller(store);
		QSignalSpy failed(&controller, &ChatController::requestFailed);
		QVERIFY(!controller.send(id, connection, "fake-key", "Prompt", "", error, {attachment}));
		QVERIFY(!error.isEmpty());
		QCOMPARE(store.session(id).value("messages").toArray().size(), 0);
		connection.capabilities.insert("inputTypes", QJsonArray{"image"});
		QVERIFY(controller.send(id, connection, "fake-key", "Prompt", "", error, {attachment}));
		QVERIFY(controller.active());
		controller.stop();
		QVERIFY(!controller.active());
		QCOMPARE(store.session(id).value("messages").toArray().last().toObject().value("status").toString(),
				 QStringLiteral("canceled"));
		// The queued completion of the canceled preparation must not start HTTP.
		QTest::qWait(50);
		QCOMPARE(fixture.requests, 0);
		QVERIFY(file.remove());
		QVERIFY(controller.send(id, connection, "fake-key", "Prompt", "Retry", error, {attachment}));
		QTRY_VERIFY(!controller.active());
		QCOMPARE(failed.count(), 1);
		QCOMPARE(fixture.requests, 0);
		QCOMPARE(store.session(id).value("messages").toArray().last().toObject().value("status").toString(),
				 QStringLiteral("error"));
	}

	void attachmentHistorySnapshot()
	{
		QTemporaryDir directory;
		ChatStore store(directory.filePath("sessions"));
		const auto session = ChatStore::newSession();
		const auto id = session.value("id").toString();
		QString error;
		QVERIFY(store.saveSession(session, error));
		QFile file(directory.filePath("reference.png"));
		const auto bytes = QByteArray::fromBase64(
			"iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAQAAAC1HAwCAAAAC0lEQVR42mP8/x8AAwMCAO+j9s0AAAAASUVORK5CYII=");
		QVERIFY(file.open(QIODevice::WriteOnly));
		QCOMPARE(file.write(bytes), bytes.size());
		file.close();
		ChatAttachment attachment;
		QVERIFY(inspectChatAttachment(file.fileName(), attachment, error));
		HttpFixture first;
		QVERIFY(first.server.listen(QHostAddress::LocalHost));
		first.body = m_text + m_done;
		auto connection = first.connection();
		connection.capabilities.insert("inputTypes", QJsonArray{"image"});
		{
			ChatController controller(store);
			QVERIFY(controller.send(id, connection, "fake-key", "Prompt", "Describe", error, {attachment}));
			QTRY_VERIFY(!controller.active());
			QCOMPARE(first.requests, 1);
		}
		QVERIFY(file.remove());
		const auto exportPath = directory.filePath("export.json");
		QVERIFY(store.exportSession(id, exportPath, error));
		const auto importedId = store.importSession(exportPath, error);
		QVERIFY(!importedId.isEmpty());
		// Metadata from an imported conversation must never read this local file.
		QFile secret(directory.filePath("private.txt"));
		QVERIFY(secret.open(QIODevice::WriteOnly));
		secret.write("LOCAL-CONTENT-MUST-NOT-BE-READ");
		secret.close();
		auto imported = store.session(importedId);
		auto messages = imported.value("messages").toArray();
		messages.append(QJsonObject{
			{"role", "user"},
			{"text", "Old reference"},
			{"attachments", QJsonArray{QJsonObject{{"name", "private.txt"}, {"path", secret.fileName()}}}}});
		imported.insert("messages", messages);
		QVERIFY(store.saveSession(imported, error));
		HttpFixture next;
		QVERIFY(next.server.listen(QHostAddress::LocalHost));
		next.body = m_text + m_done;
		connection.baseUrl = next.connection().baseUrl;
		ChatController restored(store);
		QVERIFY(restored.send(importedId, connection, "fake-key", "Prompt", "Continue", error));
		QTRY_VERIFY(!restored.active());
		QCOMPARE(next.requests, 1);
		const auto body = QJsonDocument::fromJson(next.received.mid(next.received.indexOf("\r\n\r\n") + 4)).object();
		const auto history = body.value("messages").toArray();
		const auto parts = history[1].toObject().value("content").toArray();
		QCOMPARE(parts.size(), 2);
		const auto uri = parts[1].toObject().value("image_url").toObject().value("url").toString();
		QCOMPARE(QByteArray::fromBase64(uri.mid(uri.indexOf(',') + 1).toLatin1()), bytes);
		QVERIFY(next.received.contains("content unavailable; select it again to send"));
		QVERIFY(!next.received.contains("LOCAL-CONTENT-MUST-NOT-BE-READ"));
		QVERIFY(QFile::exists(secret.fileName()));
	}

	void controllerToolHistory()
	{
		QTemporaryDir directory;
		ChatStore store(directory.path());
		auto session = ChatStore::newSession();
		const auto id = session.value("id").toString();
		const QJsonObject assistant{
			{"role", "assistant"},
			{"text", ""},
			{"status", "complete"},
			{"tool_calls", QJsonArray{QJsonObject{{"id", "a"}, {"name", "one"}, {"arguments", QJsonObject{}}}}}};
		QJsonArray messages{
			QJsonObject{{"role", "user"}, {"text", "Question"}}, assistant,
			QJsonObject{
				{"role", "tool"}, {"tool_call_id", "a"}, {"name", "one"}, {"content", QJsonObject{{"notes", 3}}}}};
		session.insert("messages", messages);
		QString error;
		QVERIFY(store.saveSession(session, error));
		HttpFixture fixture;
		QVERIFY(fixture.server.listen(QHostAddress::LocalHost));
		fixture.body = m_text + m_done;
		ChatController controller(store);
		QVERIFY(controller.send(id, fixture.connection(), "fake-key", "", "Continue", error));
		QTRY_VERIFY(!controller.active());
		QCOMPARE(fixture.requests, 1);
		const auto request
			= QJsonDocument::fromJson(fixture.received.mid(fixture.received.indexOf("\r\n\r\n") + 4)).object();
		const auto history = request.value("messages").toArray();
		QCOMPARE(history[1].toObject().value("tool_calls").toArray().size(), 1);
		QCOMPARE(history[2].toObject().value("role").toString(), QStringLiteral("tool"));
		QVERIFY(history[2].toObject().value("content").toString().contains("notes"));
		auto incomplete = ChatStore::newSession();
		const auto incompleteId = incomplete.value("id").toString();
		messages.removeLast();
		incomplete.insert("messages", messages);
		QVERIFY(store.saveSession(incomplete, error));
		QVERIFY(!controller.send(incompleteId, fixture.connection(), "fake-key", "", "Unresolved", error));
		QVERIFY(!error.isEmpty());
		QCOMPARE(store.session(incompleteId).value("messages").toArray(), messages);
		QCOMPARE(fixture.requests, 1);
	}

	void toolProtocolRoundTrip()
	{
		for (const auto& protocol : ChatConnection::protocols())
		{
			HttpFixture fixture;
			QVERIFY(fixture.server.listen(QHostAddress::LocalHost));
			fixture.contentType = "application/json";
			auto connection = fixture.connection();
			connection.protocol = protocol;
			connection.mapping.insert("stream", false);
			QByteArray finalBody;
			if (protocol == "anthropic-messages")
			{
				fixture.body
					= R"({"content":[{"type":"thinking","thinking":"Plan","signature":"opaque-state"},{"type":"tool_use","id":"a","name":"one","input":{}},{"type":"tool_use","id":"b","name":"two","input":{}}],"stop_reason":"tool_use"})";
				finalBody = R"({"content":[{"type":"text","text":"Done"}],"stop_reason":"end_turn"})";
			}
			else if (protocol == "gemini")
			{
				fixture.body
					= R"({"candidates":[{"content":{"parts":[{"functionCall":{"name":"one","args":{}},"thoughtSignature":"opaque-state"},{"functionCall":{"id":"wire-b","name":"two","args":{}}}]},"finishReason":"STOP"}]})";
				finalBody = R"({"candidates":[{"content":{"parts":[{"text":"Done"}]},"finishReason":"STOP"}]})";
			}
			else if (protocol.endsWith("responses"))
			{
				fixture.body
					= R"({"status":"completed","output":[{"type":"reasoning","encrypted_content":"opaque-state","summary":[]},{"type":"function_call","call_id":"a","name":"one","arguments":"{}"},{"type":"function_call","call_id":"b","name":"two","arguments":"{}"}]})";
				finalBody
					= R"({"status":"completed","output":[{"type":"message","role":"assistant","content":[{"type":"output_text","text":"Done"}]}]})";
			}
			else if (protocol == "custom-json")
			{
				connection.mapping.insert("path", "generate");
				connection.mapping.insert("responseTextPath", "answer");
				connection.mapping.insert("responseToolCallsPath", "calls");
				connection.mapping.insert("toolHistoryFormat", "openai-chat");
				fixture.body
					= R"({"answer":"","calls":[{"id":"a","name":"one","arguments":{}},{"id":"b","name":"two","arguments":{}}]})";
				finalBody = R"({"answer":"Done"})";
			}
			else
			{
				fixture.body
					= R"({"choices":[{"message":{"content":"","tool_calls":[{"id":"a","function":{"name":"one","arguments":"{}"}},{"id":"b","function":{"name":"two","arguments":"{}"}}]},"finish_reason":"tool_calls"}]})";
				finalBody = R"({"choices":[{"message":{"content":"Done"},"finish_reason":"stop"}]})";
			}
			ChatProviderClient client;
			QSignalSpy finished(&client, &ChatProviderClient::finished);
			QString error;
			QVERIFY(client.start(connection, "fake-key", "", m_messages, error));
			QTRY_COMPARE(finished.count(), 1);
			const auto response = client.response();
			QVERIFY2(response.complete, qPrintable(protocol + ": " + response.error));
			QCOMPARE(response.toolCalls.size(), 2);
			QJsonObject assistant{
				{"role", "assistant"}, {"content", response.text}, {"tool_calls", response.toolCalls}};
			if (!response.nativeContent.isEmpty())
			{
				assistant.insert(
					"native",
					QJsonObject{{"origin", chatNativeOrigin(connection)}, {"content", response.nativeContent}});
			}
			auto history = m_messages;
			history.append(assistant);
			// Results are fixture data; this test does not execute application tools.
			for (int index = 1; index >= 0; --index)
			{
				const auto call = response.toolCalls[index].toObject();
				history.append(
					QJsonObject{{"role", "tool"},
								{"tool_call_id", call.value("id")},
								{"name", call.value("name")},
								{"content", index == 1 ? QJsonObject{{"reason", "denied"}} : QJsonObject{{"notes", 3}}},
								{"is_error", index == 1}});
			}
			fixture.received.clear();
			fixture.body = finalBody;
			QVERIFY2(client.start(connection, "fake-key", "", history, error), qPrintable(protocol + ": " + error));
			QTRY_COMPARE(finished.count(), 2);
			QVERIFY2(client.response().complete, qPrintable(client.response().error));
			QCOMPARE(client.response().text, QStringLiteral("Done"));
			QCOMPARE(fixture.requests, 2);
			const auto request
				= QJsonDocument::fromJson(fixture.received.mid(fixture.received.indexOf("\r\n\r\n") + 4)).object();
			if (protocol == "gemini")
			{
				const auto parts = request.value("contents").toArray().last().toObject().value("parts").toArray();
				QCOMPARE(parts.size(), 2);
				QCOMPARE(parts[0].toObject().value("functionResponse").toObject().value("id").toString(),
						 QStringLiteral("wire-b"));
				QVERIFY(!parts[1].toObject().value("functionResponse").toObject().contains("id"));
			}
			else if (protocol == "anthropic-messages")
			{
				const auto parts = request.value("messages").toArray().last().toObject().value("content").toArray();
				QCOMPARE(parts.size(), 2);
				QCOMPARE(parts[0].toObject().value("tool_use_id").toString(), QStringLiteral("b"));
				QVERIFY(parts[0].toObject().value("is_error").toBool());
			}
			QVERIFY(fixture.received.contains("notes"));
			QVERIFY(fixture.received.contains("denied"));
			if (protocol == "gemini" || protocol == "anthropic-messages" || protocol.endsWith("responses"))
			{
				QVERIFY(fixture.received.contains("opaque-state"));
			}
		}
	}

	void nativeHistoryRoundTrip()
	{
		for (const auto& protocol :
			 {QStringLiteral("anthropic-messages"), QStringLiteral("gemini"), QStringLiteral("openai-responses")})
		{
			QTemporaryDir directory;
			ChatStore store(directory.path());
			const auto session = ChatStore::newSession();
			const auto id = session.value("id").toString();
			QString error;
			QVERIFY(store.saveSession(session, error));
			HttpFixture fixture;
			QVERIFY(fixture.server.listen(QHostAddress::LocalHost));
			fixture.contentType = "application/json";
			auto connection = fixture.connection();
			connection.protocol = protocol;
			connection.mapping.insert("stream", false);
			if (protocol == "anthropic-messages")
			{
				fixture.body
					= R"({"content":[{"type":"thinking","thinking":"Plan","signature":"opaque-state"},{"type":"redacted_thinking","data":"encrypted"},{"type":"text","text":"Answer"}],"stop_reason":"end_turn"})";
			}
			else if (protocol == "gemini")
			{
				fixture.body
					= R"({"candidates":[{"content":{"parts":[{"thought":true,"text":"Plan","thoughtSignature":"opaque-state"},{"text":"Answer"}]},"finishReason":"STOP"}]})";
			}
			else
			{
				fixture.body
					= R"({"status":"completed","output":[{"id":"r1","type":"reasoning","encrypted_content":"opaque-state","summary":[{"type":"summary_text","text":"Plan"}]},{"id":"m1","type":"message","role":"assistant","phase":"final_answer","status":"completed","content":[{"type":"output_text","text":"Answer","annotations":[]}]}]})";
			}
			{
				ChatController controller(store);
				QVERIFY(controller.send(id, connection, "fake-key", "Prompt", "First", error));
				QTRY_VERIFY(!controller.active());
				QCOMPARE(fixture.requests, 1);
			}
			const auto native
				= store.session(id).value("messages").toArray().last().toObject().value("native").toObject();
			QCOMPARE(native.value("origin").toObject(), chatNativeOrigin(connection));
			const auto original = native.value("content").toArray();
			QVERIFY(QJsonDocument(original).toJson().contains("opaque-state"));
			fixture.received.clear();
			{
				ChatController restored(store);
				QVERIFY(restored.send(id, connection, "fake-key", "Prompt", "Continue", error));
				QTRY_VERIFY(!restored.active());
				QCOMPARE(fixture.requests, 2);
			}
			const auto request
				= QJsonDocument::fromJson(fixture.received.mid(fixture.received.indexOf("\r\n\r\n") + 4)).object();
			if (protocol == "gemini")
			{
				QCOMPARE(request.value("contents").toArray()[1].toObject().value("parts").toArray(), original);
			}
			else if (protocol == "anthropic-messages")
			{
				QCOMPARE(request.value("messages").toArray()[1].toObject().value("content").toArray(), original);
			}
			else
			{
				const auto input = request.value("input").toArray();
				for (int index = 0; index < original.size(); ++index)
				{
					QCOMPARE(input[index + 1], original[index]);
				}
			}
			fixture.received.clear();
			connection.model = "another-model";
			ChatController switched(store);
			QVERIFY(switched.send(id, connection, "fake-key", "Prompt", "New model", error));
			QTRY_VERIFY(!switched.active());
			QCOMPARE(fixture.requests, 3);
			QVERIFY(!fixture.received.contains("opaque-state"));
		}
	}

	void cancelAndReuse()
	{
		HttpFixture stalled;
		QVERIFY(stalled.server.listen(QHostAddress::LocalHost));
		stalled.body = m_text;
		stalled.stall = true;
		ChatProviderClient client;
		QSignalSpy updated(&client, &ChatProviderClient::updated);
		QSignalSpy finished(&client, &ChatProviderClient::finished);
		QSignalSpy canceled(&client, &ChatProviderClient::canceled);
		QString error;
		QVERIFY(client.start(stalled.connection(), "fake-key", "Prompt", m_messages, error));
		QTRY_VERIFY(!updated.isEmpty());
		client.cancel();
		QCOMPARE(canceled.count(), 1);
		QVERIFY(!client.active());
		QCOMPARE(finished.count(), 0);
		HttpFixture next;
		QVERIFY(next.server.listen(QHostAddress::LocalHost));
		next.body = m_text + m_done;
		QVERIFY(client.start(next.connection(), "fake-key", "Prompt", m_messages, error));
		QTRY_COMPARE(finished.count(), 1);
		QVERIFY(client.response().complete);
		QCOMPARE(canceled.count(), 1);
	}
};

QTEST_GUILESS_MAIN(ChatProviderClientTest)
#include "ChatProviderClientTest.moc"
