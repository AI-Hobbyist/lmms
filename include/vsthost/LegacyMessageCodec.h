#ifndef LMMS_VSTHOST_LEGACY_MESSAGE_CODEC_H
#define LMMS_VSTHOST_LEGACY_MESSAGE_CODEC_H

#include "Protocol.h"
#include <algorithm>
#include <string>
#include <vector>
#include <utility>

namespace lmms::vsthost
{
// Transitional VST2 control payload; message IDs and decimal arguments retain
// their existing semantics, but lengths and IDs are explicit little-endian u32.
// This codec never serializes a native message/vector/string object.
struct LegacyCommand
{
	std::uint32_t id = 0;
	std::vector<std::string> arguments;
};
constexpr std::uint32_t MaxLegacyCommands = 1024;
constexpr std::uint32_t MaxLegacyArguments = 16384;

inline Error encodeLegacy(std::span<const LegacyCommand> commands, std::vector<std::uint8_t>& bytes,
	std::uint32_t capacity = MaxControlBytes)
{
	if (commands.size() > MaxLegacyCommands || capacity > MaxControlBytes) { return Error::InvalidMessage; }
	std::uint64_t size = 4;
	for (const auto& command : commands)
	{
		if (command.id > INT32_MAX || command.arguments.size() > MaxLegacyArguments) { return Error::InvalidMessage; }
		size += 8;
		for (const auto& argument : command.arguments)
		{
			if (argument.size() > capacity) { return Error::InvalidMessage; }
			size += 4 + argument.size();
			if (size > capacity) { return Error::InvalidMessage; }
		}
	}
	if (size > capacity) { return Error::InvalidMessage; }
	std::vector<std::uint8_t> encoded(static_cast<std::size_t>(size));
	put(encoded, 0, commands.size(), 4);
	std::uint32_t offset = 4;
	for (const auto& command : commands)
	{
		put(encoded, offset, command.id, 4); put(encoded, offset + 4, command.arguments.size(), 4); offset += 8;
		for (const auto& argument : command.arguments)
		{
			put(encoded, offset, argument.size(), 4); offset += 4;
			std::copy(argument.begin(), argument.end(), encoded.begin() + offset);
			offset += static_cast<std::uint32_t>(argument.size());
		}
	}
	bytes = std::move(encoded); return Error::None;
}

inline Error decodeLegacy(std::span<const std::uint8_t> bytes, std::vector<LegacyCommand>& commands,
	std::uint32_t capacity = MaxControlBytes)
{
	if (bytes.size() < 4 || bytes.size() > capacity || capacity > MaxControlBytes) { return Error::InvalidMessage; }
	const auto count = static_cast<std::uint32_t>(get(bytes, 0, 4));
	if (count > MaxLegacyCommands) { return Error::InvalidMessage; }
	// Validate the entire packet before allocating any strings or commands.
	std::uint32_t offset = 4;
	for (std::uint32_t i = 0; i < count; ++i)
	{
		if (!validRegion(offset, 8, bytes.size()) || get(bytes, offset, 4) > INT32_MAX) { return Error::InvalidMessage; }
		const auto arguments = get(bytes, offset + 4, 4); offset += 8;
		if (arguments > MaxLegacyArguments) { return Error::InvalidMessage; }
		for (std::uint32_t j = 0; j < arguments; ++j)
		{
			if (!validRegion(offset, 4, bytes.size())) { return Error::InvalidMessage; }
			const auto size = get(bytes, offset, 4); offset += 4;
			if (!validRegion(offset, size, bytes.size())) { return Error::InvalidMessage; }
			offset += static_cast<std::uint32_t>(size);
		}
	}
	if (offset != bytes.size()) { return Error::InvalidMessage; }
	std::vector<LegacyCommand> decoded; decoded.reserve(count); offset = 4;
	for (std::uint32_t i = 0; i < count; ++i)
	{
		LegacyCommand command{static_cast<std::uint32_t>(get(bytes, offset, 4)), {}};
		const auto arguments = static_cast<std::uint32_t>(get(bytes, offset + 4, 4)); offset += 8;
		command.arguments.reserve(arguments);
		for (std::uint32_t j = 0; j < arguments; ++j)
		{
			const auto size = static_cast<std::uint32_t>(get(bytes, offset, 4)); offset += 4;
			command.arguments.emplace_back(reinterpret_cast<const char*>(bytes.data() + offset), size); offset += size;
		}
		decoded.push_back(std::move(command));
	}
	commands = std::move(decoded); return Error::None;
}
} // namespace lmms::vsthost
#endif
