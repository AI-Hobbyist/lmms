#include "agent/ScriptRunner.h"
#include "ScriptExpression.h"
#include "ProjectSnapshot.h"
#include "AudioEngine.h"
#include "Engine.h"
#include "Song.h"
#include "lmmsversion.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QThread>
#include <cmath>
#include <stdexcept>

namespace lmms::agent {
namespace {
struct ScriptFailure
{
	CommandResult result;
};
[[noreturn]] void reject(const QString& message, const QString& code = "invalid_script")
{
	throw ScriptFailure{CommandResult::failure(code, message)};
}
void variableName(const QString& name)
{
	static const QRegularExpression valid("^[A-Za-z_][A-Za-z0-9_]*$");
	if (!valid.match(name).hasMatch() || name == "ticksPerBar" || name == "seed")
	{
		reject("Invalid or reserved variable name: " + name);
	}
}
QJsonArray body(const QJsonObject& object)
{
	if (!object.value("steps").isArray())
	{
		reject("steps must be an array.");
	}
	const auto steps = object.value("steps").toArray();
	if (steps.size() > 20000)
	{
		reject("A script cannot exceed 20000 steps.", "script_limit");
	}
	return steps;
}
QJsonObject initialVariables(const QJsonObject& script, const QJsonObject& overrides)
{
	if (script.contains("vars") && !script.value("vars").isObject())
	{
		reject("vars must be an object.");
	}
	auto result = script.value("vars").toObject();
	for (auto it = overrides.begin(); it != overrides.end(); ++it)
	{
		result.insert(it.key(), it.value());
	}
	if (result.size() > 1024)
	{
		reject("A script cannot exceed 1024 variables.", "script_limit");
	}
	for (auto it = result.begin(); it != result.end(); ++it)
	{
		variableName(it.key());
	}
	return result;
}
bool externalCommand(const QString& name)
{
	for (const auto& descriptor : CommandBus::instance().descriptors())
	{
		if (descriptor.name == name)
		{
			return descriptor.scope == TxScope::None && descriptor.mutability != Mutability::ReadOnly;
		}
	}
	return false;
}
void validateNode(const QJsonObject& node)
{
	int actions = 0;
	for (const auto& key : {"cmd", "loop", "foreach", "if", "call", "assert", "random", "value"})
	{
		actions += node.contains(key);
	}
	if (actions != 1 || (node.contains("let") && node.contains("save")))
	{
		reject("Each step needs exactly one action and at most one let/save.");
	}
}
struct Execution
{
	QJsonObject variables;
	QJsonObject lastResult;
	QJsonArray trace;
	std::uint32_t randomState = 0;
	int count = 0;
	int commands = 0;
	int currentStep = 0;
	QString location;
	QString command;

