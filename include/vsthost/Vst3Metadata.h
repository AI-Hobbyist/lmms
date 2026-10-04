#ifndef LMMS_VSTHOST_VST3_METADATA_H
#define LMMS_VSTHOST_VST3_METADATA_H
#include "vsthost/Protocol.h"
#include <algorithm>
#include <bit>
#include <cmath>
#include <string>
#include <unordered_set>
#include <vector>

namespace lmms::vsthost
{
struct Vst3ParameterInfo
{
	std::uint32_t id = 0, flags = 0, steps = 0;
	std::int32_t unit = 0;
	double value = 0, defaultValue = 0;
	std::string title, shortTitle, units;
};
struct Vst3Metadata
{
	std::uint32_t inputs = 0, outputs = 0, sampleSize = 0, latency = 0;
	std::vector<Vst3ParameterInfo> parameters;
};
inline bool validVst3ParameterInfo(const Vst3ParameterInfo& info)
{
	if (info.steps > 0x7fffffff || !std::isfinite(info.value) || info.value < 0 || info.value > 1 ||
		!std::isfinite(info.defaultValue) || info.defaultValue < 0 || info.defaultValue > 1) { return false; }
	for (const auto* string : {&info.title, &info.shortTitle, &info.units})
	{ if (string->size() > 512 || string->find('\0') != std::string::npos) { return false; } }
	return true;
}
inline bool encodeVst3Metadata(const Vst3Metadata& metadata, std::vector<std::uint8_t>& bytes)
{
	if (metadata.inputs > 32 || metadata.outputs > 32 || metadata.sampleSize > 1 || metadata.parameters.size() > 65536) { return false; }
	std::unordered_set<std::uint32_t> ids;
	std::size_t size = 24;
	for (const auto& info : metadata.parameters)
	{
		if (!validVst3ParameterInfo(info) || !ids.insert(info.id).second) { return false; }
		size += 44 + info.title.size() + info.shortTitle.size() + info.units.size();
		if (size > MaxControlBytes) { return false; }
	}
	bytes.assign(size, 0); put(bytes, 0, 1, 4); put(bytes, 4, metadata.inputs, 4); put(bytes, 8, metadata.outputs, 4);
	put(bytes, 12, metadata.sampleSize, 4); put(bytes, 16, metadata.latency, 4); put(bytes, 20, metadata.parameters.size(), 4);
	std::uint32_t offset = 24;
	for (const auto& info : metadata.parameters)
	{
		put(bytes, offset, info.id, 4); put(bytes, offset + 4, info.flags, 4); put(bytes, offset + 8, info.steps, 4);
		put(bytes, offset + 12, std::bit_cast<std::uint32_t>(info.unit), 4);
		put(bytes, offset + 16, std::bit_cast<std::uint64_t>(info.value), 8);
		put(bytes, offset + 24, std::bit_cast<std::uint64_t>(info.defaultValue), 8); offset += 32;
		for (const auto* string : {&info.title, &info.shortTitle, &info.units})
		{
			put(bytes, offset, string->size(), 4); offset += 4;
			std::copy(string->begin(), string->end(), bytes.begin() + offset); offset += static_cast<std::uint32_t>(string->size());
		}
	}
	return true;
}
inline bool decodeVst3Metadata(std::span<const std::uint8_t> bytes, Vst3Metadata& metadata)
{
	metadata = {};
	if (bytes.size() < 24 || bytes.size() > MaxControlBytes || get(bytes, 0, 4) != 1 || get(bytes, 4, 4) > 32 ||
		get(bytes, 8, 4) > 32 || get(bytes, 12, 4) > 1 || get(bytes, 20, 4) > 65536 ||
		get(bytes, 20, 4) > (bytes.size() - 24) / 44) { return false; }
	Vst3Metadata result{static_cast<std::uint32_t>(get(bytes, 4, 4)), static_cast<std::uint32_t>(get(bytes, 8, 4)),
		static_cast<std::uint32_t>(get(bytes, 12, 4)), static_cast<std::uint32_t>(get(bytes, 16, 4)), {}};
	std::size_t offset = 24;
	std::unordered_set<std::uint32_t> ids;
	for (std::uint32_t index = 0; index < get(bytes, 20, 4); ++index)
	{
		if (bytes.size() - offset < 44) { return false; }
		Vst3ParameterInfo info{static_cast<std::uint32_t>(get(bytes, offset, 4)), static_cast<std::uint32_t>(get(bytes, offset + 4, 4)),
			static_cast<std::uint32_t>(get(bytes, offset + 8, 4)), std::bit_cast<std::int32_t>(static_cast<std::uint32_t>(get(bytes, offset + 12, 4))),
			std::bit_cast<double>(get(bytes, offset + 16, 8)), std::bit_cast<double>(get(bytes, offset + 24, 8)), {}, {}, {}};
		offset += 32;
		for (auto* string : {&info.title, &info.shortTitle, &info.units})
		{
			if (bytes.size() - offset < 4) { return false; }
			const auto length = get(bytes, offset, 4); offset += 4;
			if (length > 512 || length > bytes.size() - offset) { return false; }
			string->assign(reinterpret_cast<const char*>(bytes.data() + offset), length); offset += length;
		}
		if (!validVst3ParameterInfo(info) || !ids.insert(info.id).second) { return false; }
		result.parameters.push_back(std::move(info));
	}
	if (offset != bytes.size()) { return false; } metadata = std::move(result); return true;
}
}
#endif
