#ifndef LMMS_VSTHOST_VST3_METADATA_H
#define LMMS_VSTHOST_VST3_METADATA_H
#include "vsthost/Protocol.h"
#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <string>
#include <unordered_set>
#include <vector>

namespace lmms::vsthost {
struct Vst3ParameterInfo
{
	std::uint32_t id = 0, flags = 0, steps = 0;
	std::int32_t unit = 0;
	double value = 0, defaultValue = 0;
	std::string title, shortTitle, units;
};
// SDK-free bus identity: media 0=audio/1=event, direction 0=input/1=output,
// type 0=main/1=aux. Index is local to its media/direction group. Audio channel
// offsets address the interleaved bridge; event channels are MIDI channels.
struct Vst3BusInfo
{
	std::uint32_t media = 0, direction = 0, index = 0, channels = 0;
	std::uint32_t type = 0, flags = 0, channelOffset = 0;
	bool active = false;
	std::uint64_t arrangement = 0;
	std::string name;
};
struct Vst3Metadata
{
	std::uint32_t inputs = 0, outputs = 0, sampleSize = 0, latency = 0;
	std::vector<Vst3ParameterInfo> parameters;
	bool hasEditor = false;
	std::vector<Vst3BusInfo> buses;
};
inline bool validVst3ParameterInfo(const Vst3ParameterInfo& info)
{
	if (info.steps > 0x7fffffff || !std::isfinite(info.value) || info.value < 0 || info.value > 1
		|| !std::isfinite(info.defaultValue) || info.defaultValue < 0 || info.defaultValue > 1)
	{
		return false;
	}
	for (const auto* string : {&info.title, &info.shortTitle, &info.units})
	{
		if (string->size() > 512 || string->find('\0') != std::string::npos)
		{
			return false;
		}
	}
	return true;
}
inline bool validVst3Buses(const Vst3Metadata& metadata)
{
	if (metadata.buses.size() > 128)
	{
		return false;
	}
	std::array<std::uint32_t, 4> counts{};
	std::array<std::uint32_t, 2> channels{};
	for (const auto& bus : metadata.buses)
	{
		if (bus.media > 1 || bus.direction > 1 || bus.type > 1 || bus.name.size() > 512
			|| bus.name.find('\0') != std::string::npos || (bus.flags & ~(bus.media ? 1u : 3u)))
		{
			return false;
		}
		const auto group = bus.media * 2 + bus.direction;
		if (counts[group] == 32 || bus.index != counts[group]++)
		{
			return false;
		}
		if (bus.media == 0)
		{
			if (bus.channels > 32 || static_cast<std::uint32_t>(std::popcount(bus.arrangement)) != bus.channels
				|| bus.channelOffset != channels[bus.direction]
				|| channels[bus.direction] + bus.channels > MaxAudioChannels)
			{
				return false;
			}
			channels[bus.direction] += bus.channels;
		}
		else if (bus.channels > 16 || bus.arrangement || bus.channelOffset)
		{
			return false;
		}
	}
	return channels[0] == metadata.inputs && channels[1] == metadata.outputs;
}
inline bool encodeVst3Metadata(const Vst3Metadata& metadata, std::vector<std::uint8_t>& bytes)
{
	if (metadata.inputs > MaxAudioChannels || metadata.outputs > MaxAudioChannels || metadata.sampleSize > 1
		|| metadata.parameters.size() > 65536 || !validVst3Buses(metadata))
	{
		return false;
	}
	std::unordered_set<std::uint32_t> ids;
	std::size_t size = 32;
	for (const auto& info : metadata.parameters)
	{
		if (!validVst3ParameterInfo(info) || !ids.insert(info.id).second)
		{
			return false;
		}
		size += 44 + info.title.size() + info.shortTitle.size() + info.units.size();
		if (size > MaxControlBytes)
		{
			return false;
		}
	}
	for (const auto& bus : metadata.buses)
	{
		size += 44 + bus.name.size();
		if (size > MaxControlBytes)
		{
			return false;
		}
	}
	bytes.assign(size, 0);
	put(bytes, 0, 3, 4);
	put(bytes, 4, metadata.inputs, 4);
	put(bytes, 8, metadata.outputs, 4);
	put(bytes, 12, metadata.sampleSize, 4);
	put(bytes, 16, metadata.latency, 4);
	put(bytes, 20, metadata.parameters.size(), 4);
	put(bytes, 24, metadata.hasEditor ? 1 : 0, 4);
	put(bytes, 28, metadata.buses.size(), 4);
	std::uint32_t offset = 32;
	for (const auto& info : metadata.parameters)
	{
		put(bytes, offset, info.id, 4);
		put(bytes, offset + 4, info.flags, 4);
		put(bytes, offset + 8, info.steps, 4);
		put(bytes, offset + 12, std::bit_cast<std::uint32_t>(info.unit), 4);
		put(bytes, offset + 16, std::bit_cast<std::uint64_t>(info.value), 8);
		put(bytes, offset + 24, std::bit_cast<std::uint64_t>(info.defaultValue), 8);
		offset += 32;
		for (const auto* string : {&info.title, &info.shortTitle, &info.units})
		{
			put(bytes, offset, string->size(), 4);
			offset += 4;
			std::copy(string->begin(), string->end(), bytes.begin() + offset);
			offset += static_cast<std::uint32_t>(string->size());
		}
	}
	for (const auto& bus : metadata.buses)
	{
		put(bytes, offset, bus.media, 4);
		put(bytes, offset + 4, bus.direction, 4);
		put(bytes, offset + 8, bus.index, 4);
		put(bytes, offset + 12, bus.channels, 4);
		put(bytes, offset + 16, bus.type, 4);
		put(bytes, offset + 20, bus.flags, 4);
		put(bytes, offset + 24, bus.channelOffset, 4);
		put(bytes, offset + 28, bus.active ? 1 : 0, 4);
		put(bytes, offset + 32, bus.arrangement, 8);
		put(bytes, offset + 40, bus.name.size(), 4);
		offset += 44;
		std::copy(bus.name.begin(), bus.name.end(), bytes.begin() + offset);
		offset += static_cast<std::uint32_t>(bus.name.size());
	}
	return true;
}
inline bool decodeVst3Metadata(std::span<const std::uint8_t> bytes, Vst3Metadata& metadata)
{
	metadata = {};
	if (bytes.size() < 32 || bytes.size() > MaxControlBytes || get(bytes, 0, 4) != 3
		|| get(bytes, 4, 4) > MaxAudioChannels || get(bytes, 8, 4) > MaxAudioChannels || get(bytes, 12, 4) > 1
		|| get(bytes, 20, 4) > 65536 || get(bytes, 24, 4) > 1 || get(bytes, 28, 4) > 128
		|| get(bytes, 20, 4) > (bytes.size() - 32) / 44 || get(bytes, 28, 4) > (bytes.size() - 32) / 44)
	{
		return false;
	}
	Vst3Metadata result{static_cast<std::uint32_t>(get(bytes, 4, 4)), static_cast<std::uint32_t>(get(bytes, 8, 4)),
		static_cast<std::uint32_t>(get(bytes, 12, 4)), static_cast<std::uint32_t>(get(bytes, 16, 4)), {}};
	result.hasEditor = get(bytes, 24, 4) != 0;
	std::size_t offset = 32;
	std::unordered_set<std::uint32_t> ids;
	for (std::uint32_t index = 0; index < get(bytes, 20, 4); ++index)
	{
		if (bytes.size() - offset < 44)
		{
			return false;
		}
		Vst3ParameterInfo info{static_cast<std::uint32_t>(get(bytes, offset, 4)),
			static_cast<std::uint32_t>(get(bytes, offset + 4, 4)),
			static_cast<std::uint32_t>(get(bytes, offset + 8, 4)),
			std::bit_cast<std::int32_t>(static_cast<std::uint32_t>(get(bytes, offset + 12, 4))),
			std::bit_cast<double>(get(bytes, offset + 16, 8)), std::bit_cast<double>(get(bytes, offset + 24, 8)), {},
			{}, {}};
		offset += 32;
		for (auto* string : {&info.title, &info.shortTitle, &info.units})
		{
			if (bytes.size() - offset < 4)
			{
				return false;
			}
			const auto length = static_cast<std::size_t>(get(bytes, offset, 4));
			offset += 4;
			if (length > 512 || length > bytes.size() - offset)
			{
				return false;
			}
			string->assign(reinterpret_cast<const char*>(bytes.data() + offset), length);
			offset += length;
		}
		if (!validVst3ParameterInfo(info) || !ids.insert(info.id).second)
		{
			return false;
		}
		result.parameters.push_back(std::move(info));
	}
	for (std::uint32_t index = 0; index < get(bytes, 28, 4); ++index)
	{
		if (bytes.size() - offset < 44 || get(bytes, offset + 28, 4) > 1)
		{
			return false;
		}
		Vst3BusInfo bus{static_cast<std::uint32_t>(get(bytes, offset, 4)),
			static_cast<std::uint32_t>(get(bytes, offset + 4, 4)),
			static_cast<std::uint32_t>(get(bytes, offset + 8, 4)),
			static_cast<std::uint32_t>(get(bytes, offset + 12, 4)),
			static_cast<std::uint32_t>(get(bytes, offset + 16, 4)),
			static_cast<std::uint32_t>(get(bytes, offset + 20, 4)),
			static_cast<std::uint32_t>(get(bytes, offset + 24, 4)), get(bytes, offset + 28, 4) != 0,
			get(bytes, offset + 32, 8), {}};
		const auto length = static_cast<std::size_t>(get(bytes, offset + 40, 4));
		offset += 44;
		if (length > 512 || length > bytes.size() - offset)
		{
			return false;
		}
		bus.name.assign(reinterpret_cast<const char*>(bytes.data() + offset), length);
		offset += length;
		result.buses.push_back(std::move(bus));
	}
	if (offset != bytes.size() || !validVst3Buses(result))
	{
		return false;
	}
	metadata = std::move(result);
	return true;
}
}
#endif
