#include "agent/chat/ChatResponse.h"

#include <QJsonDocument>
#include <QtTest>

using namespace lmms::agent::chat;

class ChatResponseTest : public QObject
{
	Q_OBJECT
private:
	static ChatStreamEvent event(const QJsonObject& object)
	{ return {"message", QJsonDocument(object).toJson(QJsonDocument::Compact)}; }

private slots:
	void customErrorMapping()
	{
		ChatConnection connection;
		connection.protocol = "custom-json";
		connection.mapping = {{"responseTextPath", "result.text"},
							  {"responseDonePath", "result.done"},
							  {"responseErrorPath", "failures.0.message"}};
		const QJsonObject failure{{"failures", QJsonArray{QJsonObject{{"message", "Rejected"}}}}};
		ChatResponseDecoder json(connection);
		QVERIFY(!json.consumeJson(QJsonDocument(failure).toJson()));
		QVERIFY(json.response().error.contains("Rejected"));
		ChatResponseDecoder stream(connection);
		QVERIFY(!stream.consume(event(failure)));
		QVERIFY(stream.response().error.contains("Rejected"));
		connection.mapping.insert("responseErrorPath", "");
		ChatResponseDecoder invalid(connection);
		QVERIFY(!invalid.consume(event({{"result", QJsonObject{{"text", "Done"}, {"done", true}}}})));
		QVERIFY(!invalid.response().complete);
		QVERIFY(!invalid.response().error.isEmpty());
	}

	void chatChunksAndParallelTools()
	{
		const auto bytes
			= QStringLiteral(
				  "data: "
				  "{\"choices\":[{\"delta\":{\"content\":\"旋律\",\"tool_calls\":[{\"index\":0,\"id\":\"a\","
				  "\"function\":{\"name\":\"one\",\"arguments\":\"{\\\"track\\\":\"}},{\"index\":1,\"id\":\"b\","
				  "\"function\":{\"name\":\"two\",\"arguments\":\"{\\\"note\\\":\"}}]}}]}\n\n"
				  "data: "
				  "{\"choices\":[{\"delta\":{\"tool_calls\":[{\"index\":1,\"function\":{\"arguments\":\"60}\"}},{"
				  "\"index\":0,\"function\":{\"arguments\":\"2}\"}}]},\"finish_reason\":\"tool_calls\"}]}\n\n"
				  "data: [DONE]\n\n")
				  .toUtf8();
		for (int size = 1; size <= bytes.size(); ++size)
		{
			ChatConnection connection;
			ChatResponseDecoder decoder(connection);
			ChatEventStream stream;
			for (int offset = 0; offset < bytes.size(); offset += size)
			{
				for (const auto& item : stream.feed(bytes.mid(offset, size)))
				{
					QVERIFY2(decoder.consume(item), qPrintable(decoder.response().error));
					if (!decoder.response().complete)
					{
						QVERIFY(decoder.response().toolCalls.isEmpty());
					}
				}
			}
			QVERIFY(decoder.finish());
			QCOMPARE(decoder.response().text, QStringLiteral("旋律"));
			QCOMPARE(decoder.response().toolCalls.size(), 2);
			QCOMPARE(decoder.response().toolCalls[0].toObject().value("arguments").toObject().value("track").toInt(),
					 2);
			QCOMPARE(decoder.response().toolCalls[1].toObject().value("arguments").toObject().value("note").toInt(),
					 60);
		}
	}

