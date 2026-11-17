#include "agent/chat/ChatRequest.h"

#include <QCoreApplication>
#include <QMap>
#include <QSet>
#include <QUrlQuery>

#include "agent/chat/ChatContent.h"
#include "agent/chat/ChatTools.h"

namespace lmms::agent::chat {
namespace {

QString message(const char* source)
{ return QCoreApplication::translate("AgentChat", source); }

QString appendPath(QString base, const QString& suffix)
{
	while (base.endsWith('/'))
	{
		base.chop(1);
	}
	return base + '/' + suffix;
}

bool expandTemplate(QJsonValue& value, const QJsonObject& variables, QSet<QString>& used, QString& error)
{
	if (value.isString())
	{
		const auto text = value.toString();
		if (!text.contains("${"))
		{
			return true;
		}
		const auto field = text.mid(2, text.size() - 3);
		if (!text.startsWith("${") || !text.endsWith('}') || !variables.contains(field))
		{
			error = message(
				"Request template placeholders must name an available request field and occupy the entire value.");
			return false;
		}
		// Substitute the JSON value once. Conversation content is data, not a template.
		value = variables.value(field);
		used.insert(field);
	}
	else if (value.isObject())
	{
		auto object = value.toObject();
		for (auto iterator = object.begin(); iterator != object.end(); ++iterator)
		{
			QJsonValue child = iterator.value();
			if (!expandTemplate(child, variables, used, error))
			{
				return false;
			}
			iterator.value() = child;
		}
		value = object;
	}
	else if (value.isArray())
	{
		auto array = value.toArray();
		for (int index = 0; index < array.size(); ++index)
		{
			QJsonValue child = array[index];
			if (!expandTemplate(child, variables, used, error))
			{
				return false;
			}
			array[index] = child;
		}
		value = array;
	}
	return true;
}

} // namespace

bool buildChatRequest(const ChatConnection& connection, const QString& key, const QString& systemPrompt,
					  const QJsonArray& messages, ChatRequest& request, QString& error, const QJsonArray& tools)
{
	request = {};
	if (!connection.validate(error))
	{
		return false;
	}
	if (key.isEmpty() || key.contains('\r') || key.contains('\n') || connection.model.trimmed().isEmpty()
		|| messages.isEmpty())
	{
		error = message("An API Key, model ID and conversation are required.");
		return false;
	}
	if (!validateChatToolHistory(messages, error))
	{
		return false;
	}
	QJsonArray textMessages;
	QMap<QString, QJsonObject> calls;
	bool previousToolResult = false;
	for (const auto& value : messages)
	{
		auto message = value.toObject();
		const bool toolResult = message.value("role").toString() == "tool";
		for (const auto& value : message.value("tool_calls").toArray())
		{
			const auto call = value.toObject();
			calls.insert(call.value("id").toString(), call);
		}
		if (toolResult)
		{
			const auto call = calls.take(message.value("tool_call_id").toString());
			if (call.contains("providerId"))
			{
				message.insert("provider_call_id", call.value("providerId"));
			}
		}
		QJsonObject encoded;
		if (!encodeChatMessage(connection, message, encoded, error))
		{
			return false;
		}
		if (connection.protocol.endsWith("responses") && encoded.value("output").isArray())
		{
			for (const auto& item : encoded.value("output").toArray())
			{
				textMessages.append(item);
			}
		}
		else if ((toolResult || message.value("role").toString() == "user") && previousToolResult
				 && (connection.protocol == "anthropic-messages" || connection.protocol == "gemini"))
		{
			const auto field = connection.protocol == "gemini" ? "parts" : "content";
			auto previous = textMessages.last().toObject();
			auto parts = previous.value(field).toArray();
			const auto following = encoded.value(field).isString()
				? QJsonArray{QJsonObject{{"type", "text"}, {"text", encoded.value(field)}}}
				: encoded.value(field).toArray();
			for (const auto& part : following)
			{
				parts.append(part);
			}
			previous.insert(field, parts);
			textMessages[textMessages.size() - 1] = previous;
		}
		else
		{
			textMessages.append(encoded);
		}
		previousToolResult = toolResult;
	}
	const bool anthropic = connection.protocol == "anthropic-messages";
	const bool gemini = connection.protocol == "gemini";
	const bool responses = connection.protocol.endsWith("responses");
	const bool azure = connection.protocol.startsWith("azure-");
	const bool custom = connection.protocol == "custom-json";
	const bool stream = connection.mapping.value("stream").toBool(true);
	QUrl url(connection.baseUrl);
	QString path;
	QJsonObject body;
	const auto model = azure && !connection.deployment.isEmpty() ? connection.deployment : connection.model;
	if (gemini)
	{
		auto modelPath = connection.model;
		if (modelPath.startsWith("models/"))
		{
			modelPath.remove(0, 7);
		}
		path = "models/" + modelPath + (stream ? ":streamGenerateContent" : ":generateContent");
		body.insert("contents", textMessages);
		if (!systemPrompt.isEmpty())
		{
			body.insert("systemInstruction", QJsonObject{{"parts", QJsonArray{QJsonObject{{"text", systemPrompt}}}}});
		}
		if (stream)
		{
			QUrlQuery query;
			query.addQueryItem("alt", "sse");
			url.setQuery(query);
		}
	}
	else
	{
		body.insert("model", model);
		body.insert("stream", stream);
		if (anthropic)
		{
			path = "messages";
			body.insert("messages", textMessages);
			const int maxTokens = connection.capabilities.value("maxOutputTokens").toInt(4096);
			if (maxTokens <= 0)
			{
				error = message("Maximum output tokens must be positive.");
				return false;
			}
			body.insert("max_tokens", maxTokens);
			if (!systemPrompt.isEmpty())
			{
				body.insert("system", systemPrompt);
			}
		}
		else if (responses)
		{
			path = "responses";
			body.insert("input", textMessages);
			body.insert("store", false);
			if (!systemPrompt.isEmpty())
			{
				body.insert("instructions", systemPrompt);
			}
		}
		else
		{
			path = "chat/completions";
			if (!systemPrompt.isEmpty())
			{
				textMessages.prepend(QJsonObject{{"role", "system"}, {"content", systemPrompt}});
			}
			body.insert("messages", textMessages);
		}
	}
	if (azure)
	{
		auto base = url.path();
		while (base.endsWith('/'))
		{
			base.chop(1);
		}
		const bool versioned
			= !connection.apiVersion.isEmpty() && connection.apiVersion != "v1" && connection.apiVersion != "preview";
		if (versioned)
		{
			if (!responses
				&& (connection.deployment.isEmpty() || connection.deployment.contains('/')
					|| connection.deployment == ".."))
			{
				error = message("Versioned Azure Chat requests require a valid deployment name.");
				return false;
			}
			if (!base.endsWith("/openai"))
			{
				base = appendPath(base, "openai");
			}
			if (!responses)
			{
				base = appendPath(base, "deployments/" + connection.deployment);
			}
			QUrlQuery query;
			query.addQueryItem("api-version", connection.apiVersion);
			url.setQuery(query);
		}
		else if (!base.endsWith("/openai/v1"))
		{
			base = appendPath(base, "openai/v1");
		}
		if (!versioned && !connection.apiVersion.isEmpty())
		{
			QUrlQuery query;
			query.addQueryItem("api-version", connection.apiVersion);
			url.setQuery(query);
		}
		url.setPath(base);
	}
	const auto overridePath = connection.mapping.value("path").toString();
	if (custom && overridePath.isEmpty())
	{
		error = message("Configure a request path for custom HTTP JSON.");
		return false;
	}
	if (!overridePath.isEmpty())
	{
		path = overridePath;
	}
	if (path.startsWith('/') || path.contains('?') || path.contains('#') || path.contains("://")
		|| path.split('/').contains(".."))
	{
		error = message("Request path must be relative to the configured API address.");
		return false;
	}
	url.setPath(appendPath(url.path(), path));
	if (connection.capabilities.value("temperature").toBool())
	{
		if (gemini)
		{
			body.insert("generationConfig", QJsonObject{{"temperature", connection.temperature}});
		}
		else
		{
			body.insert("temperature", connection.temperature);
		}
	}
	if (connection.reasoning != "auto")
	{
		// Each model declares its exact wire parameters; never infer budgets or
		// switches from a provider/model name or forward an unsupported level.
		const auto parameters
			= connection.capabilities.value("reasoningParameters").toObject().value(connection.reasoning).toObject();
		if (!connection.capabilities.value("reasoningLevels").toArray().contains(connection.reasoning)
			|| parameters.isEmpty())
		{
			error = message("The selected reasoning level has no model parameter mapping.");
			return false;
		}
		for (auto iterator = parameters.begin(); iterator != parameters.end(); ++iterator)
		{
			const QStringList reasoningFields{"reasoning",		  "reasoning_effort", "thinking",
											  "generationConfig", "enable_thinking",  "thinking_budget"};
			if (!reasoningFields.contains(iterator.key()))
			{
				error = message("Reasoning mappings may contain only reasoning parameters.");
				return false;
			}
			if (body.contains(iterator.key()) && !body.value(iterator.key()).isObject())
			{
				error = message("Reasoning parameters conflict with required request fields.");
				return false;
			}
			if (body.value(iterator.key()).isObject() && iterator.value().isObject())
			{
				auto merged = body.value(iterator.key()).toObject();
				const auto extra = iterator.value().toObject();
				for (auto entry = extra.begin(); entry != extra.end(); ++entry)
				{
					merged.insert(entry.key(), entry.value());
				}
				body.insert(iterator.key(), merged);
			}
			else
			{
				body.insert(iterator.key(), iterator.value());
			}
		}
	}
	QJsonArray encodedTools;
	if (!encodeChatToolDefinitions(connection, tools, encodedTools, error))
	{
		return false;
	}
	if (!encodedTools.isEmpty())
	{
		body.insert("tools", encodedTools);
	}
	if (custom && connection.mapping.contains("requestTemplate"))
	{
		auto value = connection.mapping.value("requestTemplate");
		QSet<QString> used;
		if (!value.isObject())
		{
			error = message("The custom request template must be a JSON object containing the messages placeholder.");
			return false;
		}
		if (!expandTemplate(value, body, used, error))
		{
			return false;
		}
		if (!used.contains("messages"))
		{
			error = message("The custom request template must be a JSON object containing the messages placeholder.");
			return false;
		}
		if (!encodedTools.isEmpty() && !used.contains("tools"))
		{
			error = message("The custom request template must include the tools placeholder when tools are enabled.");
			return false;
		}
		body = value.toObject();
	}
	else if (custom)
	{
		for (const auto* field : {"model", "messages", "stream"})
		{
			const auto destination = connection.mapping.value(QString::fromLatin1(field) + "Field").toString(field);
			if (destination != field)
			{
				if (destination.isEmpty() || body.contains(destination))
				{
					error = message("Custom request fields must be nonempty and distinct.");
					return false;
				}
				body.insert(destination, body.take(field));
			}
		}
	}
	QNetworkRequest network(url);
	network.setTransferTimeout(120000);
	network.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
	network.setRawHeader("Content-Type", "application/json");
	network.setRawHeader("Accept", stream ? "text/event-stream" : "application/json");
	if (anthropic)
	{
		network.setRawHeader("anthropic-version",
							 connection.apiVersion.isEmpty() ? QByteArray("2026-10-21")
															 : connection.apiVersion.toUtf8());
	}
	const auto location = connection.mapping.value("keyLocation").toString("header");
	const auto credential = connection.keyPrefix + key;
	if (location == "header")
	{
		network.setRawHeader(connection.keyHeader.toUtf8(), credential.toUtf8());
	}
	else if (location == "query")
	{
		QUrlQuery query(url);
		query.addQueryItem(connection.keyHeader, credential);
		url.setQuery(query);
		network.setUrl(url);
	}
	else if (location == "body" && !body.contains(connection.keyHeader))
	{
		body.insert(connection.keyHeader, credential);
	}
	else
	{
		error = message("Invalid or conflicting API Key location.");
		return false;
	}
	request.network = network;
	request.body = body;
	return true;
}

} // namespace lmms::agent::chat
