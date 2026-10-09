#include "agent/ToolRegistry.h"
#include "agent/CommandBus.h"

#include <QJsonDocument>

namespace lmms::agent {
namespace {
QJsonObject definition(const CommandDescriptor& command)
{
	return {{"name", command.name}, {"description", command.summary}, {"inputSchema", command.argsSchema},
		{"annotations",
			QJsonObject{{"readOnlyHint", command.mutability == Mutability::ReadOnly},
				{"destructiveHint", command.mutability == Mutability::Destructive},
				{"idempotentHint", command.mutability == Mutability::ReadOnly}, {"openWorldHint", false}}}};
}

QJsonObject objectSchema(const QJsonObject& properties = {}, const QJsonArray& required = {})
{
	return {{"type", "object"}, {"properties", properties}, {"required", required}, {"additionalProperties", false}};
}
}

QJsonArray ToolRegistry::tools()
{
	QJsonArray result;
	for (const auto& command : CommandBus::instance().descriptors())
	{
		if (command.name.startsWith("agent.") && command.name != "agent.runScript")
		{
			continue;
		}
		result.append(definition(command));
	}
	return result;
}

QJsonObject ToolRegistry::commandHelp(const QString& name)
{
	for (const auto& command : CommandBus::instance().descriptors())
	{
		if (command.name == name)
		{
			auto result = definition(command);
			result.insert("transaction",
				command.scope == TxScope::None		   ? "none"
					: command.scope == TxScope::Single ? "single"
													   : "batch");
			return result;
		}
	}
	return {};
}

QString ToolRegistry::commandManual()
{
	QString result = "# LMMS local command reference\n\nGenerated from the running command registry. "
					 "Positions and lengths use native ticks unless a command says otherwise. "
					 "Track and clip indices follow the current project order. "
					 "Project edits support dryRun and undo; file writes and playback are outside project undo.\n\n";
	for (const auto& command : CommandBus::instance().descriptors())
	{
		result += "## " + command.name + "\n\n" + command.summary + "\n\n```json\n"
			+ QString::fromUtf8(QJsonDocument(command.argsSchema).toJson(QJsonDocument::Indented)) + "```\n\n";
	}
	return result;
}

void registerMetadataCommands(CommandBus& bus)
{
	const QJsonObject string{{"type", "string"}};
	for (const auto& name :
		{QString("listCommands"), QString("searchCommands"), QString("commandHelp"), QString("getContext")})
	{
		CommandDescriptor command;
		command.name = "agent." + name;
		command.summary = "Local registry helper: " + name + ". MCP clients use tools/list for discovery.";
		command.argsSchema = name == "commandHelp" ? objectSchema({{"name", string}}, {"name"})
			: name == "searchCommands"			   ? objectSchema({{"keyword", string}}, {"keyword"})
												   : objectSchema();
		command.handler = [name](const QJsonObject& arguments) {
			if (name == "commandHelp")
			{
				const auto help = ToolRegistry::commandHelp(arguments.value("name").toString());
				return help.isEmpty()
					? CommandResult::failure("unknown_command", "The requested command is not registered.")
					: CommandResult::success(help);
			}
			if (name == "getContext")
			{
				auto& commands = CommandBus::instance();
				const auto song = commands.execute("query.songSummary");
				if (!song.ok)
				{
					return song;
				}
				const auto tracks = commands.execute("track.list");
				if (!tracks.ok)
				{
					return tracks;
				}
				return CommandResult::success({{"song", song.data}, {"tracks", tracks.data.value("tracks")}});
			}
			QJsonArray commands;
			const auto keyword = arguments.value("keyword").toString();
			for (const auto& descriptor : CommandBus::instance().descriptors())
			{
				if (name == "searchCommands" && !descriptor.name.contains(keyword, Qt::CaseInsensitive)
					&& !descriptor.summary.contains(keyword, Qt::CaseInsensitive))
				{
					continue;
				}
				commands.append(definition(descriptor));
			}
			return CommandResult::success({{"commands", commands}});
		};
		bus.registerCommand(command);
	}
}
}
