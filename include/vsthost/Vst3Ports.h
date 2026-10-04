#ifndef LMMS_VSTHOST_VST3_PORTS_H
#define LMMS_VSTHOST_VST3_PORTS_H
#include "vsthost/Vst3Metadata.h"
#include <span>

namespace lmms::vsthost
{
struct Vst3AudioPorts
{
	static constexpr std::uint32_t Automatic = UINT32_MAX - 1, Disconnected = UINT32_MAX;
	std::uint32_t inputBus = Automatic, inputChannel = 0;
	std::uint32_t outputBus = Automatic, outputChannel = 0;
};
struct Vst3MidiPorts
{
	static constexpr std::uint32_t Automatic = Vst3AudioPorts::Automatic, Disconnected = Vst3AudioPorts::Disconnected;
	std::uint32_t inputBus = Automatic, outputBus = Automatic;
	bool operator==(const Vst3MidiPorts&) const = default;
};
// Default event routing prefers an active main bus, then the first usable
// auxiliary bus. UINT32_MAX means there is no event input/output to connect.
inline std::uint32_t defaultVst3EventBus(const Vst3Metadata& metadata, std::uint32_t direction) noexcept
{
	if (direction > 1) { return UINT32_MAX; }
	std::uint32_t fallback = UINT32_MAX;
	for (const auto& bus : metadata.buses)
	{
		if (bus.media != 1 || bus.direction != direction || !bus.active || !bus.channels) { continue; }
		if (bus.type == 0) { return bus.index; }
		if (fallback == UINT32_MAX) { fallback = bus.index; }
	}
	return fallback;
}
// Packed port: 5-bit bus index plus admitted channel count. UINT32_MAX is
// disconnected. Unavailable explicit selections never fall back to another bus.
inline bool resolveVst3EventPort(const Vst3Metadata& metadata, std::uint32_t direction,
	std::uint32_t selection, std::uint32_t& port) noexcept
{
	port = UINT32_MAX;
	if (direction > 1) { return false; }
	if (selection == Vst3MidiPorts::Disconnected) { return true; }
	const auto index = selection == Vst3MidiPorts::Automatic ? defaultVst3EventBus(metadata, direction) : selection;
	if (index == UINT32_MAX) { return selection == Vst3MidiPorts::Automatic; }
	for (const auto& bus : metadata.buses)
	{
		if (bus.media == 1 && bus.direction == direction && bus.index == index && bus.active && bus.channels &&
			bus.index < 32 && bus.channels <= 16)
		{ port = bus.index | (bus.channels << 5); return true; }
	}
	return false;
}
struct Vst3StereoPort
{
	std::uint32_t offset = 0, channels = 0;
};
inline bool resolveVst3Port(const Vst3Metadata& metadata, std::uint32_t direction, std::uint32_t index,
	std::uint32_t channel, Vst3StereoPort& port) noexcept
{
	port = {};
	if (direction > 1) { return false; }
	if (index == Vst3AudioPorts::Disconnected) { return channel == 0; }
	for (const auto& bus : metadata.buses)
	{
		if (bus.media || bus.direction != direction || !bus.active || !bus.channels || (bus.flags & 2)) { continue; }
		if (index == Vst3AudioPorts::Automatic ? bus.type != 0 : bus.index != index) { continue; }
		if (channel >= bus.channels) { return false; }
		port = {bus.channelOffset + channel, std::min(2u, bus.channels - channel)}; return true;
	}
	return index == Vst3AudioPorts::Automatic && channel == 0; // Instruments may have no audio input.
}
inline std::uint32_t encodeVst3Port(Vst3StereoPort port) noexcept { return port.offset | (port.channels << 6); }
inline Vst3StereoPort decodeVst3Port(std::uint32_t value) noexcept { return {value & 63, (value >> 6) & 3}; }
// Ordinary LMMS stereo mapped to one selected bus; all other native channels
// get valid silence. Mono input is the equal-weight sum, mono output duplicates.
inline void writeVst3Port(std::span<float> frame, Vst3StereoPort port, float left, float right) noexcept
{
	std::fill(frame.begin(), frame.end(), 0.f);
	if (!port.channels || port.offset >= frame.size() || port.channels > frame.size() - port.offset) { return; }
	frame[port.offset] = port.channels == 1 ? 0.5f * left + 0.5f * right : left;
	if (port.channels == 2) { frame[port.offset + 1] = right; }
}
// Overlay a separately connected bus without clearing the selected main input.
inline void overlayVst3Port(std::span<float> frame, Vst3StereoPort port, float left, float right) noexcept
{
	if (!port.channels || port.offset >= frame.size() || port.channels > frame.size() - port.offset) { return; }
	frame[port.offset] = port.channels == 1 ? 0.5f * left + 0.5f * right : left;
	if (port.channels == 2) { frame[port.offset + 1] = right; }
}
inline std::array<float, 2> readVst3Port(std::span<const float> frame, Vst3StereoPort port) noexcept
{
	if (!port.channels || port.offset >= frame.size() || port.channels > frame.size() - port.offset) { return {}; }
	return {frame[port.offset], frame[port.offset + (port.channels == 2 ? 1 : 0)]};
}
}
#endif
