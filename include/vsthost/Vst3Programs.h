#ifndef LMMS_VST3_PROGRAMS_H
#define LMMS_VST3_PROGRAMS_H

#include "vsthost/Protocol.h"

#include <algorithm>
#include <string>
#include <vector>

namespace lmms::vsthost
{
constexpr std::uint32_t Vst3ProgramListCommand = 3;
constexpr std::size_t MaxVst3Programs = 4096;

struct Vst3Program
{
	std::uint32_t listId;
	std::uint32_t index;
	std::string name;
	std::uint32_t parameterId = UINT32_MAX;
	std::uint32_t steps = 0;

	bool selectable() const { return parameterId != UINT32_MAX && steps > 0 && index <= steps; }
};

inline std::vector<std::uint8_t> encodeVst3Programs(std::span<const Vst3Program> programs)
{
	std::vector<std::uint8_t> bytes(4);
	put(bytes, 0, programs.size(), 4);
	for (const auto& program : programs)
	{
		const auto offset = static_cast<std::uint32_t>(bytes.size());
		bytes.resize(offset + 20 + program.name.size());
		put(bytes, offset, program.listId, 4);
		put(bytes, offset + 4, program.index, 4);
		put(bytes, offset + 8, program.name.size(), 4);
		put(bytes, offset + 12, program.parameterId, 4);
		put(bytes, offset + 16, program.steps, 4);
		std::copy(program.name.begin(), program.name.end(), bytes.begin() + offset + 20);
	}
	return bytes;
}

inline bool decodeVst3Programs(std::span<const std::uint8_t> bytes, std::vector<Vst3Program>& programs)
{
	if (bytes.size() < 4 || get(bytes, 0, 4) > MaxVst3Programs)
	{
		return false;
	}
	const auto count = get(bytes, 0, 4);
	std::vector<Vst3Program> decoded;
	decoded.reserve(static_cast<std::size_t>(count));
	std::uint32_t offset = 4;
	for (std::uint64_t index = 0; index < count; ++index)
	{
		if (bytes.size() - offset < 20)
		{
			return false;
		}
		const auto length = static_cast<std::uint32_t>(get(bytes, offset + 8, 4));
		if (length > 512 || length > bytes.size() - offset - 20)
		{
			return false;
		}
		decoded.push_back({static_cast<std::uint32_t>(get(bytes, offset, 4)),
			static_cast<std::uint32_t>(get(bytes, offset + 4, 4)),
			std::string(reinterpret_cast<const char*>(bytes.data() + offset + 20), length),
			static_cast<std::uint32_t>(get(bytes, offset + 12, 4)),
			static_cast<std::uint32_t>(get(bytes, offset + 16, 4))});
		offset += 20 + length;
	}
	if (offset != bytes.size())
	{
		return false;
	}
	programs = std::move(decoded);
	return true;
}
}

#endif // LMMS_VST3_PROGRAMS_H
