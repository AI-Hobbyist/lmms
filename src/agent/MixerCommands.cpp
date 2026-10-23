#include "MixerCommands.h"

#include "agent/CommandBus.h"

#include <QJsonArray>
#include <QJsonObject>

#include <algorithm>
#include <cmath>

#include "Engine.h"
#include "Mixer.h"

namespace lmms::agent {
namespace {

CommandResult invalidArguments(const QString& message)
{
	return CommandResult::failure("invalid_arguments", message);
}

CommandResult mixerUnavailable()
{
	return CommandResult::failure("mixer_unavailable", "The mixer model is unavailable.");
}

QString mixerChannelPath(int channelIndex)
{
	return QStringLiteral("song/channel:%1").arg(channelIndex);
}

// Mirrors the shape returned by the existing mixer.get*/mixer.set* commands so
// callers can treat every channel payload identically.
QJsonObject mixerChannelDetail(MixerChannel* channel)
{
	QJsonArray sends;
	for (auto* route : channel->m_sends)
	{
		sends.append(QJsonObject{{"to", route->receiverIndex()}, {"amount", route->amount()->value()}});
	}

	QJsonArray receives;
	for (auto* route : channel->m_receives)
	{
		receives.append(QJsonObject{{"from", route->senderIndex()}, {"amount", route->amount()->value()}});
	}

	QJsonObject detail{{"path", mixerChannelPath(channel->index())}, {"channel", channel->index()},
		{"master", channel->isMaster()}, {"name", channel->m_name}, {"volume", channel->m_volumeModel.value()},
		{"muted", channel->m_muteModel.value()}, {"solo", channel->m_soloModel.value()}, {"sends", sends},
		{"receives", receives}};
	detail.insert(
		"color", channel->color().has_value() ? QJsonValue(channel->color()->name()) : QJsonValue(QJsonValue::Null));
	return detail;
}

bool readChannelIndex(const QJsonObject& arguments, int& channelIndex, QString& error)
{
	const auto value = arguments.value("channel");
	if (!value.isDouble())
	{
		error = "'channel' is required and must be an integer.";
		return false;
	}
	const double raw = value.toDouble();
	if (raw != static_cast<double>(static_cast<int>(raw)))
	{
		error = "'channel' must be an integer.";
		return false;
	}
	channelIndex = static_cast<int>(raw);
	if (channelIndex < 0)
	{
		error = "'channel' must not be negative.";
		return false;
	}
	return true;
}

bool readOptionalName(const QJsonObject& arguments, QString& name, QString& error)
{
	name.clear();
	if (!arguments.contains("name"))
	{
		return true;
	}
	const auto value = arguments.value("name");
	if (!value.isString())
	{
		error = "'name' must be a string.";
		return false;
	}
	name = value.toString().trimmed();
	if (name.size() > 128)
	{
		error = "'name' must not exceed 128 characters.";
		return false;
	}
	return true;
}

bool readOptionalForce(const QJsonObject& arguments, bool& force, QString& error)
{
	force = false;
	if (!arguments.contains("force"))
	{
		return true;
	}
	const auto value = arguments.value("force");
	if (!value.isBool())
	{
		error = "'force' must be a boolean.";
		return false;
	}
	force = value.toBool();
	return true;
}

QJsonObject mixerChannelLevels(MixerChannel* channel)
{
	const float peakLeft = channel->m_peakLeft;
	const float peakRight = channel->m_peakRight;
	const float peak = std::max(peakLeft, peakRight);
	const auto toDb = [](float value) {
		return value > 0.0f ? static_cast<double>(20.0f * std::log10(value)) : -120.0;
	};

	return {{"channel", channel->index()}, {"name", channel->m_name}, {"master", channel->isMaster()},
		{"peakLeft", peakLeft}, {"peakRight", peakRight}, {"peak", peak}, {"peakDb", toDb(peak)},
		{"muted", channel->m_muteModel.value()}, {"solo", channel->m_soloModel.value()}};
}

CommandResult executeMixerGetLevels(const QJsonObject& arguments)
{
	auto* mixer = Engine::mixer();
	if (mixer == nullptr)
	{
		return mixerUnavailable();
	}

	if (arguments.contains("channel"))
	{
		int channelIndex = -1;
		QString error;
		if (!readChannelIndex(arguments, channelIndex, error))
		{
			return invalidArguments(error);
		}
		if (channelIndex >= static_cast<int>(mixer->numChannels()))
		{
			return CommandResult::failure(
				"mixer_channel_not_found", QStringLiteral("Mixer channel %1 does not exist.").arg(channelIndex));
		}
		return CommandResult::success(mixerChannelLevels(mixer->mixerChannel(channelIndex)));
	}

	QJsonArray channels;
	for (int channelIndex = 0; channelIndex < static_cast<int>(mixer->numChannels()); ++channelIndex)
	{
		channels.append(mixerChannelLevels(mixer->mixerChannel(channelIndex)));
	}
	return CommandResult::success(QJsonObject{{"channels", channels}});
}

CommandResult executeMixerAddChannel(const QJsonObject& arguments)
{
	auto* mixer = Engine::mixer();
	if (mixer == nullptr)
	{
		return mixerUnavailable();
	}

	QString name;
	QString error;
	if (!readOptionalName(arguments, name, error))
	{
		return invalidArguments(error);
	}

	const int previousCount = static_cast<int>(mixer->numChannels());
	const int channelIndex = mixer->createChannel();
	const int currentCount = static_cast<int>(mixer->numChannels());
	if (channelIndex != previousCount || currentCount != previousCount + 1)
	{
		return CommandResult::failure(
			"mixer_channel_create_failed", "LMMS did not create the requested mixer channel.");
	}

	auto* channel = mixer->mixerChannel(channelIndex);
	if (!name.isEmpty())
	{
		channel->m_name = name;
	}

	auto detail = mixerChannelDetail(channel);
	detail.insert("numChannels", currentCount);
	return CommandResult::success(detail);
}

CommandResult executeMixerRemoveChannel(const QJsonObject& arguments)
{
	auto* mixer = Engine::mixer();
	if (mixer == nullptr)
	{
		return mixerUnavailable();
	}

	int channelIndex = -1;
	QString error;
	if (!readChannelIndex(arguments, channelIndex, error))
	{
		return invalidArguments(error);
	}

	bool force = false;
	if (!readOptionalForce(arguments, force, error))
	{
		return invalidArguments(error);
	}

	if (channelIndex == 0)
	{
		return CommandResult::failure(
			"mixer_master_channel_protected", "The master channel cannot be removed.");
	}
	if (channelIndex >= static_cast<int>(mixer->numChannels()))
	{
		return CommandResult::failure(
			"mixer_channel_not_found", QStringLiteral("Mixer channel %1 does not exist.").arg(channelIndex));
	}
	if (!force && mixer->isChannelInUse(channelIndex))
	{
		return CommandResult::failure("mixer_channel_in_use",
			QStringLiteral("Mixer channel %1 is still used by a track.").arg(channelIndex),
			"Pass force:true to reroute those tracks to the master channel and remove the channel.");
	}

	mixer->deleteChannel(channelIndex);
	return CommandResult::success(
		QJsonObject{{"removed", channelIndex}, {"numChannels", static_cast<int>(mixer->numChannels())}});
}

CommandResult executeMixerMoveChannel(const QJsonObject& arguments)
{
	auto* mixer = Engine::mixer();
	if (mixer == nullptr)
	{
		return mixerUnavailable();
	}

	int channelIndex = -1;
	QString error;
	if (!readChannelIndex(arguments, channelIndex, error))
	{
		return invalidArguments(error);
	}

	const auto directionValue = arguments.value("direction");
	if (!directionValue.isString())
	{
		return invalidArguments("'direction' is required and must be 'left' or 'right'.");
	}
	const QString direction = directionValue.toString().trimmed().toLower();
	if (direction != "left" && direction != "right")
	{
		return invalidArguments("'direction' must be 'left' or 'right'.");
	}

	const int channelCount = static_cast<int>(mixer->numChannels());
	if (channelIndex >= channelCount)
	{
		return CommandResult::failure(
			"mixer_channel_not_found", QStringLiteral("Mixer channel %1 does not exist.").arg(channelIndex));
	}

	// The master channel is fixed at index 0, so channel 1 has no room to move left.
	if (direction == "left")
	{
		if (channelIndex <= 1)
		{
			return CommandResult::failure("mixer_channel_at_edge",
				QStringLiteral("Mixer channel %1 cannot move left.").arg(channelIndex),
				"Channel 1 cannot move in front of the master channel.");
		}
		mixer->moveChannelLeft(channelIndex);
		auto detail = mixerChannelDetail(mixer->mixerChannel(channelIndex - 1));
		detail.insert("numChannels", channelCount);
		return CommandResult::success(detail);
	}

	if (channelIndex == 0 || channelIndex == channelCount - 1)
	{
		return CommandResult::failure("mixer_channel_at_edge",
			QStringLiteral("Mixer channel %1 cannot move right.").arg(channelIndex),
			"Only channels between the master channel and the last channel can move right.");
	}
	mixer->moveChannelRight(channelIndex);
	auto detail = mixerChannelDetail(mixer->mixerChannel(channelIndex + 1));
	detail.insert("numChannels", channelCount);
	return CommandResult::success(detail);
}

void addCommand(CommandBus& commandBus, const QString& name, const QString& summary, const QJsonObject& properties,
	const QJsonArray& required, Mutability mutability, TxScope scope, CommandHandler handler)
{
	CommandDescriptor descriptor;
	descriptor.name = name;
	descriptor.summary = summary;

	QJsonObject schema{{"type", "object"}, {"properties", properties}};
	if (!required.isEmpty())
	{
		schema.insert("required", required);
	}
	descriptor.argsSchema = schema;
	descriptor.mutability = mutability;
	descriptor.scope = scope;
	descriptor.handler = std::move(handler);
	commandBus.registerCommand(descriptor);
}

} // namespace

void registerMixerCommands(CommandBus& commandBus)
{
	QJsonObject addChannelProperties{{"name", QJsonObject{{"type", "string"}}}};
	addCommand(commandBus, "mixer.addChannel",
		"Create a new mixer channel and return its detail object; optional name and dryRun.", addChannelProperties,
		QJsonArray{}, Mutability::Mutating, TxScope::Single, executeMixerAddChannel);

	QJsonObject removeChannelProperties{{"channel", QJsonObject{{"type", "integer"}, {"minimum", 0}}},
		{"force", QJsonObject{{"type", "boolean"}}}};
	addCommand(commandBus, "mixer.removeChannel",
		"Remove a mixer channel after rerouting its tracks; the master channel is protected.",
		removeChannelProperties, QJsonArray{"channel"}, Mutability::Destructive, TxScope::Single,
		executeMixerRemoveChannel);

	QJsonObject moveChannelProperties{{"channel", QJsonObject{{"type", "integer"}, {"minimum", 0}}},
		{"direction", QJsonObject{{"type", "string"}, {"enum", QJsonArray{"left", "right"}}}}};
	addCommand(commandBus, "mixer.moveChannel",
		"Move a mixer channel one position left or right; the master channel stays at index 0.",
		moveChannelProperties, QJsonArray{"channel", "direction"}, Mutability::Mutating, TxScope::Single,
		executeMixerMoveChannel);

	QJsonObject levelsProperties{{"channel", QJsonObject{{"type", "integer"}, {"minimum", 0}}}};
	addCommand(commandBus, "mixer.getLevels",
		"Read current mixer peak levels (linear and dBFS) for one channel or every channel.",
		levelsProperties, QJsonArray{}, Mutability::ReadOnly, TxScope::None, executeMixerGetLevels);
}

} // namespace lmms::agent