	void tick()
	{
		currentStep = ++count;
		if (count > 20000)
		{
			reject("Script exceeds 20000 executed steps.", "script_limit");
		}
	}
	void set(const QString& name, const QJsonValue& value)
	{
		variableName(name);
		if (!variables.contains(name) && variables.size() >= 1026)
		{
			reject("Script exceeds 1024 variables.", "script_limit");
		}
		variables.insert(name, value);
	}
	void updateUnits() { variables.insert("ticksPerBar", Engine::getSong()->ticksPerBar()); }
	QJsonValue value(const QJsonValue& input, const QString& argument = {})
	{
		updateUnits();
		return evaluateScriptValue(input, variables, randomState, Engine::getSong()->ticksPerBar(), argument);
	}
	QJsonValue expression(const QJsonValue& input)
	{
		updateUnits();
		return input.isString() ? evaluateScriptExpression(input.toString(), variables, randomState) : value(input);
	}
	void save(const QJsonObject& node, const QJsonValue& result)
	{
		const auto key = node.contains("let") ? "let" : "save";
		if (node.contains(key))
		{
			if (!node.value(key).isString())
			{
				reject("let/save must name a variable.");
			}
			set(node.value(key).toString(), result);
		}
	}
	void dispatch(const QJsonObject& node, bool external = false, bool dryRun = false)
	{
		validateNode(node);
		if (!node.value("cmd").isString() || (node.contains("args") && !node.value("args").isObject()))
		{
			reject("cmd must be a string and args must be an object.");
		}
		command = node.value("cmd").toString();
		// Validate output bindings before a standalone external operation can write files.
		for (const auto& binding : {"let", "save"})
		{
			if (!node.contains(binding))
			{
				continue;
			}
			if (!node.value(binding).isString())
			{
				reject("let/save must name a variable.");
			}
			variableName(node.value(binding).toString());
			if (!variables.contains(node.value(binding).toString()) && variables.size() >= 1026)
			{
				reject("Script exceeds 1024 variables.", "script_limit");
			}
		}
		if (command.startsWith("history.") || command == "agent.runScript" || command == "agent.diffPreview")
		{
			reject("Nested scripts use call; history operations are unavailable within scripts.");
		}
		if (!external && externalCommand(command))
		{
			reject("File writes, configuration and playback require a standalone one-command script.",
				"script_command_not_transactional");
		}
		auto arguments = value(node.value("args").toObject()).toObject();
		if (arguments.contains("dryRun"))
		{
			reject("Set dryRun on the whole script, not on a step.");
		}
		if (external && dryRun)
		{
			arguments.insert("dryRun", true);
		}
		if (command == "midi.addNotes" && arguments.value("notes").toArray().size() > 4096)
		{
			reject("A step cannot add more than 4096 notes.", "script_limit");
		}
		const auto result = CommandBus::instance().execute(command, arguments);
		if (!result.ok)
		{
			throw ScriptFailure{result};
		}
		++commands;
		lastResult = result.data;
		if (trace.size() < 64)
		{
			trace.append(QJsonObject{{"step", currentStep}, {"command", command}});
		}
		save(node, result.data);
	}
	void execute(const QJsonArray& steps, const QString& path = "steps", int depth = 0)
	{
		if (depth > 16)
		{
			reject("Script nesting exceeds 16 levels.", "script_limit");
		}
		for (int index = 0; index < steps.size(); ++index)
		{
			tick();
			location = path + "[" + QString::number(index) + "]";
			command.clear();
			if (!steps[index].isObject())
			{
				reject("Each step must be an object.");
			}
			const auto node = steps[index].toObject();
			validateNode(node);
			if (node.contains("cmd"))
			{
				dispatch(node);
				continue;
			}
			if (node.contains("value"))
			{
				if (!node.contains("let") && !node.contains("save"))
				{
					reject("value needs let/save.");
				}
				save(node, value(node.value("value")));
				continue;
			}
			if (node.contains("assert"))
			{
				const auto assertion = node.value("assert").toObject();
				if (!assertion.contains("expr"))
				{
					reject("assert requires expr.");
				}
				if (!scriptBoolean(expression(assertion.value("expr"))))
				{
					reject(assertion.value("msg").toString("Script assertion failed."), "script_assertion_failed");
				}
				continue;
			}
			if (node.contains("random"))
			{
				const auto random = node.value("random").toObject();
				if (random.contains("seed"))
				{
					const auto seed = scriptNumber(value(random.value("seed")));
					if (std::floor(seed) != seed || seed < -2147483648.0 || seed > 2147483647.0)
					{
						reject("random seed must be a 32-bit integer.");
					}
					randomState = static_cast<std::uint32_t>(static_cast<int>(seed));
				}
				const auto minimum = scriptNumber(value(random.value("a")));
				const auto maximum = scriptNumber(value(random.value("b")));
				set(random.value("var").toString(), scriptRandom(randomState, minimum, maximum));
				continue;
			}
			if (node.contains("if"))
			{
				const auto condition = node.value("if").toObject();
				if (!condition.contains("expr"))
				{
					reject("if requires expr.");
				}
				if (scriptBoolean(expression(condition.value("expr"))))
				{
					execute(body(node), location, depth + 1);
				}
				else if (node.contains("else"))
				{
					const auto alternative = node.value("else");
					if (!alternative.isArray() && !alternative.isObject())
					{
						reject("else must contain steps.");
					}
					execute(alternative.isArray() ? alternative.toArray() : body(alternative.toObject()),
						location + ".else", depth + 1);
				}
				continue;
			}
			if (node.contains("call"))
			{
				const auto call = node.value("call").toObject();
				const auto child = ScriptRunner::loadBuiltIn(call.value("script").toString());
				if (child.isEmpty())
				{
					reject("Unknown built-in script.", "script_not_found");
				}
				if (call.contains("params") && !call.value("params").isObject())
				{
					reject("params must be an object.");
				}
				const auto parameters = value(call.value("params").toObject()).toObject();
				const auto parent = variables;
				variables = initialVariables(child, parameters);
				variables.insert("seed", parent.value("seed"));
				execute(body(child), location + ".call", depth + 1);
				const auto returned = variables;
				variables = parent;
				save(node, returned);
				continue;
			}
			const bool foreach = node.contains("foreach");
			const auto loop = node.value(foreach ? "foreach" : "loop").toObject();
			const auto name = loop.value("var").toString();
			variableName(name);
			QJsonArray values;
			if (foreach)
			{
				const auto input = value(loop.value("in"));
				if (!input.isArray())
				{
					reject("foreach in must be an array.");
				}
				values = input.toArray();
			}
			else
			{
				const auto from = scriptNumber(value(loop.value("from")));
				const auto to = scriptNumber(value(loop.value("to")));
				const auto step
					= scriptNumber(value(loop.value("step").isUndefined() ? QJsonValue(1) : loop.value("step")));
				if (step == 0 || (to > from && step < 0) || (to < from && step > 0))
				{
					reject("Invalid loop step.");
				}
				for (double current = from; step > 0 ? current < to : current > to; current += step)
				{
					if (values.size() >= 512)
					{
						reject("Loop exceeds 512 iterations.", "script_limit");
					}
					if (!std::isfinite(current + step) || current + step == current)
					{
						reject("Loop does not progress.");
					}
					values.append(current);
				}
			}
			if (values.size() > 512)
			{
				reject("Loop exceeds 512 iterations.", "script_limit");
			}
			const auto children = body(node);
			const bool existed = variables.contains(name);
			const auto previous = variables.value(name);
			const auto parentPath = location;
			for (int iteration = 0; iteration < values.size(); ++iteration)
			{
				tick();
				set(name, values[iteration]);
				execute(children, parentPath + ".iteration:" + QString::number(iteration), depth + 1);
			}
			if (existed)
			{
				variables.insert(name, previous);
			}
			else
			{
				variables.remove(name);
			}
		}
	}
};
struct BatchGuard
{
	CommandBus& bus;
	int depth;
	~BatchGuard()
	{
		while (bus.batchDepth() > depth)
		{
			bus.endBatch(false);
		}
	}
};
}

QStringList ScriptRunner::builtInScripts()
{
	return {"four_on_floor_drums", "pop_chord_progression", "arpeggio_16th", "bassline_root_octave", "humanize_groove",
		"scale_snap", "arrange_verse_to_chorus", "mix_gain_staging", "render_preview"};
}
QJsonObject ScriptRunner::loadBuiltIn(QString name)
{
	if (name == "four_on_floor")
	{
		name = "four_on_floor_drums";
	}
	if (!builtInScripts().contains(name))
	{
		return {};
	}
	QFile file(":/agent/scripts/" + name + ".json");
	if (!file.open(QIODevice::ReadOnly))
	{
		return {};
	}
	const auto document = QJsonDocument::fromJson(file.readAll());
	return document.isObject() ? document.object() : QJsonObject{};
}
CommandResult ScriptRunner::run(const QJsonObject& script, const QJsonObject& variables, int seed, bool dryRun)
{
	const auto* application = QCoreApplication::instance();
	if (!application || application->thread() != QThread::currentThread())
	{
		return CommandResult::failure("wrong_thread", "Scripts must run on the application thread.");
	}
	if (!Engine::getSong() || !Engine::audioEngine())
	{
		return CommandResult::failure("engine_unavailable", "The song and audio engine are unavailable.");
	}
	QElapsedTimer elapsed;
	elapsed.start();
	Execution execution;
	execution.randomState = static_cast<std::uint32_t>(seed);
	CommandResult result;
	try
	{
		const auto steps = body(script);
		execution.variables = initialVariables(script, variables);
		execution.variables.insert("seed", seed);
		const bool external
			= steps.size() == 1 && steps[0].isObject() && externalCommand(steps[0].toObject().value("cmd").toString());
		if (external)
		{
			execution.tick();
			execution.location = "steps[0]";
			execution.dispatch(steps[0].toObject(), true, dryRun);
			result = CommandResult::success({{"externalOperation", true}, {"lastResult", execution.lastResult}});
		}
		else
		{
			auto audioGuard = Engine::audioEngine()->requestChangesGuard();
			auto& bus = CommandBus::instance();
			BatchGuard guard{bus, bus.batchDepth()};
			if (!bus.beginBatch(script.value("name").toString("Agent script")))
			{
				reject("Cannot begin the script transaction.", "transaction_unavailable");
			}
			const auto before = projectFields();
			execution.execute(steps);
			const auto diff = projectDiff("agent.runScript", before, projectFields());
			const auto finish = bus.endBatch(!dryRun);
			if (!finish.ok)
			{
				throw ScriptFailure{finish};
			}
			result = CommandResult::success({{"diff", diff}, {"lastResult", execution.lastResult}});
		}
		result.data.insert("vars", execution.variables);
	}
	catch (const ScriptFailure& failure)
	{
		result = failure.result;
	}
	catch (const std::exception& error)
	{
		result = CommandResult::failure("script_expression_error", QString::fromUtf8(error.what()));
	}
	if (!result.ok)
	{
		result.data.insert("failedStep", execution.currentStep);
		result.data.insert("location", execution.location);
		result.data.insert("command", execution.command);
	}
	result.data.insert("stepsExecuted", execution.count);
	result.data.insert("commandsExecuted", execution.commands);
	result.data.insert("trace", execution.trace);
	result.data.insert("seed", seed);
	result.data.insert("version", LMMS_VERSION);
	result.data.insert("dryRun", dryRun);
	result.data.insert("elapsedMs", static_cast<double>(elapsed.elapsed()));
	return result;
}

void registerScriptCommands(CommandBus& bus)
{
	CommandDescriptor run;
	run.name = "agent.runScript";
	run.summary = "Run bounded JSON music steps with one project undo and optional dryRun.";
	run.mutability = Mutability::Mutating;
	run.argsSchema = {{"type", "object"}, {"additionalProperties", false},
		{"properties",
			QJsonObject{{"script",
							QJsonObject{{"anyOf",
								QJsonArray{QJsonObject{{"type", "object"}}, QJsonObject{{"type", "string"}}}}}},
				{"vars", QJsonObject{{"type", "object"}}},
				{"seed", QJsonObject{{"type", "integer"}, {"minimum", -2147483648.0}, {"maximum", 2147483647.0}}}}},
		{"required", QJsonArray{"script"}}};
	run.handler = [](const QJsonObject& arguments) {
		const auto input = arguments.value("script");
		const auto script = input.isString() ? ScriptRunner::loadBuiltIn(input.toString()) : input.toObject();
		if (script.isEmpty())
		{
			return CommandResult::failure("script_not_found", "The script is empty or unknown.");
		}
		const auto seed = arguments.value("seed").isUndefined() ? script.value("seed") : arguments.value("seed");
		if (!seed.isUndefined()
			&& (!seed.isDouble() || std::floor(seed.toDouble()) != seed.toDouble() || seed.toDouble() < -2147483648.0
				|| seed.toDouble() > 2147483647.0))
		{
			return CommandResult::failure("invalid_script", "seed must be a 32-bit integer.");
		}
		return ScriptRunner::run(
			script, arguments.value("vars").toObject(), seed.toInt(), arguments.value("dryRun").toBool());
	};
	bus.registerCommand(run);
	CommandDescriptor preview;
	preview.name = "agent.diffPreview";
	preview.summary = "Preview project command steps without retaining changes or history.";
	preview.mutability = Mutability::Mutating;
	preview.argsSchema = {{"type", "object"}, {"additionalProperties", false},
		{"properties",
			QJsonObject{{"commands",
				QJsonObject{{"type", "array"}, {"maxItems", 20000}, {"items", QJsonObject{{"type", "object"}}}}}}},
		{"required", QJsonArray{"commands"}}};
	preview.handler = [](const QJsonObject& arguments) {
		return ScriptRunner::run({{"name", "Diff preview"}, {"steps", arguments.value("commands")}}, {}, 0, true);
	};
	bus.registerCommand(preview);
}
}