	void nativeTextProtocols()
	{
		ChatConnection connection;
		connection.protocol = "openai-responses";
		ChatResponseDecoder responses(connection);
		QVERIFY(responses.consume(event({{"type", "response.output_text.delta"}, {"delta", "Hello"}})));
		QVERIFY(responses.consume(event({{"type", "response.reasoning_summary_text.delta"}, {"delta", "Summary"}})));
		QVERIFY(responses.consume(
			event({{"type", "response.completed"}, {"response", QJsonObject{{"status", "completed"}}}})));
		QVERIFY(responses.finish());
		QCOMPARE(responses.response().text, QStringLiteral("Hello"));
		QCOMPARE(responses.response().summary, QStringLiteral("Summary"));
		connection.protocol = "anthropic-messages";
		ChatResponseDecoder anthropic(connection);
		QVERIFY(anthropic.consume(event(
			{{"type", "content_block_delta"}, {"delta", QJsonObject{{"type", "text_delta"}, {"text", "Claude"}}}})));
		QVERIFY(
			anthropic.consume(event({{"type", "message_delta"}, {"delta", QJsonObject{{"stop_reason", "end_turn"}}}})));
		QVERIFY(!anthropic.response().complete);
		QVERIFY(anthropic.consume(event({{"type", "message_stop"}})));
		QVERIFY(anthropic.finish());
		QCOMPARE(anthropic.response().text, QStringLiteral("Claude"));
		connection.protocol = "gemini";
		ChatResponseDecoder gemini(connection);
		QVERIFY(gemini.consume(
			event({{"candidates",
					QJsonArray{QJsonObject{{"content",
											QJsonObject{{"parts",
														 QJsonArray{QJsonObject{{"text", "Thought"}, {"thought", true}},
																	QJsonObject{{"text", "Gemini"}}}}}},
										   {"finishReason", "STOP"}}}}})));
		QVERIFY(gemini.finish());
		QCOMPARE(gemini.response().text, QStringLiteral("Gemini"));
		QCOMPARE(gemini.response().summary, QStringLiteral("Thought"));
	}

	void nativeToolProtocols()
	{
		ChatConnection connection;
		connection.protocol = "openai-responses";
		ChatResponseDecoder responses(connection);
		QVERIFY(responses.consume(event(
			{{"type", "response.output_item.added"},
			 {"output_index", 1},
			 {"item",
			  QJsonObject{{"type", "function_call"}, {"call_id", "call1"}, {"name", "edit"}, {"arguments", ""}}}})));
		QVERIFY(responses.consume(
			event({{"type", "response.function_call_arguments.delta"}, {"output_index", 1}, {"delta", "{\"x\":"}})));
		QVERIFY(responses.consume(
			event({{"type", "response.function_call_arguments.delta"}, {"output_index", 1}, {"delta", "3}"}})));
		QVERIFY(responses.response().toolCalls.isEmpty());
		QVERIFY(responses.consume(
			event({{"type", "response.completed"}, {"response", QJsonObject{{"status", "completed"}}}})));
		QCOMPARE(responses.response().toolCalls[0].toObject().value("arguments").toObject().value("x").toInt(), 3);
		connection.protocol = "anthropic-messages";
		ChatResponseDecoder anthropic(connection);
		QVERIFY(anthropic.consume(
			event({{"type", "content_block_start"},
				   {"index", 2},
				   {"content_block",
					QJsonObject{{"type", "tool_use"}, {"id", "call2"}, {"name", "edit"}, {"input", QJsonObject{}}}}})));
		QVERIFY(anthropic.consume(
			event({{"type", "content_block_delta"},
				   {"index", 2},
				   {"delta", QJsonObject{{"type", "input_json_delta"}, {"partial_json", "{\"x\":4}"}}}})));
		QVERIFY(
			anthropic.consume(event({{"type", "message_delta"}, {"delta", QJsonObject{{"stop_reason", "tool_use"}}}})));
		QVERIFY(anthropic.consume(event({{"type", "message_stop"}})));
		QCOMPARE(anthropic.response().toolCalls[0].toObject().value("arguments").toObject().value("x").toInt(), 4);
	}

	void invalidToolArguments()
	{
		for (const auto& arguments : {QString{}, QStringLiteral("{\"x\":"), QStringLiteral("[]")})
		{
			ChatConnection connection;
			ChatResponseDecoder decoder(connection);
			const QJsonObject call{
				{"index", 0}, {"id", "call"}, {"function", QJsonObject{{"name", "edit"}, {"arguments", arguments}}}};
			const QJsonObject choice{{"delta", QJsonObject{{"tool_calls", QJsonArray{call}}}},
									 {"finish_reason", "tool_calls"}};
			QVERIFY(decoder.consume(event({{"choices", QJsonArray{choice}}})));
			QVERIFY(decoder.response().toolCalls.isEmpty());
			QVERIFY(!decoder.consume({"message", "[DONE]"}));
			QVERIFY(decoder.response().toolCalls.isEmpty());
			QVERIFY(!decoder.response().complete);
		}
	}

