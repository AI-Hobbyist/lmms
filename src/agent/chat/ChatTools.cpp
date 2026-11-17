#include "agent/chat/ChatTools.h"

#include <QCoreApplication>
#include <QRegularExpression>
#include <QSet>
#include <QThread>

#include "../CommandSchema.h"
#include "agent/ToolRegistry.h"

namespace lmms::agent::chat {
namespace {

QString message(const char* source)
{ return QCoreApplication::translate("AgentChat", source); }

QString wireName(QString command)
{
	// Escape underscores first so dots and existing underscores cannot collide.
	command.replace("_", "_u");
	command.replace(".", "_d");
	return "lmms_" + command;
}

bool validDefinition(const QJsonObject& definition)
{
	static const QRegularExpression namePattern("^[a-zA-Z0-9_-]{1,64}$");
	return namePattern.match(definition.value("name").toString()).hasMatch()
		&& !definition.value("description").toString().isEmpty() && definition.value("inputSchema").isObject()
		&& definition.value("inputSchema").toObject().value("type") == "object";
}

} // namespace

bool ChatToolCatalog::load(QString& error, const QJsonArray& localTools)
{
	error.clear();
	const auto* application = QCoreApplication::instance();
	if (!application || application->thread() != QThread::currentThread())
	{
		error = message("Load the chat tool catalog on the application thread.");
		return false;
	}
	QJsonArray definitions;
	QHash<QString, QJsonObject> byWireName;
	for (const auto& value : ToolRegistry::tools())
	{
		auto definition = value.toObject();
		const auto command = definition.value("name").toString();
		const auto name = wireName(command);
		definition.insert("name", name);
		if (command.isEmpty() || !validDefinition(definition) || byWireName.contains(name))
		{
			error = message("A registered tool cannot be represented as a model function: %1").arg(command);
			return false;
		}
		definitions.append(definition);
		definition.insert("command", command);
		byWireName.insert(name, definition);
	}
	for (const auto& value : localTools)
	{
		auto definition = value.toObject();
		const auto command = definition.value("name").toString();
		const auto name = wireName(command);
		definition.insert("name", name);
		if (command.isEmpty() || !validDefinition(definition) || byWireName.contains(name))
		{
			error = message("A chat tool cannot be represented as a model function: %1").arg(command);
			return false;
		}
		definitions.append(definition);
		definition.insert("command", command);
		definition.insert("local", true);
		byWireName.insert(name, definition);
	}
	m_definitions = definitions;
	m_byWireName = byWireName;
	return true;
}

QJsonArray ChatToolCatalog::definitions() const
{ return m_definitions; }

QString ChatToolCatalog::nameForCommand(const QString& command) const
{
	const auto name = wireName(command);
	return m_byWireName.value(name).value("command") == command ? name : QString{};
}

bool ChatToolCatalog::isLocal(const QString& command) const
{ return m_byWireName.value(wireName(command)).value("local").toBool(); }

QString ChatToolCatalog::description(const QString& command) const
{ return m_byWireName.value(wireName(command)).value("description").toString(); }

bool ChatToolCatalog::resolve(const QJsonObject& call, ChatToolCall& resolved, QString& error) const
{
	resolved = {};
	error.clear();
	const auto name = call.value("name").toString();
	const auto definition = m_byWireName.constFind(name);
	if (definition == m_byWireName.cend())
	{
		error = message("The model requested a tool that was not offered in this turn.");
		return false;
	}
	if (call.value("id").toString().isEmpty() || !call.value("arguments").isObject())
	{
		error = message("The model returned an incomplete or invalid tool call.");
		return false;
	}
	const auto arguments = call.value("arguments").toObject();
	error = validateArguments(definition.value().value("inputSchema").toObject(), arguments);
	if (!error.isEmpty())
	{
		return false;
	}
	resolved = {call.value("id").toString(), name, definition.value().value("command").toString(), arguments};
	return true;
}

bool encodeChatToolDefinitions(const ChatConnection& connection, const QJsonArray& definitions, QJsonArray& encoded,
							   QString& error)
{
	encoded = {};
	error.clear();
	if (definitions.isEmpty())
	{
		return true;
	}
	const auto protocol = connection.protocol;
	if (protocol == "custom-json"
		&& (connection.mapping.value("toolFormat") != "openai-chat"
			|| connection.mapping.value("toolHistoryFormat") != "openai-chat"
			|| connection.mapping.value("responseToolCallsPath").toString().isEmpty()))
	{
		error = message("Configure custom tool declarations, the call path and result history before enabling tools.");
		return false;
	}
	QSet<QString> names;
	QJsonArray functions;
	for (const auto& value : definitions)
	{
		const auto definition = value.toObject();
		const auto name = definition.value("name").toString();
		if (!validDefinition(definition) || names.contains(name))
		{
			error = message("Chat tool declarations require distinct valid names and object schemas.");
			return false;
		}
		names.insert(name);
		QJsonObject function{{"name", name}, {"description", definition.value("description")}};
		if (protocol == "anthropic-messages")
		{
			function.insert("input_schema", definition.value("inputSchema"));
			functions.append(function);
		}
		else if (protocol == "gemini")
		{
			function.insert("parametersJsonSchema", definition.value("inputSchema"));
			functions.append(function);
		}
		else
		{
			function.insert("parameters", definition.value("inputSchema"));
			// Registry arguments have genuine optional fields. Keep their semantics
			// instead of letting Responses normalize every property into required.
			function.insert("strict", false);
			if (protocol.endsWith("responses"))
			{
				function.insert("type", "function");
				functions.append(function);
			}
			else
			{
				functions.append(QJsonObject{{"type", "function"}, {"function", function}});
			}
		}
	}
	encoded = protocol == "gemini" ? QJsonArray{QJsonObject{{"functionDeclarations", functions}}} : functions;
	return true;
}

} // namespace lmms::agent::chat
