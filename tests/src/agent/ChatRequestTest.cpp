#include "agent/chat/ChatRequest.h"

#include <QJsonDocument>
#include <QUrlQuery>
#include <QtTest>

#include "agent/CommandBus.h"
#include "agent/ToolRegistry.h"
#include "agent/chat/ChatContent.h"
#include "agent/chat/ChatTools.h"

using namespace lmms::agent::chat;

class ChatRequestTest : public QObject
{
	Q_OBJECT
private slots:
	void registryCatalogDoesNotExecute()
	{
		static int executions = 0;
		executions = 0;
		ChatToolCatalog before;
		QString error;
		QVERIFY2(before.load(error), qPrintable(error));
		lmms::agent::CommandDescriptor descriptor;
		descriptor.name = "chat.catalog_probe";
		descriptor.summary = "Probe catalog metadata without executing this handler.";
		descriptor.argsSchema
			= {{"type", "object"},
			   {"additionalProperties", false},
			   {"properties", QJsonObject{{"value", QJsonObject{{"type", "integer"}, {"minimum", 0}, {"maximum", 9}}}}},
			   {"required", QJsonArray{"value"}}};
		descriptor.handler = [](const QJsonObject&) {
			++executions;
			return lmms::agent::CommandResult::success();
		};
		QVERIFY(lmms::agent::CommandBus::instance().registerCommand(descriptor));
		ChatToolCatalog catalog;
		QVERIFY2(catalog.load(error), qPrintable(error));
		QCOMPARE(catalog.definitions().size(), lmms::agent::ToolRegistry::tools().size());
		QVERIFY(!catalog.nameForCommand("agent.runScript").isEmpty());
		QVERIFY(catalog.nameForCommand("agent.getContext").isEmpty());
		const auto name = catalog.nameForCommand(descriptor.name);
		QCOMPARE(name, QStringLiteral("lmms_chat_dcatalog_uprobe"));
		const QJsonObject call{
			{"id", "call-1"}, {"name", name}, {"arguments", QJsonObject{{"value", 7}}}, {"command", "project.clear"}};
		ChatToolCall resolved;
		QVERIFY(!before.resolve(call, resolved, error));
		QVERIFY2(catalog.resolve(call, resolved, error), qPrintable(error));
		QCOMPARE(resolved.command, descriptor.name);
		QCOMPARE(resolved.wireName, name);
		QCOMPARE(resolved.arguments.value("value").toInt(), 7);
		for (const auto& arguments : QJsonArray{QJsonObject{}, QJsonObject{{"value", 10}}, QJsonObject{{"value", "7"}},
												QJsonObject{{"value", 1}, {"extra", true}}, QJsonArray{7}})
		{
			auto invalid = call;
			invalid.insert("arguments", arguments);
			QVERIFY(!catalog.resolve(invalid, resolved, error));
			QVERIFY(resolved.command.isEmpty());
			QVERIFY(!error.isEmpty());
		}
		auto unknown = call;
		unknown.insert("name", descriptor.name);
		QVERIFY(!catalog.resolve(unknown, resolved, error));
		QCOMPARE(executions, 0);
	}