	void completeJsonTools()
	{
		const QList<QPair<QString, QByteArray>> examples{
			{"openai-chat",
			 R"({"choices":[{"message":{"content":"","reasoning_content":"Plan","tool_calls":[{"id":"a","function":{"name":"one","arguments":"{}"}},{"id":"b","function":{"name":"two","arguments":"{}"}}]},"finish_reason":"tool_calls"}]})"},
			{"openai-responses",
			 R"({"status":"completed","output":[{"type":"reasoning","summary":[{"type":"summary_text","text":"Plan"}]},{"type":"function_call","call_id":"a","name":"one","arguments":"{}"},{"type":"function_call","call_id":"b","name":"two","arguments":"{}"}]})"},
			{"anthropic-messages",
			 R"({"content":[{"type":"thinking","thinking":"Plan","signature":"signed"},{"type":"tool_use","id":"a","name":"one","input":{}},{"type":"tool_use","id":"b","name":"two","input":{}}],"stop_reason":"tool_use"})"},
			{"gemini",
			 R"({"candidates":[{"content":{"parts":[{"thought":true,"text":"Plan"},{"functionCall":{"id":"a","name":"one","args":{}}},{"functionCall":{"id":"b","name":"two","args":{}}}]},"finishReason":"STOP"}]})"}};
		for (const auto& example : examples)
		{
			auto connection = ChatConnection::create();
			connection.protocol = example.first;
			ChatResponseDecoder decoder(connection);
			QVERIFY2(decoder.consumeJson(example.second), qPrintable(example.first + ": " + decoder.response().error));
			QVERIFY(decoder.finish());
			QCOMPARE(decoder.response().summary, QStringLiteral("Plan"));
			QCOMPARE(decoder.response().toolCalls.size(), 2);
			QCOMPARE(decoder.response().toolCalls[0].toObject().value("name").toString(), QStringLiteral("one"));
			QCOMPARE(decoder.response().toolCalls[1].toObject().value("name").toString(), QStringLiteral("two"));
		}
	}

	void streamedNativeSignatures()
	{
		auto connection = ChatConnection::create();
		connection.protocol = "anthropic-messages";
		ChatResponseDecoder claude(connection);
		QVERIFY(claude.consume(
			event({{"type", "content_block_start"},
				   {"index", 0},
				   {"content_block", QJsonObject{{"type", "thinking"}, {"thinking", ""}, {"signature", ""}}}})));
		for (const auto& delta : {QJsonObject{{"type", "thinking_delta"}, {"thinking", "Plan"}},
								  QJsonObject{{"type", "signature_delta"}, {"signature", "opaque-"}},
								  QJsonObject{{"type", "signature_delta"}, {"signature", "state"}}})
		{
			QVERIFY(claude.consume(event({{"type", "content_block_delta"}, {"index", 0}, {"delta", delta}})));
		}
		QVERIFY(claude.consume(
			event({{"type", "content_block_start"},
				   {"index", 1},
				   {"content_block", QJsonObject{{"type", "redacted_thinking"}, {"data", "encrypted"}}}})));
		QVERIFY(
			claude.consume(event({{"type", "message_delta"}, {"delta", QJsonObject{{"stop_reason", "end_turn"}}}})));
		QVERIFY(claude.consume(event({{"type", "message_stop"}})));
		QCOMPARE(claude.response().nativeContent[0].toObject().value("signature").toString(),
				 QStringLiteral("opaque-state"));
		QCOMPARE(claude.response().nativeContent[0].toObject().value("thinking").toString(), QStringLiteral("Plan"));
		QCOMPARE(claude.response().nativeContent[1].toObject().value("data").toString(), QStringLiteral("encrypted"));
		connection.protocol = "openai-responses";
		ChatResponseDecoder responses(connection);
		const QJsonArray output{QJsonObject{
			{"type", "reasoning"}, {"id", "r1"}, {"encrypted_content", "opaque-state"}, {"summary", QJsonArray{}}}};
		QVERIFY(responses.consume(event(
			{{"type", "response.completed"}, {"response", QJsonObject{{"status", "completed"}, {"output", output}}}})));
		QCOMPARE(responses.response().nativeContent, output);
	}

