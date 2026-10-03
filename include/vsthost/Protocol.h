#ifndef LMMS_VSTHOST_PROTOCOL_H
#define LMMS_VSTHOST_PROTOCOL_H

#include <array>
#include <cstdint>
#include <span>

namespace lmms::vsthost
{
enum class Format : std::uint8_t { Vst2 = 2, Vst3 = 3 };
enum class Architecture : std::uint16_t { X86 = 0x014c, X64 = 0x8664 };
enum class SessionState : std::uint8_t { Stopped, Starting, Ready, Processing, Faulted, Stopping };
enum class Error : std::uint32_t
{
	None, InvalidMessage, ProtocolMismatch, StaleSession, UnsupportedArchitecture,
	MissingHelper, LoadFailed, InitializationFailed, Timeout, Disconnected,
	ProcessCrashed, InvalidState, ProcessingFailed
};
enum class MessageType : std::uint16_t
{
	Hello = 1, Create, Close, Process, ProcessDone, Parameter, Midi, GetState,
	SetState, ShowEditor, HideEditor, Scan, ScanResult, Fault
};

// Wire encoding is little endian. Never memcpy a native C++ struct into IPC.
constexpr std::uint32_t ProtocolMagic = 0x48564d4c;
constexpr std::uint16_t ProtocolVersion = 1;
constexpr std::uint32_t MaxControlBytes = 16 * 1024 * 1024;
constexpr std::uint32_t HeaderBytes = 40;

struct Header
{
	MessageType type = MessageType::Hello;
	std::uint64_t session = 0;
	std::uint64_t generation = 0;
	std::uint64_t sequence = 0;
	std::uint32_t payloadBytes = 0;
};

inline void put(std::span<std::uint8_t> bytes, std::uint32_t offset, std::uint64_t value, unsigned width)
{
	for (unsigned i = 0; i < width; ++i) { bytes[offset + i] = static_cast<std::uint8_t>(value >> (8 * i)); }
}
inline std::uint64_t get(std::span<const std::uint8_t> bytes, std::uint32_t offset, unsigned width)
{
	std::uint64_t value = 0;
	for (unsigned i = 0; i < width; ++i) { value |= std::uint64_t{bytes[offset + i]} << (8 * i); }
	return value;
}
inline std::array<std::uint8_t, HeaderBytes> encode(const Header& header)
{
	std::array<std::uint8_t, HeaderBytes> bytes{};
	put(bytes, 0, ProtocolMagic, 4);
	put(bytes, 4, ProtocolVersion, 2);
	put(bytes, 6, static_cast<std::uint16_t>(header.type), 2);
	put(bytes, 8, header.session, 8);
	put(bytes, 16, header.generation, 8);
	put(bytes, 24, header.sequence, 8);
	put(bytes, 32, header.payloadBytes, 4);
	// bytes 36..39 reserved, must remain zero.
	return bytes;
}
inline Error decode(std::span<const std::uint8_t> bytes, Header& header)
{
	if (bytes.size() < HeaderBytes || get(bytes, 0, 4) != ProtocolMagic || get(bytes, 36, 4) != 0)
	{ return Error::InvalidMessage; }
	if (get(bytes, 4, 2) != ProtocolVersion) { return Error::ProtocolMismatch; }
	const auto type = get(bytes, 6, 2);
	const auto size = get(bytes, 32, 4);
	if (type < 1 || type > static_cast<unsigned>(MessageType::Fault) || size > MaxControlBytes)
	{ return Error::InvalidMessage; }
	header = {static_cast<MessageType>(type), get(bytes, 8, 8), get(bytes, 16, 8), get(bytes, 24, 8),
		static_cast<std::uint32_t>(size)};
	return Error::None;
}
inline bool validRegion(std::uint64_t offset, std::uint64_t size, std::uint64_t capacity)
{
	return offset <= capacity && size <= capacity - offset;
}
inline bool matches(const Header& header, std::uint64_t session, std::uint64_t generation, std::uint64_t sequence)
{
	return header.session == session && header.generation == generation && header.sequence == sequence;
}
} // namespace lmms::vsthost
#endif