	void protocolToolDeclarations()
	{
		const QJsonObject schema{{"type", "object"},
								 {"properties", QJsonObject{{"value", QJsonObject{{"type", "integer"}}}}}};
		const QJsonArray tools{
			QJsonObject{{"name", "lmms_fixture"}, {"description", "Fixture tool"}, {"inputSchema", schema}}};
		const QJsonArray history{QJsonObject{{"role", "user"}, {"content", "Hello"}}};
		for (const auto& protocol : ChatConnection::protocols())
		{
			auto connection = ChatConnection::create();
			connection.protocol = protocol;
			connection.baseUrl = "https://example.com/v1";
			connection.model = "fixture-model";
			if (protocol == "custom-json")
			{
				connection.mapping
					= {{"path", "generate"},
					   {"toolFormat", "openai-chat"},
					   {"toolHistoryFormat", "openai-chat"},
					   {"responseToolCallsPath", "calls"},
					   {"requestTemplate", QJsonObject{{"history", "${messages}"}, {"functions", "${tools}"}}}};
			}
			ChatRequest request;
			QString error;
			QVERIFY2(buildChatRequest(connection, "fake-key", "", history, request, error, tools),
					 qPrintable(protocol + error));
			auto declarations = request.body.value(protocol == "custom-json" ? "functions" : "tools").toArray();
			QCOMPARE(declarations.size(), 1);
			if (protocol == "gemini")
			{
				declarations = declarations[0].toObject().value("functionDeclarations").toArray();
			}
			auto function = declarations[0].toObject();
			if (protocol != "gemini" && protocol != "anthropic-messages" && !protocol.endsWith("responses"))
			{
				QCOMPARE(function.value("type").toString(), QStringLiteral("function"));
				function = function.value("function").toObject();
			}
			QCOMPARE(function.value("name").toString(), QStringLiteral("lmms_fixture"));
			const auto field = protocol == "gemini"
				? "parametersJsonSchema"
				: (protocol == "anthropic-messages" ? "input_schema" : "parameters");
			QCOMPARE(function.value(field).toObject(), schema);
			if (protocol != "gemini" && protocol != "anthropic-messages")
			{
				QCOMPARE(function.value("strict"), QJsonValue(false));
			}
			if (protocol == "custom-json")
			{
				connection.mapping.insert("requestTemplate", QJsonObject{{"history", "${messages}"}});
				QVERIFY(!buildChatRequest(connection, "fake-key", "", history, request, error, tools));
				connection.mapping.remove("requestTemplate");
				connection.mapping.remove("toolFormat");
				QVERIFY(!buildChatRequest(connection, "fake-key", "", history, request, error, tools));
			}
		}
	}

	void invalidToolDeclarations()
	{
		auto connection = ChatConnection::create();
		connection.baseUrl = "https://example.com/v1";
		connection.model = "fixture-model";
		const QJsonObject valid{
			{"name", "fixture"}, {"description", "Fixture tool"}, {"inputSchema", QJsonObject{{"type", "object"}}}};
		const QJsonArray history{QJsonObject{{"role", "user"}, {"content", "Hello"}}};
		for (const auto& tools : {QJsonArray{valid, valid},
								  QJsonArray{QJsonObject{{"name", "has.dot"},
														 {"description", "Fixture tool"},
														 {"inputSchema", QJsonObject{{"type", "object"}}}}},
								  QJsonArray{QJsonObject{{"name", "fixture"},
														 {"description", "Fixture tool"},
														 {"inputSchema", QJsonObject{{"type", "array"}}}}}})
		{
			ChatRequest request;
			QString error;
			QVERIFY(!buildChatRequest(connection, "fake-key", "", history, request, error, tools));
			QVERIFY(request.body.isEmpty());
			QVERIFY(!error.isEmpty());
		}
	}

	void customRequestTemplate()
	{
		auto connection = ChatConnection::create();
		connection.protocol = "custom-json";
		connection.baseUrl = "https://example.com/v1";
		connection.model = "fixture-model";
		connection.mapping = {{"path", "generate"}, {"stream", false}};
		const QJsonArray history{QJsonObject{{"role", "user"}, {"content", "${not-a-variable}"}}};
		const QJsonObject bodyTemplate{
			{"payload", QJsonObject{{"history", "${messages}"}, {"model_id", "${model}"}}},
			{"options", QJsonArray{QJsonObject{{"streaming", "${stream}"}}, 7, QJsonValue(QJsonValue::Null)}}};
		connection.mapping.insert("requestTemplate", bodyTemplate);
		ChatRequest request;
		QString error;
		QVERIFY2(buildChatRequest(connection, "fake-key", "Prompt", history, request, error), qPrintable(error));
		const auto payload = request.body.value("payload").toObject();
		QCOMPARE(payload.value("model_id").toString(), connection.model);
		const auto messages = payload.value("history").toArray();
		QCOMPARE(messages.size(), 2);
		QCOMPARE(messages.first().toObject().value("content").toString(), QStringLiteral("Prompt"));
		QCOMPARE(messages.last().toObject().value("content").toString(), QStringLiteral("${not-a-variable}"));
		QVERIFY(request.body.value("options").toArray()[0].toObject().value("streaming").isBool());
		QVERIFY(!request.body.value("options").toArray()[0].toObject().value("streaming").toBool());
		QVERIFY(request.body.value("options").toArray()[2].isNull());
		for (const auto& invalid :
			 QJsonArray{QJsonArray{}, QJsonObject{}, QJsonObject{{"history", "${messages}"}, {"bad", "${missing}"}},
						QJsonObject{{"history", "prefix ${messages}"}}})
		{
			connection.mapping.insert("requestTemplate", invalid);
			QVERIFY(!buildChatRequest(connection, "fake-key", "Prompt", history, request, error));
			QVERIFY(!error.isEmpty());
			QVERIFY(request.body.isEmpty());
		}
	}

