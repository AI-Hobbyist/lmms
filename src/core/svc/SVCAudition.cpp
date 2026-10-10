#include "SVCAudition.h"

#include <algorithm>
#include <cmath>

namespace lmms::svc {
std::array<float, 2> auditionSample(
	const PlaybackSnapshot& snapshot, double position, AuditionMode mode, double sourceGainDb, double renderedGainDb)
{
	const auto aGain = mode == AuditionMode::Rendered ? 0.0 : std::pow(10.0, sourceGainDb / 20.0);
	const auto bGain = mode == AuditionMode::Source ? 0.0 : std::pow(10.0, renderedGainDb / 20.0);
	return auditionSampleLinear(snapshot, position, aGain, bGain);
}

std::array<float, 2> auditionSampleLinear(
	const PlaybackSnapshot& snapshot, double position, double sourceGain, double renderedGain)
{
	std::array<float, 2> result{};
	if (!snapshot.source || !std::isfinite(position) || position < 0 || position >= snapshot.source->frames())
	{
		return result;
	}
	const auto frame = static_cast<uint64_t>(position);
	const auto next = std::min(frame + 1, snapshot.source->frames() - 1);
	const auto fraction = position - frame;
	bool available;
	const auto b = snapshot.renderedSample(position, available);
	for (unsigned channel = 0; channel < 2; ++channel)
	{
		const auto a
			= snapshot.sourceSample(frame, channel) * (1 - fraction) + snapshot.sourceSample(next, channel) * fraction;
		result[channel] = static_cast<float>(sourceGain * a + renderedGain * b);
	}
	return result;
}
} // namespace lmms::svc
