#ifndef LMMS_VSTHOST_VST3_OUTPUT_EVENTS_H
#define LMMS_VSTHOST_VST3_OUTPUT_EVENTS_H
#include "vsthost/Vst3BlockEvents.h"
#include <algorithm>
#include <vector>

namespace lmms::vsthost {
struct Vst3OutputEvent
{
	std::uint64_t sequence = 0;
	std::uint32_t frames = 0;
	Vst3BlockEvent event;
};
// Control polling may collect several audio blocks. Carry each original block's
// sequence and size so offsets never silently become relative to the latest block.
inline bool encodeVst3OutputEvents(
	std::span<const Vst3OutputEvent> events, std::span<std::uint8_t> storage, std::uint32_t& written) noexcept
{
	written = 0;
	if (events.size() > 512 || storage.size() < 8 + events.size() * 48)
	{
		return false;
	}
	for (const auto& output : events)
	{
		if (!output.frames || output.frames > 4096 || output.event.type < 1 || output.event.type > 3
			|| !validVst3Event(output.event, output.frames))
		{
			return false;
		}
	}
	auto bytes = storage.first(8 + events.size() * 48);
	std::fill(bytes.begin(), bytes.end(), 0);
	put(bytes, 0, 1, 4);
	put(bytes, 4, events.size(), 4);
	std::uint32_t offset = 8;
	for (const auto& output : events)
	{
		put(bytes, offset, output.sequence, 8);
		put(bytes, offset + 8, output.frames, 4);
		const auto& event = output.event;
		put(bytes, offset + 16, event.type, 4);
		put(bytes, offset + 20, event.offset, 4);
		put(bytes, offset + 24, event.bus, 4);
		put(bytes, offset + 28, event.channel, 4);
		put(bytes, offset + 32, event.id, 4);
		put(bytes, offset + 40, std::bit_cast<std::uint64_t>(event.value), 8);
		offset += 48;
	}
	written = static_cast<std::uint32_t>(bytes.size());
	return true;
}
inline bool encodeVst3OutputEvents(std::span<const Vst3OutputEvent> events, std::vector<std::uint8_t>& bytes)
{
	if (events.size() > 512)
	{
		return false;
	}
	bytes.resize(8 + events.size() * 48);
	std::uint32_t written = 0;
	return encodeVst3OutputEvents(events, std::span(bytes), written);
}
template <class Consumer> bool decodeVst3OutputEvents(std::span<const std::uint8_t> bytes, Consumer consume)
{
	if (bytes.size() < 8 || get(bytes, 0, 4) != 1 || get(bytes, 4, 4) > 512
		|| bytes.size() != 8 + get(bytes, 4, 4) * 48)
	{
		return false;
	}
	for (unsigned pass = 0; pass < 2; ++pass)
	{
		for (std::uint32_t offset = 8; offset < bytes.size(); offset += 48)
		{
			Vst3OutputEvent output{get(bytes, offset, 8), static_cast<std::uint32_t>(get(bytes, offset + 8, 4)),
				{static_cast<std::uint32_t>(get(bytes, offset + 16, 4)),
					static_cast<std::uint32_t>(get(bytes, offset + 20, 4)),
					static_cast<std::uint32_t>(get(bytes, offset + 24, 4)),
					static_cast<std::uint32_t>(get(bytes, offset + 28, 4)),
					static_cast<std::uint32_t>(get(bytes, offset + 32, 4)),
					std::bit_cast<double>(get(bytes, offset + 40, 8))}};
			if (!output.frames || output.frames > 4096 || output.event.type < 1 || output.event.type > 3
				|| get(bytes, offset + 12, 4) || get(bytes, offset + 36, 4)
				|| !validVst3Event(output.event, output.frames) || (pass && !consume(output)))
			{
				return false;
			}
		}
	}
	return true;
}
}
#endif