	void toolHistoryFormats()
	{
		const QJsonArray calls{
			QJsonObject{{"id", "a"}, {"name", "one"}, {"arguments", QJsonObject{}}, {"providerId", ""}},
			QJsonObject{{"id", "b"}, {"name", "two"}, {"arguments", QJsonObject{}}, {"providerId", "wire-b"}}};
		const QJsonArray messages{
			QJsonObject{{"role", "user"}, {"content", "Hello"}},
			QJsonObject{{"role", "assistant"}, {"content", ""}, {"tool_calls", calls}},
			QJsonObject{{"role", "tool"},
						{"tool_call_id", "b"},
						{"name", "two"},
						{"is_error", true},
						{"content", QJsonObject{{"reason", "denied"}}}},
			QJsonObject{
				{"role", "tool"}, {"tool_call_id", "a"}, {"name", "one"}, {"content", QJsonObject{{"notes", 3}}}}};
		for (const auto& protocol : ChatConnection::protocols())
		{
			auto connection = ChatConnection::create();
			connection.baseUrl = "https://example.com/v1";
			connection.model = "fixture-model";
			connection.protocol = protocol;
			if (protocol == "custom-json")
			{
				connection.mapping = {{"path", "generate"}, {"toolHistoryFormat", "openai-chat"}};
			}
			ChatRequest request;
			QString error;
			QVERIFY2(buildChatRequest(connection, "fake-key", "", messages, request, error),
					 qPrintable(protocol + ": " + error));
			if (protocol == "anthropic-messages")
			{
				const auto history = request.body.value("messages").toArray();
				QCOMPARE(history.size(), 3);
				const auto results = history[2].toObject().value("content").toArray();
				QCOMPARE(results.size(), 2);
				QCOMPARE(results[0].toObject().value("tool_use_id").toString(), QStringLiteral("b"));
				QVERIFY(results[0].toObject().value("is_error").toBool());
				QCOMPARE(QJsonDocument::fromJson(results[1].toObject().value("content").toString().toUtf8())
							 .object()
							 .value("notes")
							 .toInt(),
						 3);
			}
			else if (protocol == "gemini")
			{
				const auto history = request.body.value("contents").toArray();
				QCOMPARE(history.size(), 3);
				const auto assistant = history[1].toObject().value("parts").toArray();
				QVERIFY(!assistant[0].toObject().value("functionCall").toObject().contains("id"));
				const auto results = history[2].toObject().value("parts").toArray();
				QCOMPARE(results.size(), 2);
				const auto first = results[0].toObject().value("functionResponse").toObject();
				QCOMPARE(first.value("id").toString(), QStringLiteral("wire-b"));
				QVERIFY(first.value("response").toObject().contains("error"));
				QVERIFY(!results[1].toObject().value("functionResponse").toObject().contains("id"));
			}
			else if (protocol.endsWith("responses"))
			{
				const auto input = request.body.value("input").toArray();
				QCOMPARE(input.size(), 5);
				QCOMPARE(input[1].toObject().value("type").toString(), QStringLiteral("function_call"));
				QCOMPARE(input[3].toObject().value("type").toString(), QStringLiteral("function_call_output"));
				QCOMPARE(input[3].toObject().value("call_id").toString(), QStringLiteral("b"));
			}
			else
			{
				const auto history = request.body.value("messages").toArray();
				QCOMPARE(history.size(), 4);
				QCOMPARE(history[1].toObject().value("tool_calls").toArray().size(), 2);
				QCOMPARE(history[2].toObject().value("role").toString(), QStringLiteral("tool"));
				QCOMPARE(history[2].toObject().value("tool_call_id").toString(), QStringLiteral("b"));
			}
			if (protocol == "anthropic-messages" || protocol == "gemini")
			{
				auto followup = messages;
				followup.append(QJsonObject{{"role", "user"}, {"content", "Continue"}});
				QVERIFY(buildChatRequest(connection, "fake-key", "", followup, request, error));
				const auto history = request.body.value(protocol == "gemini" ? "contents" : "messages").toArray();
				QCOMPARE(history.size(), 3);
				const auto parts
					= history.last().toObject().value(protocol == "gemini" ? "parts" : "content").toArray();
				QCOMPARE(parts.size(), 3);
				QCOMPARE(parts.last().toObject().value("text").toString(), QStringLiteral("Continue"));
			}
		}
	}

