#pragma once

#include <array>
#include <atomic>

#include "SVCPlayback.h"

namespace lmms::svc {
enum class AuditionMode
{
	Source,
	Rendered,
	Overlay
};

std::array<float, 2> auditionSample(
	const PlaybackSnapshot& snapshot, double frame, AuditionMode mode, double sourceGainDb, double renderedGainDb);
std::array<float, 2> auditionSampleLinear(
	const PlaybackSnapshot& snapshot, double frame, double sourceGain, double renderedGain);

struct AuditionState
{
	std::shared_ptr<PlaybackState> playback;
	std::atomic<bool> playing{false};
	std::atomic<bool> alive{true};
	std::atomic<bool> overload{false};
	std::atomic<bool> failed{false};
	std::atomic<AuditionMode> mode{AuditionMode::Source};
	std::atomic<double> sourceGainDb{0};
	std::atomic<double> renderedGainDb{0};
	std::atomic<uint64_t> position{0};
	std::atomic<uint64_t> seekPosition{0};
	std::atomic<uint64_t> seekSerial{0};
	std::atomic<uint64_t> loopStart{0};
	std::atomic<uint64_t> loopEnd{0};
	std::atomic<bool> loop{false};
};
} // namespace lmms::svc
