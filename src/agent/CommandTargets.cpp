#include "CommandTargets.h"

#include <QJsonValue>
#include <QRegularExpression>

#include "Clip.h"
#include "Engine.h"
#include "PatternStore.h"
#include "Song.h"
#include "Track.h"
#include "TrackContainer.h"

namespace lmms::agent {
namespace {

CommandResult invalidArguments(const QString& message)
{
	return CommandResult::failure("invalid_arguments", message);
}

bool readNonNegativeInteger(const QJsonObject& arguments, const char* name, int& result, QString& error)
{
	if (!arguments.contains(name))
	{
		error = QStringLiteral("'%1' is required.").arg(QString::fromUtf8(name));
		return false;
	}
	const auto value = arguments.value(name);
	if (!value.isDouble() || value.toDouble() < 0 || value.toDouble() > 2147483647.0)
	{
		error = QStringLiteral("'%1' must be a non-negative integer.").arg(QString::fromUtf8(name));
		return false;
	}
	result = value.toInt();
	return true;
}

} // namespace

bool resolveParent(Song* song, const QJsonObject& arguments, TrackContainer*& container, CommandResult& failure)
{
	const auto parent = arguments.value("parent").toString("song");
	if (parent == "song")
	{
		container = song;
		return true;
	}
	if (parent == "pattern")
	{
		container = Engine::patternStore();
		return true;
	}
	failure = invalidArguments("'parent' must be song or pattern.");
	return false;
}

bool resolveTrack(Song* song, const QJsonObject& arguments, Track*& track, int& trackIndex, CommandResult& failure)
{
	TrackContainer* container = nullptr;
	if (!resolveParent(song, arguments, container, failure))
	{
		return false;
	}
	QString error;
	const auto selector = arguments.value("track");
	if (selector.isString())
	{
		const auto name = selector.toString();
		const auto path = QRegularExpression("^(song|pattern)/track:([0-9]+)$").match(name);
		if (path.hasMatch())
		{
			if (arguments.contains("parent") && arguments.value("parent").toString() != path.captured(1))
			{
				failure = invalidArguments("The track path conflicts with 'parent'.");
				return false;
			}
			container = path.captured(1) == "song" ? static_cast<TrackContainer*>(song) : Engine::patternStore();
			bool valid = false;
			trackIndex = path.captured(2).toInt(&valid);
			if (!valid)
			{
				failure = invalidArguments("The track index is out of range.");
				return false;
			}
		}
		else
		{
			trackIndex = -1;
			for (std::size_t index = 0; index < container->tracks().size(); ++index)
			{
				if (container->tracks()[index]->name() != name)
				{
					continue;
				}
				if (trackIndex >= 0)
				{
					failure
						= CommandResult::failure("ambiguous_track", "Use a track path or index for duplicate names.");
					return false;
				}
				trackIndex = static_cast<int>(index);
			}
		}
	}
	else if (!readNonNegativeInteger(arguments, "track", trackIndex, error))
	{
		failure = invalidArguments(error);
		return false;
	}
	const auto& tracks = container->tracks();
	if (trackIndex < 0 || trackIndex >= static_cast<int>(tracks.size()))
	{
		failure = CommandResult::failure("track_not_found", QStringLiteral("Track %1 does not exist.").arg(trackIndex));
		return false;
	}
	track = tracks[trackIndex];
	return true;
}

bool resolveClip(Song* song, const QJsonObject& arguments, ClipAddress& address, CommandResult& failure)
{
	if (!resolveTrack(song, arguments, address.track, address.trackIndex, failure))
	{
		return false;
	}
	QString error;
	if (!readNonNegativeInteger(arguments, "clip", address.clipIndex, error))
	{
		failure = invalidArguments(error);
		return false;
	}
	const auto& clips = address.track->getClips();
	if (address.clipIndex >= static_cast<int>(clips.size()))
	{
		failure = CommandResult::failure("clip_not_found",
			QStringLiteral("Clip %1 does not exist on track %2.").arg(address.clipIndex).arg(address.trackIndex));
		return false;
	}
	address.clip = clips[address.clipIndex];
	return true;
}

QString trackPath(int trackIndex, const Track* track)
{
	return QStringLiteral("%1/track:%2")
		.arg(track && track->trackContainer() == Engine::patternStore() ? "pattern" : "song")
		.arg(trackIndex);
}

QString clipPath(int trackIndex, int clipIndex, const Track* track)
{
	return QStringLiteral("%1/clip:%2").arg(trackPath(trackIndex, track)).arg(clipIndex);
}

int indexOfTrack(const Track* track)
{
	const auto& tracks = track->trackContainer()->tracks();
	for (std::size_t index = 0; index < tracks.size(); ++index)
	{
		if (tracks[index] == track)
		{
			return static_cast<int>(index);
		}
	}
	return -1;
}

int indexOfClip(const Track* track, const Clip* clip)
{
	const auto& clips = track->getClips();
	for (std::size_t index = 0; index < clips.size(); ++index)
	{
		if (clips[index] == clip)
		{
			return static_cast<int>(index);
		}
	}
	return -1;
}

} // namespace lmms::agent
