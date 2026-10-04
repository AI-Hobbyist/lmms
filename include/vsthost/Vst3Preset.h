#ifndef LMMS_VSTHOST_VST3_PRESET_H
#define LMMS_VSTHOST_VST3_PRESET_H

#include "Protocol.h"
#include <algorithm>
#include <array>
#include <cstring>
#include <span>
#include <vector>

namespace lmms::vsthost
{
// Validate only the SDK-free transport envelope. Component/controller bytes
// remain opaque and are interpreted exclusively by the supervised helper.
inline bool validVst3State(std::span<const std::uint8_t> state)
{
	if (state.size() < 16 || state.size() > MaxControlBytes || get(state, 0, 4) != 1 || get(state, 12, 4) > 1)
	{ return false; }
	const auto component = get(state, 4, 4), controller = get(state, 8, 4);
	return component + controller == state.size() - 16 && (get(state, 12, 4) || !controller);
}

// Steinberg PresetFile v1 container. The DAW copies opaque native state streams;
// only the supervised helper interprets component/controller state.
// Windows FUID text uses GUID field order, not the raw catalog CID byte order.
inline std::array<std::uint8_t, 32> vst3PresetClassText(const std::array<std::uint8_t, 16>& cid)
{
	constexpr char hex[] = "0123456789ABCDEF";
	constexpr unsigned order[] = {3, 2, 1, 0, 5, 4, 7, 6, 8, 9, 10, 11, 12, 13, 14, 15};
	std::array<std::uint8_t, 32> text{};
	for (unsigned i = 0; i < 16; ++i)
	{
		text[2 * i] = hex[cid[order[i]] >> 4];
		text[2 * i + 1] = hex[cid[order[i]] & 15];
	}
	return text;
}

inline bool encodeVst3Preset(const std::array<std::uint8_t, 16>& cid,
	std::span<const std::uint8_t> state, std::vector<std::uint8_t>& output)
{
	if (state.size() < 16 || state.size() > MaxControlBytes || get(state, 0, 4) != 1 || get(state, 12, 4) > 1)
	{ return false; }
	const auto component = get(state, 4, 4), controller = get(state, 8, 4);
	const bool hasController = get(state, 12, 4) != 0;
	if (component + controller != state.size() - 16 || (!hasController && controller) ||
		std::all_of(cid.begin(), cid.end(), [](auto byte) { return byte == 0; })) { return false; }
	const auto list = 48 + component + controller;
	std::vector<std::uint8_t> bytes(list + 8 + (hasController ? 2 : 1) * 20);
	std::memcpy(bytes.data(), "VST3", 4); put(bytes, 4, 1, 4);
	const auto text = vst3PresetClassText(cid); std::copy(text.begin(), text.end(), bytes.begin() + 8);
	put(bytes, 40, list, 8);
	std::copy(state.begin() + 16, state.end(), bytes.begin() + 48);
	std::memcpy(bytes.data() + list, "List", 4); put(bytes, list + 4, hasController ? 2 : 1, 4);
	std::memcpy(bytes.data() + list + 8, "Comp", 4); put(bytes, list + 12, 48, 8); put(bytes, list + 20, component, 8);
	if (hasController)
	{
		std::memcpy(bytes.data() + list + 28, "Cont", 4);
		put(bytes, list + 32, 48 + component, 8); put(bytes, list + 40, controller, 8);
	}
	output = std::move(bytes); return true;
}

inline bool decodeVst3Preset(const std::array<std::uint8_t, 16>& cid,
	std::span<const std::uint8_t> bytes, std::vector<std::uint8_t>& state)
{
	// Permit bounded metadata/unknown chunks from other hosts, but never ranges
	// intersecting the header, chunk list or each other. Validate before allocating.
	if (bytes.size() < 76 || bytes.size() > MaxControlBytes + 65536 ||
		std::memcmp(bytes.data(), "VST3", 4) || get(bytes, 4, 4) != 1) { return false; }
	if (std::all_of(cid.begin(), cid.end(), [](auto byte) { return byte == 0; })) { return false; }
	const auto expected = vst3PresetClassText(cid);
	for (unsigned i = 0; i < 32; ++i)
	{
		auto character = bytes[8 + i];
		if (character >= 'a' && character <= 'f') { character -= 'a' - 'A'; }
		if (character != expected[i]) { return false; }
	}
	const auto list = get(bytes, 40, 8);
	if (list < 48 || list > bytes.size() - 8 || std::memcmp(bytes.data() + list, "List", 4)) { return false; }
	const auto count = get(bytes, list + 4, 4);
	if (!count || count > 128 || count > (bytes.size() - list - 8) / 20) { return false; }
	struct Range { std::uint64_t offset, size; };
	std::array<Range, 128> ranges{};
	Range component{}, controller{}; bool haveComponent = false, haveController = false;
	for (std::size_t i = 0; i < count; ++i)
	{
		const auto entry = list + 8 + i * 20;
		const Range range{get(bytes, entry + 4, 8), get(bytes, entry + 12, 8)};
		if (range.offset < 48 || range.offset > list || range.size > list - range.offset) { return false; }
		for (std::size_t j = 0; j < i; ++j)
		{
			if (!std::memcmp(bytes.data() + entry, bytes.data() + list + 8 + j * 20, 4)) { return false; }
			if (range.size && ranges[j].size && range.offset < ranges[j].offset + ranges[j].size &&
				ranges[j].offset < range.offset + range.size) { return false; }
		}
		ranges[i] = range;
		if (!std::memcmp(bytes.data() + entry, "Comp", 4)) { component = range; haveComponent = true; }
		if (!std::memcmp(bytes.data() + entry, "Cont", 4)) { controller = range; haveController = true; }
	}
	if (!haveComponent || component.size > MaxControlBytes - 16 ||
		controller.size > MaxControlBytes - 16 - component.size) { return false; }
	std::vector<std::uint8_t> decoded(16 + component.size + controller.size);
	put(decoded, 0, 1, 4); put(decoded, 4, component.size, 4); put(decoded, 8, controller.size, 4);
	put(decoded, 12, haveController ? 1 : 0, 4);
	std::copy_n(bytes.begin() + component.offset, component.size, decoded.begin() + 16);
	std::copy_n(bytes.begin() + controller.offset, controller.size, decoded.begin() + 16 + component.size);
	state = std::move(decoded); return true;
}
} // namespace lmms::vsthost
#endif