	void invalidJsonDocuments()
	{
		for (
			const QByteArray bytes :
			{QByteArray("{bad"), QByteArray("[]"), QByteArray("{}"),
			 QByteArray(R"({"choices":[{"message":{"content":"Partial"},"finish_reason":"length"}]})"),
			 QByteArray(
				 R"({"choices":[{"message":{"tool_calls":[{"id":"a","function":{"name":"one","arguments":"[1]"}}]},"finish_reason":"tool_calls"}]})")})
		{
			ChatResponseDecoder decoder(ChatConnection::create());
			QVERIFY(!decoder.consumeJson(bytes));
			QVERIFY(!decoder.response().error.isEmpty());
			QVERIFY(!decoder.response().complete);
			QVERIFY(decoder.response().toolCalls.isEmpty());
		}
		auto connection = ChatConnection::create();
		connection.protocol = "custom-json";
		connection.mapping.insert("responseTextPath", "output.0.text");
		ChatResponseDecoder custom(connection);
		QVERIFY(custom.consumeJson(R"({"output":[{"text":"Answer"}]})"));
		QCOMPARE(custom.response().text, QStringLiteral("Answer"));
		ChatResponseDecoder missing(connection);
		QVERIFY(!missing.consumeJson(R"({"output":[{"text":42}]})"));
	}

	void customToolMapping()
	{
		auto connection = ChatConnection::create();
		connection.protocol = "custom-json";
		connection.mapping = {{"responseTextPath", "answer"},
							  {"responseDonePath", "done"},
							  {"responseToolCallsPath", "calls"},
							  {"toolHistoryFormat", "openai-chat"}};
		const QJsonArray calls{QJsonObject{{"id", "a"}, {"name", "one"}, {"arguments", QJsonObject{}}}};
		ChatResponseDecoder decoder(connection);
		QVERIFY(decoder.consume(event({{"answer", ""}, {"calls", calls}})));
		QVERIFY(decoder.response().toolCalls.isEmpty());
		QVERIFY(decoder.consume(event({{"done", true}})));
		QCOMPARE(decoder.response().toolCalls, calls);
		ChatResponseDecoder invalid(connection);
		QVERIFY(
			!invalid.consumeJson(R"({"answer":"","done":true,"calls":[{"id":"a","name":"one","arguments":"{}"}]})"));
		QVERIFY(invalid.response().toolCalls.isEmpty());
		connection.mapping.remove("toolHistoryFormat");
		ChatResponseDecoder unsupported(connection);
		QVERIFY(!unsupported.consume(event({{"answer", ""}, {"calls", calls}, {"done", true}})));
	}

	void interruptionErrorsAndCustom()
	{
		ChatConnection connection;
		ChatResponseDecoder interrupted(connection);
		QVERIFY(interrupted.consume(
			event({{"choices", QJsonArray{QJsonObject{{"delta", QJsonObject{{"content", "Partial"}}}}}}})));
		QVERIFY(!interrupted.finish());
		QCOMPARE(interrupted.response().text, QStringLiteral("Partial"));
		QVERIFY(!interrupted.response().complete);
		ChatResponseDecoder invalid(connection);
		QVERIFY(!invalid.consume({"message", "{bad"}));
		ChatResponseDecoder noReason(connection);
		QVERIFY(!noReason.consume({"message", "[DONE]"}));
		connection.protocol = "openai-responses";
		ChatResponseDecoder failed(connection);
		QVERIFY(!failed.consume(event({{"type", "response.incomplete"}})));
		connection.protocol = "custom-json";
		connection.mapping = {{"responseTextPath", "output.0.text"}, {"responseDonePath", "finished"}};
		ChatResponseDecoder custom(connection);
		QVERIFY(custom.consume(event({{"output", QJsonArray{QJsonObject{{"text", "Custom"}}}}})));
		QVERIFY(!custom.response().complete);
		QVERIFY(custom.consume(event({{"finished", true}})));
		QVERIFY(custom.finish());
		QCOMPARE(custom.response().text, QStringLiteral("Custom"));
	}
};

QTEST_GUILESS_MAIN(ChatResponseTest)
#include "ChatResponseTest.moc"
