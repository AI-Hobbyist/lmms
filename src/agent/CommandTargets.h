#ifndef LMMS_AGENT_COMMAND_TARGETS_H
#define LMMS_AGENT_COMMAND_TARGETS_H

#include <QJsonObject>
#include <QString>

#include "agent/CommandBus.h"

namespace lmms {
class Clip;
class Song;
class Track;
class TrackContainer;
} // namespace lmms

namespace lmms::agent {

struct ClipAddress
{
	Track* track = nullptr;
	Clip* clip = nullptr;
	int trackIndex = -1;
	int clipIndex = -1;
};

bool resolveParent(Song* song, const QJsonObject& arguments, TrackContainer*& container, CommandResult& failure);
bool resolveTrack(Song* song, const QJsonObject& arguments, Track*& track, int& trackIndex, CommandResult& failure);
bool resolveClip(Song* song, const QJsonObject& arguments, ClipAddress& address, CommandResult& failure);
QString trackPath(int trackIndex, const Track* track);
QString clipPath(int trackIndex, int clipIndex, const Track* track);
int indexOfTrack(const Track* track);
int indexOfClip(const Track* track, const Clip* clip);

} // namespace lmms::agent

#endif // LMMS_AGENT_COMMAND_TARGETS_H
