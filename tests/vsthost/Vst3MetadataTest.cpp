#include "vsthost/Vst3Ports.h"
#include <iostream>

using namespace lmms::vsthost;
#define CHECK(value) do { if (!(value)) { std::cerr << "FAIL " << __LINE__ << ": " << #value << '\n'; return 1; } } while (false)
int main()
{
	Vst3Metadata metadata{3, 4, 1, 17, {}, true, {
		{0, 0, 0, 2, 0, 1, 0, true, 3, "Main"},
		{0, 0, 1, 1, 1, 0, 2, false, 1, "Sidechain"},
		{0, 1, 0, 2, 0, 1, 0, true, 3, "Main"},
		{0, 1, 1, 2, 1, 0, 2, true, 3, "Extra output"},
		{1, 0, 0, 16, 0, 1, 0, true, 0, "MIDI"},
		{1, 0, 1, 8, 1, 0, 0, false, 0, "MIDI port 2"},
		{1, 1, 0, 16, 0, 1, 0, true, 0, "MIDI output"}}};
	CHECK(defaultVst3EventBus(metadata, 0) == 0 && defaultVst3EventBus(metadata, 1) == 0);
	CHECK(defaultVst3EventBus(metadata, 2) == UINT32_MAX);
	auto eventLayout = metadata;
	eventLayout.buses[4].type = 1; eventLayout.buses[5].type = 0; eventLayout.buses[5].active = true;
	CHECK(defaultVst3EventBus(eventLayout, 0) == 1); // Main bus is not necessarily index zero.
	eventLayout.buses[5].active = false;
	CHECK(defaultVst3EventBus(eventLayout, 0) == 0); // Auxiliary-only plugins remain usable.
	eventLayout.buses[4].channels = 0;
	CHECK(defaultVst3EventBus(eventLayout, 0) == UINT32_MAX);
	Vst3StereoPort port;
	CHECK(resolveVst3Port(metadata, 0, Vst3AudioPorts::Automatic, 0, port) && port.offset == 0 && port.channels == 2);
	CHECK(resolveVst3Port(metadata, 1, 1, 1, port) && port.offset == 3 && port.channels == 1);
	std::array<float, 4> frame{}; writeVst3Port(frame, port, 1, -0.5f);
	CHECK(frame[0] == 0 && frame[2] == 0 && frame[3] == 0.25f);
	CHECK(readVst3Port(frame, port)[0] == 0.25f && readVst3Port(frame, port)[1] == 0.25f);
	CHECK(resolveVst3Port(metadata, 1, 1, 0, port) && port.offset == 2 && port.channels == 2);
	writeVst3Port(frame, port, 1, -0.5f); CHECK(frame[0] == 0 && frame[1] == 0 && frame[2] == 1 && frame[3] == -0.5f);
	CHECK(!resolveVst3Port(metadata, 0, 1, 0, port)); // Inactive sidechain is unavailable.
	CHECK(!resolveVst3Port(metadata, 1, 1, 2, port)); // Cannot spill into the next bus.
	CHECK(!resolveVst3Port(metadata, 1, 99, 0, port));
	CHECK(resolveVst3Port(metadata, 1, Vst3AudioPorts::Disconnected, 0, port) && port.channels == 0);
	metadata.buses[0].flags |= 2; CHECK(!resolveVst3Port(metadata, 0, 0, 0, port)); metadata.buses[0].flags &= ~2u;
	std::vector<std::uint8_t> bytes; CHECK(encodeVst3Metadata(metadata, bytes));
	CHECK(get(bytes, 0, 4) == 3);
	Vst3Metadata decoded; CHECK(decodeVst3Metadata(bytes, decoded));
	CHECK(decoded.inputs == 3 && decoded.outputs == 4 && decoded.sampleSize == 1 && decoded.latency == 17 && decoded.hasEditor);
	CHECK(decoded.buses.size() == metadata.buses.size());
	std::vector<std::size_t> offsets;
	std::size_t offset = 32;
	for (std::size_t index = 0; index < metadata.buses.size(); ++index)
	{
		const auto& original = metadata.buses[index]; const auto& bus = decoded.buses[index];
		CHECK(bus.media == original.media && bus.direction == original.direction && bus.index == original.index &&
			bus.channels == original.channels && bus.type == original.type && bus.flags == original.flags &&
			bus.channelOffset == original.channelOffset && bus.active == original.active &&
			bus.arrangement == original.arrangement && bus.name == original.name);
		offsets.push_back(offset); offset += 44 + original.name.size();
	}
	for (std::size_t length = 0; length < bytes.size(); ++length)
	{
		decoded = metadata; CHECK(!decodeVst3Metadata(std::span(bytes).first(length), decoded));
		CHECK(decoded.buses.empty() && decoded.parameters.empty() && decoded.inputs == 0);
	}
	for (unsigned malformed = 0; malformed < 15; ++malformed)
	{
		auto corrupt = bytes;
		switch (malformed)
		{
		case 0: put(corrupt, 28, 129, 4); break;
		case 1: put(corrupt, 32, 2, 4); break;
		case 2: put(corrupt, 36, 2, 4); break;
		case 3: put(corrupt, 40, 1, 4); break;
		case 4: put(corrupt, offsets[1] + 8, 0, 4); break;
		case 5: put(corrupt, 44, 33, 4); break;
		case 6: put(corrupt, 48, 2, 4); break;
		case 7: put(corrupt, 52, 4, 4); break;
		case 8: put(corrupt, 56, 1, 4); break;
		case 9: put(corrupt, 60, 2, 4); break;
		case 10: put(corrupt, 64, 1, 8); break;
		case 11: put(corrupt, 72, 513, 4); break;
		case 12: put(corrupt, offsets[4] + 12, 17, 4); break;
		case 13: put(corrupt, offsets[4] + 32, 1, 8); break;
		case 14: put(corrupt, 4, 4, 4); break;
		}
		decoded = metadata; CHECK(!decodeVst3Metadata(corrupt, decoded)); CHECK(decoded.buses.empty());
	}
	auto trailing = bytes; trailing.push_back(0); CHECK(!decodeVst3Metadata(trailing, decoded));
	metadata.buses[0].flags |= 2; CHECK(encodeVst3Metadata(metadata, bytes));
	CHECK(decodeVst3Metadata(bytes, decoded) && (decoded.buses[0].flags & 2)); // Retain control-voltage routing restriction.
	metadata.buses[0].name = "\xe9\x9f\xb3\xe9\xa2\x91";
	CHECK(encodeVst3Metadata(metadata, bytes) && decodeVst3Metadata(bytes, decoded));
	CHECK(decoded.buses[0].name == metadata.buses[0].name);
	metadata.buses[0].name = std::string("bad\0name", 8); CHECK(!encodeVst3Metadata(metadata, bytes));
	metadata = {}; CHECK(encodeVst3Metadata(metadata, bytes) && decodeVst3Metadata(bytes, decoded));
	std::cout << "PASS named audio/event bus metadata, multi-output/sidechain offsets, flags, activation and hostile envelopes\n";
	return 0;
}