	void invalidToolHistory()
	{
		auto connection = ChatConnection::create();
		connection.baseUrl = "https://example.com/v1";
		connection.model = "fixture-model";
		const QJsonObject call{{"id", "a"}, {"name", "one"}, {"arguments", QJsonObject{}}};
		const QJsonObject assistant{{"role", "assistant"}, {"content", ""}, {"tool_calls", QJsonArray{call}}};
		const QJsonObject result{{"role", "tool"}, {"tool_call_id", "a"}, {"name", "one"}, {"content", "Success"}};
		for (const auto& messages : {QJsonArray{result}, QJsonArray{assistant}, QJsonArray{assistant, result, result},
									 QJsonArray{assistant, QJsonObject{{"role", "user"}, {"content", "Unresolved"}}}})
		{
			ChatRequest request;
			QString error;
			QVERIFY(!buildChatRequest(connection, "fake-key", "", messages, request, error));
			QVERIFY(!error.isEmpty());
			QVERIFY(request.body.isEmpty());
		}
		connection.protocol = "custom-json";
		connection.mapping.insert("path", "generate");
		ChatRequest request;
		QString error;
		QVERIFY(!buildChatRequest(connection, "fake-key", "", QJsonArray{assistant, result}, request, error));
	}

	void nativeOriginIsolation()
	{
		auto connection = ChatConnection::create();
		connection.baseUrl = "https://example.com/v1";
		connection.model = "fixture-model";
		connection.protocol = "gemini";
		const QJsonObject native{
			{"origin", chatNativeOrigin(connection)},
			{"content", QJsonArray{QJsonObject{{"text", "Answer"}, {"thoughtSignature", "opaque-state"}}}}};
		const QJsonArray messages{QJsonObject{{"role", "assistant"}, {"content", "Answer"}, {"native", native}}};
		ChatRequest request;
		QString error;
		QVERIFY(buildChatRequest(connection, "fake-key", "", messages, request, error));
		QCOMPARE(request.body.value("contents").toArray().first().toObject().value("parts"), native.value("content"));
		connection.id = ChatConnection::create().id;
		QVERIFY(!buildChatRequest(connection, "fake-key", "", messages, request, error));
		QVERIFY(!error.isEmpty());
		connection.protocol = "openai-chat";
		QVERIFY(!buildChatRequest(connection, "fake-key", "", messages, request, error));
	}

	void protocolFormats()
	{
		const QJsonArray messages{QJsonObject{{"role", "user"}, {"content", QStringLiteral("编写旋律")}},
								  QJsonObject{{"role", "assistant"}, {"content", "Previous answer"}}};
		for (const auto& protocol : ChatConnection::protocols())
		{
			auto connection = ChatConnection::create();
			connection.protocol = protocol;
			connection.baseUrl = "https://example.com/v1/";
			connection.model = "fixture-model";
			if (protocol == "custom-json")
			{
				connection.mapping = {{"path", "generate"}, {"messagesField", "conversation"}};
			}
			ChatRequest request;
			QString error;
			QVERIFY2(buildChatRequest(connection, "fake-key", "System prompt", messages, request, error),
					 qPrintable(error));
			QCOMPARE(request.network.rawHeader("Authorization"), QByteArray("Bearer fake-key"));
			QCOMPARE(request.network.attribute(QNetworkRequest::RedirectPolicyAttribute).toInt(),
					 int(QNetworkRequest::ManualRedirectPolicy));
			QVERIFY(!request.body.contains("temperature"));
			QVERIFY(!request.body.contains("reasoning_effort"));
			QVERIFY(!request.body.contains("tools"));
			if (protocol == "gemini")
			{
				QCOMPARE(request.network.url().path(),
						 QStringLiteral("/v1/models/fixture-model:streamGenerateContent"));
				QCOMPARE(QUrlQuery(request.network.url()).queryItemValue("alt"), QStringLiteral("sse"));
				QCOMPARE(request.body.value("contents").toArray()[1].toObject().value("role").toString(),
						 QStringLiteral("model"));
				QVERIFY(request.body.contains("systemInstruction"));
			}
			else if (protocol == "anthropic-messages")
			{
				QCOMPARE(request.network.url().path(), QStringLiteral("/v1/messages"));
				QCOMPARE(request.body.value("system").toString(), QStringLiteral("System prompt"));
				QCOMPARE(request.body.value("messages").toArray(), messages);
				QVERIFY(request.body.value("max_tokens").toInt() > 0);
			}
			else if (protocol.endsWith("responses"))
			{
				QVERIFY(request.network.url().path().endsWith("/responses"));
				QCOMPARE(request.body.value("input").toArray(), messages);
				QCOMPARE(request.body.value("instructions").toString(), QStringLiteral("System prompt"));
				QVERIFY(!request.body.value("store").toBool(true));
			}
			else
			{
				const auto field = protocol == "custom-json" ? "conversation" : "messages";
				QCOMPARE(request.body.value(field).toArray().size(), 3);
				QCOMPARE(request.body.value(field).toArray().first().toObject().value("role").toString(),
						 QStringLiteral("system"));
			}
		}
	}

