#ifndef LMMS_NODE_WORKFLOW_MIXER_SELECTION_H
#define LMMS_NODE_WORKFLOW_MIXER_SELECTION_H

#include "AudioEngine.h"
#include "Engine.h"
#include "Song.h"

namespace lmms::gui
{

inline void assignTrackMixerChannel(Track& track, IntModel& model, int channel)
{
	auto* song = Engine::getSong();
	if (!song->isNodeProject())
	{
		model.setValue(channel);
		return;
	}

	const auto guard = Engine::audioEngine()->requestChangesGuard();
	const auto target = static_cast<mix_ch_t>(channel);
	song->setNodeTrackAudioRoute(track, 0, target, 0);
	song->setNodeTrackAudioRoute(track, 1, target, 1);
	model.setValue(channel);
}

} // namespace lmms::gui

#endif
