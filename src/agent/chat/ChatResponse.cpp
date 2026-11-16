#include "agent/chat/ChatResponse.h"

#include <QCoreApplication>
#include <QJsonDocument>
#include <QSet>

namespace lmms::agent::chat {
namespace {

QString message(const char* source)
{ return QCoreApplication::translate("AgentChat", source); }

QJsonValue fieldAt(QJsonValue value, const QString& path)
{
	for (const auto& field : path.split('.'))
	{
		bool number = false;
		const int index = field.toInt(&number);
		if (number && value.isArray())
		{
			const auto array = value.toArray();
			value = index >= 0 && index < array.size() ? array.at(index) : QJsonValue(QJsonValue::Undefined);
		}
		else
		{
			value = value.toObject().value(field);
		}
	}
	return value;
}

} // namespace

ChatResponseDecoder::ChatResponseDecoder(ChatConnection connection)
	: m_connection(std::move(connection))
{
}

const ChatResponse& ChatResponseDecoder::response() const
{ return m_response; }

bool ChatResponseDecoder::fail(const QString& error)
{
	if (m_response.error.isEmpty())
	{
		m_response.error = error;
	}
	m_response.complete = false;
	m_response.toolCalls = {};
	m_response.nativeContent = {};
	return false;
}

bool ChatResponseDecoder::complete()
{
	const QStringList normalReasons{"stop", "tool_calls", "end_turn", "tool_use", "stop_sequence", "completed", "STOP"};
	if (!normalReasons.contains(m_response.finishReason))
	{
		return fail(message("Generation did not finish normally: %1.").arg(m_response.finishReason));
	}
	QJsonArray calls;
	QSet<QString> ids;
	for (auto iterator = m_calls.cbegin(); iterator != m_calls.cend(); ++iterator)
	{
		const auto& call = iterator.value();
		QJsonParseError parseError;
		const auto arguments = QJsonDocument::fromJson(call.arguments, &parseError);
		if (call.id.isEmpty() || call.name.isEmpty() || ids.contains(call.id)
			|| parseError.error != QJsonParseError::NoError || !arguments.isObject())
		{
			return fail(message("The model returned an incomplete or invalid tool call."));
		}
		ids.insert(call.id);
		QJsonObject normalized{{"id", call.id}, {"name", call.name}, {"arguments", arguments.object()}};
		if (m_connection.protocol == "gemini")
		{
			normalized.insert("providerId", call.providerId);
		}
		calls.append(normalized);
	}
	// No call is exposed until both the protocol completion marker and all
	// argument objects have been validated. This class never executes tools.
	m_response.toolCalls = calls;
	const auto protocol = m_connection.protocol;
	QJsonArray native;
	if (protocol == "anthropic-messages" || protocol == "gemini" || protocol.endsWith("responses"))
	{
		for (auto iterator = m_nativeBlocks.cbegin(); iterator != m_nativeBlocks.cend(); ++iterator)
		{
			auto block = iterator.value();
			if (protocol == "anthropic-messages" && block.value("type").toString() == "tool_use")
			{
				block.insert("input", QJsonDocument::fromJson(m_calls.value(iterator.key()).arguments).object());
			}
			if (protocol.endsWith("responses") && block.value("type").toString() == "function_call")
			{
				block.insert("arguments", QString::fromUtf8(m_calls.value(iterator.key()).arguments));
			}
			native.append(block);
		}
	}
	else if (protocol != "custom-json")
	{
		QJsonObject assistant{{"role", "assistant"}, {"content", m_response.text}};
		if (!m_response.summary.isEmpty())
		{
			assistant.insert("reasoning_content", m_response.summary);
		}
		QJsonArray tools;
		for (const auto& call : m_calls)
		{
			tools.append(QJsonObject{
				{"id", call.id},
				{"type", "function"},
				{"function", QJsonObject{{"name", call.name}, {"arguments", QString::fromUtf8(call.arguments)}}}});
		}
		if (!tools.isEmpty())
		{
			assistant.insert("tool_calls", tools);
		}
		native.append(assistant);
	}
	m_response.nativeContent = native;
	m_response.complete = true;
	return true;
}

bool ChatResponseDecoder::consume(const ChatStreamEvent& event)
{
	if (!m_response.error.isEmpty())
	{
		return false;
	}
	if (m_response.complete)
	{
		return true;
	}
	const auto protocol = m_connection.protocol;
	if (event.data.trimmed() == "[DONE]"
		&& (protocol == "openai-chat" || protocol == "azure-chat" || protocol == "openai-compatible"))
	{
		return complete();
	}
	QJsonParseError parseError;
	const auto document = QJsonDocument::fromJson(event.data, &parseError);
	if (parseError.error != QJsonParseError::NoError || !document.isObject())
	{
		return fail(message("The model stream contained invalid JSON."));
	}
	const auto object = document.object();
	if (protocol == "custom-json")
	{
		consumeCustom(object);
		if (event.type == "error" && m_response.error.isEmpty())
		{
			return fail(message("Model protocol error: %1").arg(object.value("message").toString()));
		}
		return m_response.error.isEmpty();
	}
	if (object.contains("error") || event.type == "error")
	{
		const auto detail
			= object.value("error").toObject().value("message").toString(object.value("message").toString());
		return fail(message("Model protocol error: %1").arg(detail));
	}
	const auto type = object.value("type").toString(QString::fromUtf8(event.type));
	if (protocol == "anthropic-messages")
	{
		consumeAnthropic(object, type);
	}
	else if (protocol == "gemini")
	{
		consumeGemini(object);
	}
	else if (protocol.endsWith("responses"))
	{
		consumeResponses(object, type);
	}
	else
	{
		consumeChat(object);
	}
	return m_response.error.isEmpty();
}

bool ChatResponseDecoder::consumeJson(const QByteArray& bytes)
{
	QJsonParseError parseError;
	const auto document = QJsonDocument::fromJson(bytes, &parseError);
	if (parseError.error != QJsonParseError::NoError || !document.isObject())
	{
		return fail(message("The model response contained invalid JSON."));
	}
	const auto object = document.object();
	if (m_connection.protocol == "custom-json")
	{
		consumeCustom(object, true);
		return finish();
	}
	if (!object.value("error").isNull() && !object.value("error").isUndefined())
	{
		return fail(
			message("Model protocol error: %1").arg(object.value("error").toObject().value("message").toString()));
	}
	const auto protocol = m_connection.protocol;
	if (protocol == "gemini")
	{
		consumeGemini(object);
		return finish();
	}
	if (protocol == "anthropic-messages")
	{
		if (!object.value("content").isArray())
		{
			return fail(message("The model response did not contain the expected message fields."));
		}
		const auto content = object.value("content").toArray();
		for (int index = 0; index < content.size(); ++index)
		{
			consumeAnthropic({{"index", index}, {"content_block", content[index]}}, "content_block_start");
		}
		m_response.usage = object.value("usage").toObject();
		m_response.finishReason = object.value("stop_reason").toString();
	}
	else if (protocol.endsWith("responses"))
	{
		if (!object.value("output").isArray())
		{
			return fail(message("The model response did not contain the expected message fields."));
		}
		const auto output = object.value("output").toArray();
		for (int index = 0; index < output.size(); ++index)
		{
			const auto item = output[index].toObject();
			m_nativeBlocks[index] = item;
			const auto type = item.value("type").toString();
			if (type == "function_call")
			{
				consumeResponses({{"output_index", index}, {"item", item}}, "response.output_item.done");
			}
			else if (type == "message")
			{
				for (const auto& value : item.value("content").toArray())
				{
					const auto part = value.toObject();
					m_response.text += part.value("type").toString() == "refusal" ? part.value("refusal").toString()
																				  : part.value("text").toString();
				}
			}
			else if (type == "reasoning")
			{
				for (const auto& value : item.value("summary").toArray())
				{
					m_response.summary += value.toObject().value("text").toString();
				}
			}
		}
		m_response.usage = object.value("usage").toObject();
		m_response.finishReason = object.value("status").toString();
	}
	else
	{
		const auto choices = object.value("choices").toArray();
		if (choices.isEmpty() || !choices.first().toObject().value("message").isObject())
		{
			return fail(message("The model response did not contain the expected message fields."));
		}
		consumeChat(object);
	}
	return m_response.error.isEmpty() && complete();
}

bool ChatResponseDecoder::finish()
{
	if (!m_response.error.isEmpty())
	{
		return false;
	}
	return m_response.complete || fail(message("The model stream ended before its completion marker."));
}

void ChatResponseDecoder::consumeChat(const QJsonObject& object)
{
	if (object.value("usage").isObject())
	{
		m_response.usage = object.value("usage").toObject();
	}
	for (const auto& value : object.value("choices").toArray())
	{
		const auto choice = value.toObject();
		if (choice.value("index").toInt() != 0)
		{
			continue;
		}
		const auto delta
			= choice.value("delta").isObject() ? choice.value("delta").toObject() : choice.value("message").toObject();
		m_response.text += delta.value("content").toString();
		m_response.text += delta.value("refusal").toString();
		m_response.summary += delta.value("reasoning_content").toString();
		int toolIndex = 0;
		for (const auto& value : delta.value("tool_calls").toArray())
		{
			const auto tool = value.toObject();
			auto& call = m_calls[tool.value("index").toInt(toolIndex)];
			++toolIndex;
			call.id += tool.value("id").toString();
			const auto function = tool.value("function").toObject();
			call.name += function.value("name").toString();
			call.arguments += function.value("arguments").toString().toUtf8();
		}
		if (choice.value("finish_reason").isString())
		{
			m_response.finishReason = choice.value("finish_reason").toString();
		}
	}
}

void ChatResponseDecoder::consumeResponses(const QJsonObject& object, const QString& type)
{
	const int index = object.value("output_index").toInt();
	if (type == "response.output_text.delta")
	{
		m_response.text += object.value("delta").toString();
	}
	else if (type == "response.reasoning_summary_text.delta")
	{
		m_response.summary += object.value("delta").toString();
	}
	else if (type == "response.output_item.added" || type == "response.output_item.done")
	{
		const auto item = object.value("item").toObject();
		m_nativeBlocks[index] = item;
		if (item.value("type").toString() == "function_call")
		{
			auto& call = m_calls[index];
			call.id = item.value("call_id").toString();
			call.name = item.value("name").toString();
			if (item.value("arguments").isString())
			{
				call.arguments = item.value("arguments").toString().toUtf8();
			}
		}
	}
	else if (type == "response.function_call_arguments.delta")
	{
		m_calls[index].arguments += object.value("delta").toString().toUtf8();
	}
	else if (type == "response.function_call_arguments.done")
	{
		m_calls[index].arguments = object.value("arguments").toString().toUtf8();
	}
	else if (type == "response.completed")
	{
		const auto response = object.value("response").toObject();
		if (response.value("output").isArray())
		{
			m_nativeBlocks.clear();
			const auto output = response.value("output").toArray();
			for (int index = 0; index < output.size(); ++index)
			{
				m_nativeBlocks[index] = output[index].toObject();
			}
		}
		m_response.usage = response.value("usage").toObject();
		m_response.finishReason = response.value("status").toString();
		complete();
	}
	else if (type == "response.failed" || type == "response.incomplete")
	{
		fail(message("Generation did not finish normally: %1.").arg(type));
	}
}

void ChatResponseDecoder::consumeAnthropic(const QJsonObject& object, const QString& type)
{
	const int index = object.value("index").toInt();
	if (type == "content_block_start")
	{
		const auto block = object.value("content_block").toObject();
		m_nativeBlocks[index] = block;
		if (block.value("type").toString() == "tool_use")
		{
			auto& call = m_calls[index];
			call.id = block.value("id").toString();
			call.name = block.value("name").toString();
			if (!block.value("input").isObject())
			{
				fail(message("The model returned an incomplete or invalid tool call."));
				return;
			}
			const auto input = block.value("input").toObject();
			call.arguments = QJsonDocument(input).toJson(QJsonDocument::Compact);
		}
		m_response.text += block.value("text").toString();
		m_response.summary += block.value("thinking").toString();
	}
	else if (type == "content_block_delta")
	{
		const auto delta = object.value("delta").toObject();
		const auto deltaType = delta.value("type").toString();
		if (deltaType == "text_delta")
		{
			m_response.text += delta.value("text").toString();
			auto& block = m_nativeBlocks[index];
			block.insert("text", block.value("text").toString() + delta.value("text").toString());
		}
		else if (deltaType == "thinking_delta")
		{
			m_response.summary += delta.value("thinking").toString();
			auto& block = m_nativeBlocks[index];
			block.insert("thinking", block.value("thinking").toString() + delta.value("thinking").toString());
		}
		else if (deltaType == "signature_delta")
		{
			auto& block = m_nativeBlocks[index];
			block.insert("signature", block.value("signature").toString() + delta.value("signature").toString());
		}
		else if (deltaType == "input_json_delta")
		{
			auto& call = m_calls[index];
			if (!call.streamedArguments)
			{
				call.arguments.clear();
				call.streamedArguments = true;
			}
			call.arguments += delta.value("partial_json").toString().toUtf8();
		}
	}
	else if (type == "message_start")
	{
		m_response.usage = object.value("message").toObject().value("usage").toObject();
	}
	else if (type == "message_delta")
	{
		m_response.finishReason = object.value("delta").toObject().value("stop_reason").toString();
		const auto usage = object.value("usage").toObject();
		for (auto iterator = usage.begin(); iterator != usage.end(); ++iterator)
		{
			m_response.usage.insert(iterator.key(), iterator.value());
		}
	}
	else if (type == "message_stop")
	{
		complete();
	}
}

void ChatResponseDecoder::consumeGemini(const QJsonObject& object)
{
	if (object.value("usageMetadata").isObject())
	{
		m_response.usage = object.value("usageMetadata").toObject();
	}
	const auto blocked = object.value("promptFeedback").toObject().value("blockReason").toString();
	if (!blocked.isEmpty() && blocked != "BLOCK_REASON_UNSPECIFIED")
	{
		fail(message("Generation did not finish normally: %1.").arg(blocked));
		return;
	}
	for (const auto& value : object.value("candidates").toArray())
	{
		const auto candidate = value.toObject();
		if (candidate.value("index").toInt() != 0)
		{
			continue;
		}
		for (const auto& value : candidate.value("content").toObject().value("parts").toArray())
		{
			const auto part = value.toObject();
			m_nativeBlocks[m_nativeBlocks.size()] = part;
			if (part.value("thought").toBool())
			{
				m_response.summary += part.value("text").toString();
			}
			else
			{
				m_response.text += part.value("text").toString();
			}
			if (part.contains("functionCall"))
			{
				const auto function = part.value("functionCall").toObject();
				if (function.contains("args") && !function.value("args").isObject())
				{
					fail(message("The model returned an incomplete or invalid tool call."));
					return;
				}
				const int index = m_calls.size();
				m_calls[index] = {function.value("id").toString(QStringLiteral("gemini_%1").arg(index)),
								  function.value("name").toString(),
								  QJsonDocument(function.value("args").toObject()).toJson(QJsonDocument::Compact)};
				m_calls[index].providerId = function.value("id").toString();
			}
		}
		if (candidate.value("finishReason").isString())
		{
			m_response.finishReason = candidate.value("finishReason").toString();
			complete();
		}
	}
}

void ChatResponseDecoder::consumeCustom(const QJsonObject& object, bool document)
{
	const auto mapping = m_connection.mapping;
	const auto errorPath = mapping.value("responseErrorPath").toString("error");
	if (errorPath.isEmpty())
	{
		fail(message("Configure an error path for custom JSON responses."));
		return;
	}
	const auto problem = fieldAt(object, errorPath);
	if (!problem.isUndefined() && !problem.isNull() && problem != QJsonValue(false) && problem != QJsonValue(""))
	{
		const auto detail = problem.isObject() ? problem.toObject().value("message") : problem;
		if (!detail.isString())
		{
			fail(message("The configured response error path did not contain an error string or message object."));
			return;
		}
		fail(message("Model protocol error: %1").arg(detail.toString()));
		return;
	}
	const auto textPath = mapping.value("responseTextPath").toString();
	const auto donePath = mapping.value("responseDonePath").toString();
	if (textPath.isEmpty() || (!document && donePath.isEmpty()))
	{
		fail(message("Configure text and completion paths for custom JSON responses."));
		return;
	}
	const auto text = fieldAt(object, textPath);
	if (document && !text.isString())
	{
		fail(message("The configured response text path did not contain a string."));
		return;
	}
	m_response.text += text.toString();
	const auto toolsPath = mapping.value("responseToolCallsPath").toString();
	const auto tools = toolsPath.isEmpty() ? QJsonValue(QJsonValue::Undefined) : fieldAt(object, toolsPath);
	if (!tools.isUndefined())
	{
		if (!tools.isArray() || mapping.value("toolHistoryFormat").toString() != "openai-chat")
		{
			fail(message("The model returned an incomplete or invalid tool call."));
			return;
		}
		m_calls.clear();
		const auto calls = tools.toArray();
		for (int index = 0; index < calls.size(); ++index)
		{
			const auto call = calls[index].toObject();
			if (!call.value("arguments").isObject())
			{
				fail(message("The model returned an incomplete or invalid tool call."));
				return;
			}
			m_calls[index] = {call.value("id").toString(), call.value("name").toString(),
							  QJsonDocument(call.value("arguments").toObject()).toJson(QJsonDocument::Compact)};
		}
	}
	const auto done = fieldAt(object, donePath);
	const auto expected = mapping.contains("responseDoneValue") ? mapping.value("responseDoneValue") : QJsonValue(true);
	if ((document && donePath.isEmpty()) || (!done.isUndefined() && done == expected))
	{
		m_response.finishReason = "completed";
		complete();
	}
}

} // namespace lmms::agent::chat
