#ifndef LMMS_VSTHOST_VST3_BLOCK_EVENTS_H
#define LMMS_VSTHOST_VST3_BLOCK_EVENTS_H
#include "vsthost/Protocol.h"
#include <bit>
#include <cmath>

namespace lmms::vsthost
{
struct Vst3Transport
{
	std::uint32_t flags = 0, numerator = 4, denominator = 4;
	double tempo = 120;
	std::int64_t samples = 0;
	double music = 0, bar = 0, cycleStart = 0, cycleEnd = 0;
	std::int64_t continuous = 0;
};
inline bool validVst3Transport(const Vst3Transport& value) noexcept
{
	return value.flags <= 7 && value.numerator && value.numerator <= 256 && value.denominator && value.denominator <= 256 &&
		!(value.denominator & (value.denominator - 1)) && std::isfinite(value.tempo) && value.tempo > 0 && value.tempo <= 1000 &&
		std::isfinite(value.music) && std::isfinite(value.bar) && std::isfinite(value.cycleStart) && std::isfinite(value.cycleEnd) &&
		(!(value.flags & 4) || value.cycleEnd > value.cycleStart);
}
inline bool decodeVst3Transport(std::span<const std::uint8_t> bytes, Vst3Transport& value) noexcept
{
	if (bytes.size() < 80 || get(bytes, 0, 4) != 2 || get(bytes, 20, 4)) { return false; }
	Vst3Transport parsed{static_cast<std::uint32_t>(get(bytes, 8, 4)), static_cast<std::uint32_t>(get(bytes, 12, 4)),
		static_cast<std::uint32_t>(get(bytes, 16, 4)), std::bit_cast<double>(get(bytes, 24, 8)), std::bit_cast<std::int64_t>(get(bytes, 32, 8)),
		std::bit_cast<double>(get(bytes, 40, 8)), std::bit_cast<double>(get(bytes, 48, 8)), std::bit_cast<double>(get(bytes, 56, 8)),
		std::bit_cast<double>(get(bytes, 64, 8)), std::bit_cast<std::int64_t>(get(bytes, 72, 8))};
	if (!validVst3Transport(parsed)) { return false; } value = parsed; return true;
}
struct Vst3BlockEvent
{
	std::uint32_t type = 0, offset = 0, bus = 0, channel = 0, id = 0;
	double value = 0;
};
// Types: parameter=0, note-on=1, note-off=2, poly-pressure=3, controller=4.
// Controller IDs 0..127 are CC, 128 channel pressure, 129 pitch bend;
// the sender supplies normalized 7/14-bit MIDI values.
// Explicit bytes
// retain unsigned ParamIDs and sample offsets across the two helper ABIs.
inline bool validVst3Event(const Vst3BlockEvent& event, std::uint32_t frames) noexcept
{
	return event.type <= 4 && event.offset < frames && std::isfinite(event.value) && event.value >= 0 && event.value <= 1 &&
		(event.type == 0 ? event.bus == 0 && event.channel == 0 :
			event.bus < 32 && event.channel < 16 && event.id < (event.type == 4 ? 130u : 128u));
}
inline std::span<const std::uint8_t> encodeVst3Events(std::span<const Vst3BlockEvent> events,
	std::span<std::uint8_t> bytes, std::uint32_t frames, const Vst3Transport* transport = nullptr) noexcept
{
	const auto header = transport ? 80u : 8u;
	const auto size = header + events.size() * 32;
	if (events.size() > (16384 - header) / 32 || size > bytes.size() || (transport && !validVst3Transport(*transport))) { return {}; }
	for (const auto& event : events) { if (!validVst3Event(event, frames)) { return {}; } }
	put(bytes, 0, transport ? 2 : 1, 4); put(bytes, 4, events.size(), 4);
	if (transport)
	{
		put(bytes, 8, transport->flags, 4); put(bytes, 12, transport->numerator, 4); put(bytes, 16, transport->denominator, 4); put(bytes, 20, 0, 4);
		put(bytes, 24, std::bit_cast<std::uint64_t>(transport->tempo), 8); put(bytes, 32, std::bit_cast<std::uint64_t>(transport->samples), 8);
		put(bytes, 40, std::bit_cast<std::uint64_t>(transport->music), 8); put(bytes, 48, std::bit_cast<std::uint64_t>(transport->bar), 8);
		put(bytes, 56, std::bit_cast<std::uint64_t>(transport->cycleStart), 8); put(bytes, 64, std::bit_cast<std::uint64_t>(transport->cycleEnd), 8);
		put(bytes, 72, std::bit_cast<std::uint64_t>(transport->continuous), 8);
	}
	std::uint32_t offset = header;
	for (const auto& event : events)
	{
		put(bytes, offset, event.type, 4); put(bytes, offset + 4, event.offset, 4);
		put(bytes, offset + 8, event.bus, 4); put(bytes, offset + 12, event.channel, 4);
		put(bytes, offset + 16, event.id, 4); put(bytes, offset + 20, 0, 4);
		put(bytes, offset + 24, std::bit_cast<std::uint64_t>(event.value), 8); offset += 32;
	}
	return bytes.first(size);
}
template<class Consumer> bool decodeVst3Events(std::span<const std::uint8_t> bytes, std::uint32_t frames, Consumer consume)
{
	if (bytes.empty()) { return true; }
	if (bytes.size() < 8) { return false; }
	const auto version = get(bytes, 0, 4);
	const auto header = version == 2 ? 80u : 8u;
	Vst3Transport transport;
	if ((version != 1 && version != 2) || (version == 2 && !decodeVst3Transport(bytes, transport)) ||
		get(bytes, 4, 4) > (16384 - header) / 32 || bytes.size() != header + get(bytes, 4, 4) * 32) { return false; }
	// Validate the entire packet before mutating native queues.
	for (unsigned pass = 0; pass < 2; ++pass)
	{
		for (std::uint32_t offset = header; offset < bytes.size(); offset += 32)
		{
			const Vst3BlockEvent event{static_cast<std::uint32_t>(get(bytes, offset, 4)), static_cast<std::uint32_t>(get(bytes, offset + 4, 4)),
				static_cast<std::uint32_t>(get(bytes, offset + 8, 4)), static_cast<std::uint32_t>(get(bytes, offset + 12, 4)),
				static_cast<std::uint32_t>(get(bytes, offset + 16, 4)), std::bit_cast<double>(get(bytes, offset + 24, 8))};
			if (get(bytes, offset + 20, 4) || !validVst3Event(event, frames) || (pass && !consume(event))) { return false; }
		}
	}
	return true;
}
}
#endif