	void authenticationAndCapabilities()
	{
		auto connection = ChatConnection::create();
		connection.baseUrl = "https://example.com/v1";
		connection.model = "model";
		connection.reasoning = "high";
		connection.capabilities
			= {{"temperature", true},
			   {"reasoningLevels", QJsonArray{"high"}},
			   {"reasoningParameters", QJsonObject{{"high", QJsonObject{{"reasoning_effort", "high"}}}}}};
		const QJsonArray messages{QJsonObject{{"role", "user"}, {"content", "Hello"}}};
		ChatRequest request;
		QString error;
		QVERIFY(buildChatRequest(connection, "test-key", "Prompt", messages, request, error));
		QCOMPARE(request.body.value("temperature").toDouble(), 0.7);
		QCOMPARE(request.body.value("reasoning_effort").toString(), QStringLiteral("high"));
		connection.mapping.insert("keyLocation", "query");
		connection.keyHeader = "key";
		connection.keyPrefix.clear();
		QVERIFY(buildChatRequest(connection, "a+b&c=?中文", "Prompt", messages, request, error));
		QCOMPARE(QUrlQuery(request.network.url()).queryItemValue("key"), QStringLiteral("a+b&c=?中文"));
		QVERIFY(!request.network.hasRawHeader("key"));
		connection.mapping.insert("keyLocation", "body");
		QVERIFY(buildChatRequest(connection, "test-key", "Prompt", messages, request, error));
		QCOMPARE(request.body.value("key").toString(), QStringLiteral("test-key"));
		QVERIFY(!request.network.url().hasQuery());
		connection.capabilities.remove("reasoningParameters");
		QVERIFY(!buildChatRequest(connection, "test-key", "Prompt", messages, request, error));
		QVERIFY(request.body.isEmpty());
		connection.reasoning = "auto";
		connection.keyHeader = "model";
		QVERIFY(!buildChatRequest(connection, "test-key", "Prompt", messages, request, error));
	}

	void azureAndInvalidInputs()
	{
		auto connection = ChatConnection::create();
		connection.baseUrl = "https://resource.openai.azure.com";
		connection.protocol = "azure-chat";
		connection.model = "model";
		connection.deployment = "deployment-name";
		connection.apiVersion = "2024-10-21";
		const QJsonArray messages{QJsonObject{{"role", "user"}, {"content", "Hello"}}};
		ChatRequest request;
		QString error;
		QVERIFY(buildChatRequest(connection, "fake-key", "", messages, request, error));
		QCOMPARE(request.network.url().path(), QStringLiteral("/openai/deployments/deployment-name/chat/completions"));
		QCOMPARE(QUrlQuery(request.network.url()).queryItemValue("api-version"), connection.apiVersion);
		connection.protocol = "azure-responses";
		connection.apiVersion = "v1";
		QVERIFY(buildChatRequest(connection, "fake-key", "", messages, request, error));
		QCOMPARE(request.network.url().path(), QStringLiteral("/openai/v1/responses"));
		QCOMPARE(request.body.value("model").toString(), connection.deployment);
		connection.mapping.insert("path", "https://other.example/collect");
		QVERIFY(!buildChatRequest(connection, "fake-key", "", messages, request, error));
		connection.mapping = {};
		QVERIFY(!buildChatRequest(connection, "fake-key", "",
								  QJsonArray{QJsonObject{{"role", "system"}, {"content", "imported"}}}, request,
								  error));
		QVERIFY(!buildChatRequest(connection, "fake-key", "",
								  QJsonArray{QJsonObject{{"role", "user"}, {"content", QJsonArray{}}}}, request,
								  error));
	}
};

QTEST_GUILESS_MAIN(ChatRequestTest)
#include "ChatRequestTest.moc"
