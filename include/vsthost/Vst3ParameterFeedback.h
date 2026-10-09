#ifndef LMMS_VSTHOST_VST3_PARAMETER_FEEDBACK_H
#define LMMS_VSTHOST_VST3_PARAMETER_FEEDBACK_H
#include "vsthost/Protocol.h"
#include <bit>
#include <cmath>
#include <vector>

namespace lmms::vsthost {
struct Vst3ParameterFeedback
{
	// 0/1/2 = controller begin/perform/end; 3 = processor output parameter.
	std::uint32_t phase = 0, id = 0;
	double value = 0;
	std::uint32_t offset = 0, frames = 0;
	std::uint64_t sequence = 0;
};
inline bool validVst3Feedback(const Vst3ParameterFeedback& edit) noexcept
{
	return edit.phase <= 3 && std::isfinite(edit.value) && edit.value >= 0 && edit.value <= 1
		&& (edit.phase == 3 ? edit.frames && edit.frames <= 4096 && edit.offset < edit.frames
							: !edit.offset && !edit.frames && !edit.sequence);
}
inline bool encodeVst3Feedback(
	std::span<const Vst3ParameterFeedback> edits, std::uint32_t restart, std::vector<std::uint8_t>& bytes)
{
	if (edits.size() > 512)
	{
		return false;
	}
	for (const auto& edit : edits)
	{
		if (!validVst3Feedback(edit))
		{
			return false;
		}
	}
	bytes.assign(12 + edits.size() * 32, 0);
	put(bytes, 0, 2, 4);
	put(bytes, 4, edits.size(), 4);
	put(bytes, 8, restart, 4);
	std::uint32_t offset = 12;
	for (const auto& edit : edits)
	{
		put(bytes, offset, edit.phase, 4);
		put(bytes, offset + 4, edit.id, 4);
		put(bytes, offset + 8, std::bit_cast<std::uint64_t>(edit.value), 8);
		put(bytes, offset + 16, edit.offset, 4);
		put(bytes, offset + 20, edit.frames, 4);
		put(bytes, offset + 24, edit.sequence, 8);
		offset += 32;
	}
	return true;
}
template <class Consumer>
bool decodeVst3Feedback(std::span<const std::uint8_t> bytes, std::uint32_t& restart, Consumer consume)
{
	if (bytes.size() < 12 || get(bytes, 0, 4) != 2 || get(bytes, 4, 4) > 512
		|| bytes.size() != 12 + get(bytes, 4, 4) * 32)
	{
		return false;
	}
	for (unsigned pass = 0; pass < 2; ++pass)
	{
		for (std::uint32_t offset = 12; offset < bytes.size(); offset += 32)
		{
			const Vst3ParameterFeedback edit{static_cast<std::uint32_t>(get(bytes, offset, 4)),
				static_cast<std::uint32_t>(get(bytes, offset + 4, 4)), std::bit_cast<double>(get(bytes, offset + 8, 8)),
				static_cast<std::uint32_t>(get(bytes, offset + 16, 4)),
				static_cast<std::uint32_t>(get(bytes, offset + 20, 4)), get(bytes, offset + 24, 8)};
			if (!validVst3Feedback(edit) || (pass && !consume(edit)))
			{
				return false;
			}
		}
	}
	restart = static_cast<std::uint32_t>(get(bytes, 8, 4));
	return true;
}
}
#endif
