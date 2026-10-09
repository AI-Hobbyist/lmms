#include "HighLevelCommands.h"
#include "agent/CommandBus.h"
#include "agent/ScriptRunner.h"
#include "agent/ToolRegistry.h"
#include "Engine.h"
#include "Song.h"
#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QMap>
#include <QRegularExpression>
#include <QSet>
#include <QUuid>
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <vector>

namespace lmms::agent {
namespace {
QJsonObject integer(int minimum = 0, int maximum = 2147483647)
{
	return {{"type", "integer"}, {"minimum", minimum}, {"maximum", maximum}};
}
QJsonObject number(double minimum, double maximum)
{
	return {{"type", "number"}, {"minimum", minimum}, {"maximum", maximum}};
}
QJsonObject string()
{
	return {{"type", "string"}};
}
QJsonObject schema(const QJsonObject& properties, const QJsonArray& required = {})
{
	return {{"type", "object"}, {"properties", properties}, {"required", required}, {"additionalProperties", false}};
}
QJsonObject step(const QString& name, const QJsonObject& args, const QString& variable = {})
{
	QJsonObject result{{"cmd", name}, {"args", args}};
	if (!variable.isEmpty())
	{
		result.insert("let", variable);
	}
	return result;
}
QJsonObject note(int key, int position, int length, int volume)
{
	return {{"key", key}, {"position", position}, {"length", length}, {"volume", volume}};
}
CommandResult execute(const QJsonArray& steps, const QJsonObject& arguments, const QString& label)
{
	return ScriptRunner::run(
		{{"name", label}, {"steps", steps}}, {}, arguments.value("seed").toInt(), arguments.value("dryRun").toBool());
}
CommandResult invalid(const QString& message)
{
	return CommandResult::failure("invalid_arguments", message);
}
std::vector<int> chord(const QString& symbol, int root)
{
	static const QRegularExpression roman("^([b#]?)([ivIV]+)(maj7|m7|7|dim|m)?$");
	static const QRegularExpression named("^([A-G])([b#]?)(maj7|m7|7|dim|m)?$");
	int base = root;
	bool minor = false;
	QString quality;
	const auto degree = roman.match(symbol);
	if (degree.hasMatch())
	{
		const auto token = degree.captured(2);
		const QStringList degrees{"I", "II", "III", "IV", "V", "VI", "VII"};
		const int index = degrees.indexOf(token.toUpper());
		if (index < 0)
		{
			throw std::runtime_error("Unknown Roman chord degree.");
		}
		const int pitches[]{0, 2, 4, 5, 7, 9, 11};
		base += pitches[index] + (degree.captured(1) == "b" ? -1 : degree.captured(1) == "#" ? 1 : 0);
		minor = token == token.toLower();
		quality = degree.captured(3);
	}
	else
	{
		const auto match = named.match(symbol);
		if (!match.hasMatch())
		{
			throw std::runtime_error("Unsupported chord symbol: " + symbol.toStdString());
		}
		const QStringList letters{"C", "D", "E", "F", "G", "A", "B"};
		const int pitches[]{0, 2, 4, 5, 7, 9, 11};
		base = root / 12 * 12 + pitches[letters.indexOf(match.captured(1))]
			+ (match.captured(2) == "b"		   ? -1
					: match.captured(2) == "#" ? 1
											   : 0);
		quality = match.captured(3);
	}
	minor = minor || quality == "m" || quality == "m7" || quality == "dim";
	std::vector<int> result{base, base + (minor ? 3 : 4), base + (quality == "dim" ? 6 : 7)};
	if (quality.endsWith("7"))
	{
		result.push_back(base + (quality == "maj7" ? 11 : 10));
	}
	return result;
}
CommandResult compose(const QString& action, const QJsonObject& arguments)
{
	QJsonArray notes;
	const int ticks = Engine::getSong()->ticksPerBar();
	const int bars = arguments.value("bars").toInt(4);
	const int volume = arguments.value("velocity").toInt(90);
	const int root = arguments.value("root").toInt(48);
	if (arguments.contains("rate") && arguments.contains("division"))
	{
		return invalid("Specify rate or division, not both.");
	}
	const int grid = arguments.value("rate").toInt(arguments.value("division").toInt(action == "arpeggio" ? 16 : 8));
	if (192 % grid != 0)
	{
		return invalid("division must evenly divide 192 ticks.");
	}
	const int duration = 192 / grid;
	if (action == "arpeggio" && arguments.contains("track") && arguments.contains("clip")
		&& !arguments.contains("progression"))
	{
		QJsonObject target{{"track", arguments.value("track")}, {"clip", arguments.value("clip")}};
		auto queryArgs = target;
		queryArgs.insert("pageSize", 4096);
		const auto source = CommandBus::instance().execute("midi.getNotes", queryArgs);
		if (!source.ok)
		{
			return source;
		}
		if (source.data.value("total").toInt() > 4096)
		{
			return invalid("Source exceeds 4096 notes.");
		}
		QMap<int, std::vector<int>> groups;
		QMap<int, int> ends;
		for (const auto& entry : source.data.value("notes").toArray())
		{
			const auto item = entry.toObject();
			const int position = item.value("position").toInt();
			groups[position].push_back(item.value("key").toInt());
			ends[position] = std::max(ends.value(position), position + item.value("length").toInt());
		}
		for (auto group = groups.begin(); group != groups.end(); ++group)
		{
			auto keys = group.value();
			std::sort(keys.begin(), keys.end());
			keys.erase(std::unique(keys.begin(), keys.end()), keys.end());
			const auto mode = arguments.value("mode").toString("up");
			if (mode == "down")
			{
				std::reverse(keys.begin(), keys.end());
			}
			if (mode == "updown" && keys.size() > 2)
			{
				const auto original = keys;
				for (int i = static_cast<int>(original.size()) - 2; i > 0; --i)
				{
					keys.push_back(original[i]);
				}
			}
			auto next = group;
			++next;
			const int end
				= next == groups.end() ? ends.value(group.key()) : std::min(next.key(), ends.value(group.key()));
			for (int position = group.key(), index = 0; position < end; position += duration, ++index)
			{
				auto item = note(keys[index % keys.size()], position, std::min(duration, end - position), volume);
				if (mode == "random")
				{
					QJsonArray choices;
					for (const auto key : keys)
					{
						choices.append(key);
					}
					item.insert("key",
						QJsonObject{{"expr",
							"pick(" + QString::fromUtf8(QJsonDocument(choices).toJson(QJsonDocument::Compact)) + ")"}});
				}
				notes.append(item);
				if (notes.size() > 4096)
				{
					return invalid("Arpeggio exceeds 4096 notes.");
				}
			}
		}
		auto add = target;
		add.insert("notes", notes);
		return execute(
			{step("midi.clearNotes", target), step("midi.addNotes", add)}, arguments, "Arpeggiate chord clip");
	}
	try
	{
		if (action == "drumPattern")
		{
			const auto style = arguments.value("style").toString("four_on_floor");
			if (ticks != 192)
			{
				return invalid("Drum templates require 4/4.");
			}
			for (int bar = 0; bar < bars; ++bar)
			{
				for (int beat = 0; beat < 4; ++beat)
				{
					if (style == "four_on_floor" || beat % 2 == 0)
					{
						notes.append(note(36, bar * ticks + beat * 48, 12, volume));
					}
					if (beat == 1 || beat == 3)
					{
						notes.append(note(38, bar * ticks + beat * 48, 12, volume));
					}
				}
				for (int hat = 0; hat < (style == "trap" ? 16 : 8); ++hat)
				{
					notes.append(note(42, bar * ticks + hat * (style == "trap" ? 12 : 24), 6, volume * 2 / 3));
				}
			}
		}
		else
		{
			QJsonArray symbols;
			const auto input = arguments.value("progression");
			if (input.isArray())
			{
				symbols = input.toArray();
			}
			else
			{
				for (const auto& symbol : input.toString("I-V-vi-IV").split('-', Qt::SkipEmptyParts))
				{
					symbols.append(symbol);
				}
			}
			if (symbols.isEmpty() || symbols.size() > 512)
			{
				return invalid("progression needs 1 to 512 chords.");
			}
			std::vector<std::vector<int>> chords;
			for (const auto& symbol : symbols)
			{
				chords.push_back(chord(symbol.toString(), root));
			}
			for (int bar = 0; bar < bars; ++bar)
			{
				auto keys = chords[bar % chords.size()];
				for (int turn = 0; turn < arguments.value("inversion").toInt(); ++turn)
				{
					const int bottom = keys.front();
					keys.erase(keys.begin());
					keys.push_back(bottom + 12);
				}
				if (action == "chordProgression")
				{
					for (const auto key : keys)
					{
						notes.append(note(key, bar * ticks, ticks, volume));
					}
				}
				else
				{
					const auto mode = arguments.value("mode").toString("up");
					if (mode == "down")
					{
						std::reverse(keys.begin(), keys.end());
					}
					if (mode == "updown" && keys.size() > 2)
					{
						const auto original = keys;
						for (int i = static_cast<int>(original.size()) - 2; i > 0; --i)
						{
							keys.push_back(original[i]);
						}
					}
					for (int position = 0, index = 0; position < ticks; position += duration, ++index)
					{
						const int key = action == "bassline"
							? chords[bar % chords.size()].front() - 24 + (index % 2) * 12
							: keys[index % keys.size()];
						auto item = note(key, bar * ticks + position, std::min(duration, ticks - position), volume);
						if (mode == "random" && action == "arpeggio")
						{
							QJsonArray choices;
							for (const auto choice : keys)
							{
								choices.append(choice);
							}
							item.insert("key",
								QJsonObject{{"expr",
									"pick(" + QString::fromUtf8(QJsonDocument(choices).toJson(QJsonDocument::Compact))
										+ ")"}});
						}
						notes.append(item);
					}
				}
				if (notes.size() > 4096)
				{
					return invalid("Composition exceeds 4096 notes.");
				}
			}
		}
	}
	catch (const std::exception& error)
	{
		return invalid(QString::fromUtf8(error.what()));
	}
	if (notes.size() > 4096)
	{
		return invalid("Composition exceeds 4096 notes.");
	}
	QJsonArray steps;
	QJsonValue targetTrack = arguments.value("track");
	QJsonValue targetClip = arguments.value("clip");
	if (targetTrack.isUndefined())
	{
		if (!targetClip.isUndefined())
		{
			return invalid("clip requires track.");
		}
		steps.append(step("track.create", {{"type", "Instrument"}, {"name", arguments.value("name").toString(action)}},
			"createdTrack"));
		targetTrack = "$createdTrack.index";
	}
	if (arguments.contains("instrument"))
	{
		steps.append(step("instrument.load", {{"track", targetTrack}, {"plugin", arguments.value("instrument")}}));
	}
	if (targetClip.isUndefined())
	{
		steps.append(step("clip.create",
			{{"track", targetTrack}, {"position", arguments.value("start").toInt()}, {"length", bars * ticks}},
			"createdClip"));
		targetClip = "$createdClip.index";
	}
	steps.append(step("midi.addNotes", {{"track", targetTrack}, {"clip", targetClip}, {"notes", notes}}, "notes"));
	return execute(steps, arguments, "Compose " + action);
}
CommandResult arrange(const QString& action, const QJsonObject& arguments)
{
	auto& bus = CommandBus::instance();
	const int ticks = Engine::getSong()->ticksPerBar();
	const int start = arguments.value("startBar").toInt() * ticks;
	const int end = arguments.value("endBar").toInt() * ticks;
	const int destination = arguments.value("destinationBar").toInt() * ticks;
	const int amount = action == "insertBars" ? arguments.value("bars").toInt() * ticks : end - start;
	if (amount <= 0)
	{
		return invalid("The bar range or insertion length must be positive.");
	}
	const auto tracks = bus.execute("track.list");
	if (!tracks.ok)
	{
		return tracks;
	}
	QJsonArray steps;
	int serial = 0;
	for (const auto& track : tracks.data.value("tracks").toArray())
	{
		const int index = track.toObject().value("index").toInt();
		const auto clips = bus.execute("clip.list", {{"track", index}});
		if (!clips.ok)
		{
			return clips;
		}
		const auto values = clips.data.value("clips").toArray();
		for (int clipIndex = static_cast<int>(values.size()) - 1; clipIndex >= 0; --clipIndex)
		{
			const auto clip = values[clipIndex].toObject();
			const int position = clip.value("start").toInt();
			const int length = clip.value("length").toInt();
			const int clipEnd = position + length;
			const int offset = clip.value("startTimeOffset").toInt();
			const QJsonObject target{{"track", index}, {"clip", clipIndex}};
			const auto change = [&](const QJsonObject& address, int localOffset, int newLength, int newPosition) {
				auto args = address;
				args.insert("value", false);
				steps.append(step("clip.setAutoResize", args));
				args = address;
				args.insert("value", offset - localOffset);
				steps.append(step("clip.setStartTimeOffset", args));
				args = address;
				args.insert("length", newLength);
				steps.append(step("clip.resize", args));
				args = address;
				args.insert("position", newPosition);
				steps.append(step("clip.setPosition", args));
			};
			const auto copy = [&](int localOffset, int newLength, int newPosition) {
				const auto variable = "sectionClip" + QString::number(serial++);
				auto args = target;
				args.insert("position", newPosition);
				steps.append(step("clip.duplicate", args, variable));
				change({{"track", index}, {"clip", "$" + variable + ".index"}}, localOffset, newLength, newPosition);
			};
			if (action == "duplicateSection")
			{
				const int left = std::max(position, start), right = std::min(clipEnd, end);
				if (right > left)
				{
					copy(left - position, right - left, destination + left - start);
				}
			}
			else if (action == "insertBars")
			{
				if (position >= start)
				{
					auto args = target;
					args.insert("position", position + amount);
					steps.append(step("clip.setPosition", args));
				}
				else if (clipEnd > start)
				{
					copy(start - position, clipEnd - start, start + amount);
					change(target, 0, start - position, position);
				}
			}
			else if (position >= end)
			{
				auto args = target;
				args.insert("position", position - amount);
				steps.append(step("clip.setPosition", args));
			}
			else if (clipEnd > start)
			{
				const int leftLength = std::max(0, start - position);
				const int rightLength = std::max(0, clipEnd - end);
				if (leftLength && rightLength)
				{
					copy(end - position, rightLength, start);
					change(target, 0, leftLength, position);
				}
				else if (leftLength)
				{
					change(target, 0, leftLength, position);
				}
				else if (rightLength)
				{
					change(target, end - position, rightLength, start);
				}
				else
				{
					steps.append(step("clip.remove", target));
				}
			}
		}
	}
	return execute(steps, arguments, "Arrange " + action);
}
}
void registerHighLevelCommands(CommandBus& bus)
{
	for (const auto& action : {"chordProgression", "arpeggio", "drumPattern", "bassline"})
	{
		CommandDescriptor command;
		command.name = "compose." + QString(action);
		command.summary
			= "Compose " + QString(action) + " as native MIDI notes; root uses LMMS C0=0, positions use ticks.";
		command.scope = TxScope::Batch;
		command.mutability = Mutability::Mutating;
		command.argsSchema = schema({{"track", integer()}, {"clip", integer()}, {"name", string()},
			{"instrument", string()}, {"start", integer(0, 9999 * 192)}, {"bars", integer(1, 512)},
			{"root", integer(24, 96)}, {"velocity", integer(1, 200)}, {"division", integer(1, 192)},
			{"rate", integer(1, 192)}, {"inversion", integer(0, 3)}, {"seed", integer(-2147483647 - 1)},
			{"progression",
				QJsonObject{{"anyOf",
					QJsonArray{string(), QJsonObject{{"type", "array"}, {"maxItems", 512}, {"items", string()}}}}}},
			{"style", QJsonObject{{"type", "string"}, {"enum", QJsonArray{"four_on_floor", "rock", "trap"}}}},
			{"mode", QJsonObject{{"type", "string"}, {"enum", QJsonArray{"up", "down", "updown", "random"}}}}});
		command.handler = [action](const QJsonObject& arguments) { return compose(action, arguments); };
		bus.registerCommand(command);
	}
	for (const auto& action : {"quantize", "transpose", "humanize"})
	{
		CommandDescriptor command;
		command.name = "edit." + QString(action);
		command.summary = "Edit selected MIDI notes through midi." + QString(action) + ".";
		for (const auto& native : bus.descriptors())
		{
			if (native.name == "midi." + QString(action))
			{
				command.argsSchema = native.argsSchema;
				break;
			}
		}
		command.scope = TxScope::Batch;
		command.mutability = Mutability::Mutating;
		command.handler = [action](QJsonObject arguments) {
			const auto options = arguments;
			arguments.remove("dryRun");
			if (QString(action) == "humanize" && !arguments.contains("timing") && !arguments.contains("velocity")
				&& !arguments.contains("detune"))
			{
				arguments.insert("timing", 3);
				arguments.insert("velocity", 8);
			}
			return execute({step("midi." + QString(action), arguments)}, options, "Edit " + QString(action));
		};
		bus.registerCommand(command);
	}
	for (const auto& action : {"duplicateSection", "insertBars", "deleteBars"})
	{
		CommandDescriptor command;
		command.name = "arrange." + QString(action);
		command.summary
			= "Arrange all Song tracks using a half-open bar interval; split crossing clips and preserve native offsets.";
		command.scope = TxScope::Batch;
		command.mutability = QString(action) == "deleteBars" ? Mutability::Destructive : Mutability::Mutating;
		command.argsSchema = schema({{"startBar", integer(0, 9999)}, {"endBar", integer(1, 9999)},
										{"destinationBar", integer(0, 9999)}, {"bars", integer(1, 9999)}},
			QString(action) == "insertBars"				? QJsonArray{"startBar", "bars"}
				: QString(action) == "duplicateSection" ? QJsonArray{"startBar", "endBar", "destinationBar"}
														: QJsonArray{"startBar", "endBar"});
		command.handler = [action](const QJsonObject& args) { return arrange(action, args); };
		bus.registerCommand(command);
	}
	CommandDescriptor gain;
	gain.name = "mix.gainStaging";
	gain.summary
		= "Adjust selected mixer gains from caller-provided measured peakDb to targetDb minus headroomDb; no automatic audio analysis.";
	gain.scope = TxScope::Batch;
	gain.mutability = Mutability::Mutating;
	gain.argsSchema
		= schema({{"channels", QJsonObject{{"type", "array"}, {"maxItems", 512}, {"items", integer()}}},
					 {"peakDb", number(-120, 60)}, {"targetDb", number(-120, 0)}, {"headroomDb", number(0, 60)}},
			{"channels", "peakDb"});
	gain.handler = [](const QJsonObject& args) {
		QJsonArray steps;
		QSet<int> seen;
		const double multiplier = std::pow(10.0,
			(args.value("targetDb").toDouble(-6) - args.value("headroomDb").toDouble()
				- args.value("peakDb").toDouble())
				/ 20.0);
		for (const auto& channel : args.value("channels").toArray())
		{
			if (seen.contains(channel.toInt()))
			{
				return invalid("channels must be unique.");
			}
			seen.insert(channel.toInt());
			const auto detail = CommandBus::instance().execute("mixer.getChannel", {{"channel", channel}});
			if (!detail.ok)
			{
				return detail;
			}
			const double volume = detail.data.value("volume").toDouble() * multiplier;
			steps.append(step("mixer.setVolume", {{"channel", channel}, {"value", volume}}));
		}
		return execute(steps, args, "Gain staging");
	};
	bus.registerCommand(gain);
	CommandDescriptor render;
	render.name = "render.preview";
	render.summary
		= "Render a local temporary WAV and return the export task; the caller plays the file and removes it when finished.";
	render.mutability = Mutability::Mutating;
	render.argsSchema = schema(
		{{"range", schema({{"start", integer(0, 9999 * 192)}, {"end", integer(1, 9999 * 192)}}, {"start", "end"})}});
	render.handler = [](const QJsonObject& args) {
		QJsonObject exportArgs{
			{"path", QDir::tempPath() + "/lmms-preview-" + QUuid::createUuid().toString(QUuid::WithoutBraces) + ".wav"},
			{"format", "wav"}};
		if (args.contains("range"))
		{
			exportArgs.insert("range", args.value("range"));
		}
		exportArgs.insert("dryRun", args.value("dryRun").toBool());
		return CommandBus::instance().execute("export.audio", exportArgs);
	};
	bus.registerCommand(render);
	CommandDescriptor variation;
	variation.name = "compose.melodyVariation";
	variation.summary = "Duplicate a MIDI clip, optionally transpose, then apply seeded timing and velocity variation.";
	variation.mutability = Mutability::Mutating;
	variation.scope = TxScope::Batch;
	variation.argsSchema
		= schema({{"track", integer()}, {"clip", integer()}, {"position", integer()}, {"semitones", integer(-127, 127)},
					 {"timing", integer(0, 48)}, {"velocity", integer(0, 200)}, {"seed", integer(-2147483647 - 1)}},
			{"track", "clip"});
	variation.handler = [](QJsonObject args) {
		QJsonObject duplicate{{"track", args.value("track")}, {"clip", args.value("clip")}};
		if (args.contains("position"))
		{
			duplicate.insert("position", args.value("position"));
		}
		const QJsonObject target{{"track", args.value("track")}, {"clip", "$variationClip.index"}};
		QJsonArray steps{step("clip.duplicate", duplicate, "variationClip")};
		if (args.contains("semitones"))
		{
			auto transpose = target;
			transpose.insert("semitones", args.value("semitones"));
			steps.append(step("midi.transpose", transpose));
		}
		auto humanize = target;
		humanize.insert("seed", args.value("seed").toInt());
		humanize.insert("timing", args.value("timing").toInt(3));
		humanize.insert("velocity", args.value("velocity").toInt(8));
		steps.append(step("midi.humanize", humanize));
		return execute(steps, args, "Melody variation");
	};
	bus.registerCommand(variation);
}
}
