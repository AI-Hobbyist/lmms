#ifndef LMMS_VSTHOST_VST3_CATALOG_CODEC_H
#define LMMS_VSTHOST_VST3_CATALOG_CODEC_H
#include "vsthost/Protocol.h"
#include <algorithm>
#include <string>
#include <vector>

namespace lmms::vsthost {
// SDK-free catalog wire contract. CIDs retain the exact 16 native TUID bytes
// on both Windows architectures; no GUID text formatting or struct padding.
struct Vst3Class
{
	std::array<std::uint8_t, 16> cid{};
	std::uint32_t flags = 0;
	std::string category, name, vendor, version, sdkVersion, subcategories;
	bool audioClass() const noexcept { return category == "Audio Module Class"; }
};

inline bool encodeVst3Catalog(const std::vector<Vst3Class>& classes, std::vector<std::uint8_t>& bytes)
{
	bytes.clear();
	if (classes.size() > 4096)
	{
		return false;
	}
	std::uint64_t total = 8;
	for (const auto& info : classes)
	{
		total += 20;
		for (const auto* text :
			{&info.category, &info.name, &info.vendor, &info.version, &info.sdkVersion, &info.subcategories})
		{
			if (text->size() > 4096 || text->find('\0') != std::string::npos)
			{
				return false;
			}
			total += 4 + text->size();
		}
	}
	if (total > MaxControlBytes)
	{
		return false;
	}
	bytes.resize(static_cast<std::size_t>(total));
	put(bytes, 0, 1, 4);
	put(bytes, 4, classes.size(), 4);
	std::uint32_t offset = 8;
	for (const auto& info : classes)
	{
		std::copy(info.cid.begin(), info.cid.end(), bytes.begin() + offset);
		offset += 16;
		put(bytes, offset, info.flags, 4);
		offset += 4;
		for (const auto* text :
			{&info.category, &info.name, &info.vendor, &info.version, &info.sdkVersion, &info.subcategories})
		{
			put(bytes, offset, text->size(), 4);
			offset += 4;
			std::copy(text->begin(), text->end(), bytes.begin() + offset);
			offset += static_cast<std::uint32_t>(text->size());
		}
	}
	return true;
}

inline bool decodeVst3Catalog(std::span<const std::uint8_t> bytes, std::vector<Vst3Class>& classes)
{
	classes.clear();
	if (bytes.size() < 8 || bytes.size() > MaxControlBytes || get(bytes, 0, 4) != 1)
	{
		return false;
	}
	const auto count = get(bytes, 4, 4);
	if (count > 4096 || count > (bytes.size() - 8) / 44)
	{
		return false;
	}
	std::vector<Vst3Class> parsed;
	parsed.reserve(static_cast<std::size_t>(count));
	std::uint32_t offset = 8;
	for (std::uint32_t index = 0; index < count; ++index)
	{
		if (!validRegion(offset, 20, bytes.size()))
		{
			return false;
		}
		Vst3Class info;
		std::copy_n(bytes.begin() + offset, 16, info.cid.begin());
		offset += 16;
		info.flags = static_cast<std::uint32_t>(get(bytes, offset, 4));
		offset += 4;
		for (auto* text :
			{&info.category, &info.name, &info.vendor, &info.version, &info.sdkVersion, &info.subcategories})
		{
			if (!validRegion(offset, 4, bytes.size()))
			{
				return false;
			}
			const auto size = get(bytes, offset, 4);
			offset += 4;
			if (size > 4096 || !validRegion(offset, size, bytes.size()))
			{
				return false;
			}
			text->assign(reinterpret_cast<const char*>(bytes.data() + offset), static_cast<std::size_t>(size));
			if (text->find('\0') != std::string::npos)
			{
				return false;
			}
			offset += static_cast<std::uint32_t>(size);
		}
		if (info.category.empty() || info.name.empty()
			|| std::all_of(info.cid.begin(), info.cid.end(), [](auto byte) { return byte == 0; })
			|| std::any_of(parsed.begin(), parsed.end(), [&](const auto& prior) { return prior.cid == info.cid; }))
		{
			return false;
		}
		parsed.push_back(std::move(info));
	}
	if (offset != bytes.size())
	{
		return false;
	}
	classes = std::move(parsed);
	return true;
}
} // namespace lmms::vsthost
#endif
