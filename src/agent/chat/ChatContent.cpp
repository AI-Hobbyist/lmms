#include "agent/chat/ChatContent.h"

#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QMap>

#include "agent/chat/ChatAttachment.h"

namespace lmms::agent::chat {
namespace {

QString invalidContent()
{ return QCoreApplication::translate("AgentChat", "Unsupported or invalid attachment content for this protocol."); }

bool encodePart(const ChatConnection& connection, const QJsonObject& part, QJsonObject& encoded)
{
	const auto type = part.value("type").toString();
	const auto protocol = connection.protocol;
	const bool gemini = protocol == "gemini";
	const bool anthropic = protocol == "anthropic-messages";
	const bool responses = protocol.endsWith("responses");
	if (type == "text" && part.value("text").isString())
	{
		encoded = {{"text", part.value("text")}};
		if (!gemini)
		{
			encoded.insert("type", responses ? "input_text" : "text");
		}
		return true;
	}
	ChatAttachment attachment;
	attachment.name = part.value("name").toString();
	attachment.mimeType = part.value("mimeType").toString();
	attachment.type = type;
	const auto data = part.value("data").toString();
	const auto bytes = QByteArray::fromBase64Encoding(data.toLatin1(), QByteArray::AbortOnBase64DecodingErrors);
	attachment.size = bytes.decoded.size();
	QString error;
	if (!bytes || data.isEmpty() || !supportsChatAttachment(connection, attachment, error))
	{
		return false;
	}
	const auto uri = "data:" + attachment.mimeType + ";base64," + data;
	if (gemini)
	{
		encoded = {{"inlineData", QJsonObject{{"mimeType", attachment.mimeType}, {"data", data}}}};
	}
	else if (anthropic)
	{
		encoded = {{"type", type == "image" ? "image" : "document"},
				   {"source", QJsonObject{{"type", "base64"}, {"media_type", attachment.mimeType}, {"data", data}}}};
	}
	else if (type == "image")
	{
		encoded = responses ? QJsonObject{{"type", "input_image"}, {"image_url", uri}}
							: QJsonObject{{"type", "image_url"}, {"image_url", QJsonObject{{"url", uri}}}};
	}
	else if (type == "audio")
	{
		encoded = {{"type", "input_audio"},
				   {"input_audio",
					QJsonObject{{"data", data}, {"format", attachment.mimeType == "audio/mpeg" ? "mp3" : "wav"}}}};
	}
	else if (type == "file")
	{
		encoded = responses
			? QJsonObject{{"type", "input_file"}, {"filename", attachment.name}, {"file_data", uri}}
			: QJsonObject{{"type", "file"}, {"file", QJsonObject{{"filename", attachment.name}, {"file_data", uri}}}};
	}
	else
	{
		return false;
	}
	return true;
}

QString invalidTools()
{
	return QCoreApplication::translate("AgentChat",
									   "Invalid, incomplete or unsupported tool history for this protocol.");
}

bool collectCalls(const QJsonArray& calls, QMap<QString, QString>& pending)
{
	for (const auto& value : calls)
	{
		const auto call = value.toObject();
		const auto id = call.value("id").toString();
		const auto name = call.value("name").toString();
		if (id.isEmpty() || name.isEmpty() || pending.contains(id) || !call.value("arguments").isObject())
		{
			return false;
		}
		pending.insert(id, name);
	}
	return true;
}

QString toolProtocol(const ChatConnection& connection)
{
	if (connection.protocol == "custom-json")
	{
		return connection.mapping.value("toolHistoryFormat").toString() == "openai-chat" ? QStringLiteral("openai-chat")
																						 : QString{};
	}
	return connection.protocol;
}

bool encodeToolResult(const ChatConnection& connection, const QJsonObject& message, QJsonObject& encoded)
{
	const auto protocol = toolProtocol(connection);
	const auto id = message.value("tool_call_id").toString();
	const auto name = message.value("name").toString();
	const auto content = message.value("content");
	if (protocol.isEmpty() || id.isEmpty() || (!content.isString() && !content.isObject() && !content.isArray()))
	{
		return false;
	}
	const auto text = content.isString()
		? content.toString()
		: QString::fromUtf8((content.isObject() ? QJsonDocument(content.toObject()) : QJsonDocument(content.toArray()))
								.toJson(QJsonDocument::Compact));
	if (protocol == "anthropic-messages")
	{
		encoded = {{"role", "user"},
				   {"content",
					QJsonArray{QJsonObject{{"type", "tool_result"},
										   {"tool_use_id", id},
										   {"content", text},
										   {"is_error", message.value("is_error").toBool()}}}}};
	}
	else if (protocol == "gemini")
	{
		if (name.isEmpty())
		{
			return false;
		}
		QJsonObject response;
		if (message.value("is_error").toBool())
		{
			response = {{"error", content}};
		}
		else if (content.isObject())
		{
			response = content.toObject();
		}
		else
		{
			response = {{"result", content}};
		}
		QJsonObject function{{"name", name}, {"response", response}};
		const auto providerId = message.value("provider_call_id").toString(id);
		if (!providerId.isEmpty())
		{
			function.insert("id", providerId);
		}
		encoded = {{"role", "user"}, {"parts", QJsonArray{QJsonObject{{"functionResponse", function}}}}};
	}
	else if (protocol.endsWith("responses"))
	{
		encoded = {{"type", "function_call_output"}, {"call_id", id}, {"output", text}};
	}
	else
	{
		encoded = {{"role", "tool"}, {"tool_call_id", id}, {"content", text}};
	}
	return true;
}

bool encodeAssistantCalls(const ChatConnection& connection, const QJsonObject& message, QJsonObject& encoded)
{
	const auto protocol = toolProtocol(connection);
	const auto calls = message.value("tool_calls").toArray();
	QMap<QString, QString> pending;
	if (protocol.isEmpty() || calls.isEmpty() || !collectCalls(calls, pending)
		|| (!message.value("content").isUndefined() && !message.value("content").isString()))
	{
		return false;
	}
	const auto text = message.value("content").toString();
	QJsonArray parts;
	if (!text.isEmpty() && (protocol == "gemini" || protocol == "anthropic-messages" || protocol.endsWith("responses")))
	{
		if (protocol.endsWith("responses"))
		{
			parts.append(QJsonObject{{"role", "assistant"}, {"content", text}});
		}
		else if (protocol == "gemini")
		{
			parts.append(QJsonObject{{"text", text}});
		}
		else
		{
			parts.append(QJsonObject{{"type", "text"}, {"text", text}});
		}
	}
	for (const auto& value : calls)
	{
		const auto call = value.toObject();
		const auto id = call.value("id");
		const auto name = call.value("name");
		const auto arguments = call.value("arguments").toObject();
		const auto json = QString::fromUtf8(QJsonDocument(arguments).toJson(QJsonDocument::Compact));
		if (protocol == "gemini")
		{
			QJsonObject function{{"name", name}, {"args", arguments}};
			const auto providerId = call.contains("providerId") ? call.value("providerId").toString() : id.toString();
			if (!providerId.isEmpty())
			{
				function.insert("id", providerId);
			}
			parts.append(QJsonObject{{"functionCall", function}});
		}
		else if (protocol == "anthropic-messages")
		{
			parts.append(QJsonObject{{"type", "tool_use"}, {"id", id}, {"name", name}, {"input", arguments}});
		}
		else if (protocol.endsWith("responses"))
		{
			parts.append(QJsonObject{{"type", "function_call"}, {"call_id", id}, {"name", name}, {"arguments", json}});
		}
		else
		{
			parts.append(QJsonObject{
				{"id", id}, {"type", "function"}, {"function", QJsonObject{{"name", name}, {"arguments", json}}}});
		}
	}
	if (protocol == "gemini")
	{
		encoded = {{"role", "model"}, {"parts", parts}};
	}
	else if (protocol == "anthropic-messages")
	{
		encoded = {{"role", "assistant"}, {"content", parts}};
	}
	else if (protocol.endsWith("responses"))
	{
		encoded = {{"output", parts}};
	}
	else
	{
		encoded = {{"role", "assistant"}, {"content", text}, {"tool_calls", parts}};
	}
	return true;
}

} // namespace

bool validateChatToolHistory(const QJsonArray& messages, QString& error)
{
	QMap<QString, QString> pending;
	for (const auto& value : messages)
	{
		const auto item = value.toObject();
		const auto role = item.value("role").toString();
		if (item.contains("tool_calls") && !item.value("tool_calls").isArray())
		{
			error = invalidTools();
			return false;
		}
		if (role == "tool")
		{
			const auto id = item.value("tool_call_id").toString();
			const auto name = item.value("name").toString();
			if (!pending.contains(id) || (!name.isEmpty() && pending.value(id) != name))
			{
				error = invalidTools();
				return false;
			}
			pending.remove(id);
		}
		else if (!pending.isEmpty()
				 || (role == "assistant" && !collectCalls(item.value("tool_calls").toArray(), pending)))
		{
			error = invalidTools();
			return false;
		}
	}
	if (!pending.isEmpty())
	{
		error = invalidTools();
		return false;
	}
	return true;
}

QJsonObject chatNativeOrigin(const ChatConnection& connection)
{
	return {{"connectionId", connection.id},
			{"protocol", connection.protocol},
			{"model", connection.model},
			{"baseUrl", connection.baseUrl},
			{"deployment", connection.deployment}};
}

bool encodeChatMessage(const ChatConnection& connection, const QJsonObject& message, QJsonObject& encoded,
					   QString& error)
{
	encoded = {};
	error.clear();
	const auto role = message.value("role").toString();
	const auto content = message.value("content");
	if (role == "tool")
	{
		if (!encodeToolResult(connection, message, encoded))
		{
			error = invalidTools();
			return false;
		}
		return true;
	}
	if (message.contains("native"))
	{
		const auto native = message.value("native").toObject();
		const auto parts = native.value("content").toArray();
		if (role != "assistant" || native.value("origin").toObject() != chatNativeOrigin(connection) || parts.isEmpty())
		{
			error = invalidContent();
			return false;
		}
		if (connection.protocol == "gemini")
		{
			encoded = {{"role", "model"}, {"parts", parts}};
		}
		else if (connection.protocol == "anthropic-messages")
		{
			encoded = {{"role", "assistant"}, {"content", parts}};
		}
		else if (connection.protocol.endsWith("responses"))
		{
			// Responses has multiple output items per assistant turn. The request
			// builder expands this internal bundle into the flat input array.
			encoded = {{"output", parts}};
		}
		else if (connection.protocol != "custom-json" && parts.size() == 1)
		{
			encoded = parts.first().toObject();
		}
		else
		{
			error = invalidContent();
			return false;
		}
		return true;
	}
	if (role == "assistant" && !message.value("tool_calls").toArray().isEmpty())
	{
		if (!encodeAssistantCalls(connection, message, encoded))
		{
			error = invalidTools();
			return false;
		}
		return true;
	}
	if ((role != "user" && role != "assistant") || message.contains("attachments")
		|| (message.contains("tool_calls") && !message.value("tool_calls").toArray().isEmpty()))
	{
		error = invalidContent();
		return false;
	}
	QJsonArray parts;
	if (content.isString())
	{
		parts.append(QJsonObject{{"type", "text"}, {"text", content}});
	}
	else if (content.isArray() && !content.toArray().isEmpty())
	{
		parts = content.toArray();
	}
	else
	{
		error = invalidContent();
		return false;
	}
	QJsonArray nativeParts;
	QStringList textParts;
	for (const auto& value : parts)
	{
		const auto part = value.toObject();
		const bool text = part.value("type").toString() == "text";
		if ((!text && (role != "user" || connection.protocol == "custom-json")))
		{
			error = invalidContent();
			return false;
		}
		QJsonObject native;
		if (!encodePart(connection, part, native))
		{
			error = invalidContent();
			return false;
		}
		nativeParts.append(native);
		if (text)
		{
			textParts.append(part.value("text").toString());
		}
	}
	if (connection.protocol == "gemini")
	{
		encoded = {{"role", role == "assistant" ? "model" : "user"}, {"parts", nativeParts}};
	}
	else
	{
		encoded = {{"role", role}, {"content", content.isString() ? content : QJsonValue(nativeParts)}};
		if (role == "assistant" || connection.protocol == "custom-json")
		{
			encoded.insert("content", textParts.join("\n\n"));
		}
	}
	return true;
}

} // namespace lmms::agent::chat
