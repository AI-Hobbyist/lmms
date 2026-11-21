#ifndef LMMS_NODE_AUDIO_SILENCE_THRESHOLD_H
#define LMMS_NODE_AUDIO_SILENCE_THRESHOLD_H

#include <atomic>
#include <cmath>

namespace lmms
{

inline constexpr double DefaultNodeAudioSilenceDb = -70.0;
inline std::atomic<float> nodeAudioSilenceAmplitude{0.000316227766f};

inline void setNodeAudioSilenceDb(double decibels)
{
	nodeAudioSilenceAmplitude.store(static_cast<float>(std::pow(10.0, decibels / 20.0)),
		std::memory_order_relaxed);
}

}

#endif
