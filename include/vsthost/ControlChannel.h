#ifndef LMMS_VSTHOST_CONTROL_CHANNEL_H
#define LMMS_VSTHOST_CONTROL_CHANNEL_H
#include "vsthost/Protocol.h"
#include <algorithm>
#include <cstring>
#include <vector>
#include <windows.h>

namespace lmms::vsthost {
// Two single-writer mailboxes: DAW request and helper reply. Startup negotiates
// capacity; publication is atomic and never exposes a partially written frame.
// Control work may allocate; no method in this class belongs on the audio thread.
class ControlChannel
{
public:
	enum class Side
	{
		Host,
		Helper
	};
	static constexpr std::uint32_t MetadataBytes = 64;
	static constexpr std::uint32_t DefaultCapacity = MaxControlBytes;
	static constexpr std::uint32_t storageBytes(std::uint32_t capacity = DefaultCapacity)
	{
		return capacity && capacity <= MaxControlBytes && capacity % 4 == 0 ? 2 * (MetadataBytes + capacity) : 0;
	}
	struct Frame
	{
		Header header;
		std::vector<std::uint8_t> payload;
	};
	ControlChannel(std::span<std::uint8_t> memory, Side side, std::uint32_t capacity = DefaultCapacity) noexcept
		: m_memory(memory)
		, m_capacity(capacity)
		, m_write(side == Side::Host ? 0 : 1)
		, m_read(1 - m_write)
	{
		if (!storageBytes(capacity) || memory.size() != storageBytes(capacity)
			|| reinterpret_cast<std::uintptr_t>(memory.data()) % 4 != 0)
		{
			m_memory = {};
		}
	}
	bool initialize() noexcept
	{
		if (!valid())
		{
			return false;
		}
		// Don't touch the entire maximum-size blob arena at startup.
		for (unsigned i = 0; i < 2; ++i)
		{
			std::fill_n(cell(i).begin(), MetadataBytes, 0);
		}
		return true;
	}
	bool valid() const noexcept { return !m_memory.empty(); }
	Error send(Header header, std::span<const std::uint8_t> payload) noexcept
	{
		if (!valid() || payload.size() > m_capacity || payload.size() != header.payloadBytes)
		{
			return Error::InvalidMessage;
		}
		const auto encoded = encode(header);
		Header checked{};
		if (const auto error = decode(encoded, checked); error != Error::None)
		{
			return error;
		}
		if (InterlockedCompareExchange(owner(m_write), Writing, Free) != Free)
		{
			return Error::InvalidState;
		}
		auto bytes = cell(m_write);
		put(bytes, 4, HeaderBytes + payload.size(), 4);
		std::copy(encoded.begin(), encoded.end(), bytes.begin() + 8);
		if (!payload.empty())
		{
			std::memcpy(bytes.data() + MetadataBytes, payload.data(), payload.size());
		}
		InterlockedExchange(owner(m_write), Ready);
		return Error::None;
	}
	// InvalidState means no frame is currently available, not a blocking read.
	Error receive(Frame& frame)
	{
		if (!valid())
		{
			return Error::InvalidMessage;
		}
		if (InterlockedCompareExchange(owner(m_read), Reading, Ready) != Ready)
		{
			return Error::InvalidState;
		}
		auto bytes = cell(m_read);
		Header header{};
		auto error = decode(bytes.subspan(8, HeaderBytes), header);
		if (error == Error::None
			&& (header.payloadBytes > m_capacity || get(bytes, 4, 4) != HeaderBytes + header.payloadBytes))
		{
			error = Error::InvalidMessage;
		}
		if (error == Error::None)
		{
			try
			{
				frame.payload.assign(
					bytes.begin() + MetadataBytes, bytes.begin() + MetadataBytes + header.payloadBytes);
				frame.header = header;
			}
			catch (...)
			{
				InterlockedExchange(owner(m_read), Free);
				throw;
			}
		}
		InterlockedExchange(owner(m_read), Free);
		return error;
	}
	template <class IsConnected> Error wait(Frame& frame, DWORD timeoutMs, IsConnected connected)
	{
		const auto deadline = GetTickCount64() + timeoutMs;
		while (true)
		{
			const auto error = receive(frame);
			if (error != Error::InvalidState)
			{
				return error;
			}
			if (!connected())
			{
				return Error::Disconnected;
			}
			if (GetTickCount64() >= deadline)
			{
				return Error::Timeout;
			}
			Sleep(1);
		}
	}

private:
	enum : LONG
	{
		Free,
		Writing,
		Ready,
		Reading
	};
	std::span<std::uint8_t> cell(unsigned i) noexcept
	{
		return m_memory.subspan(i * (MetadataBytes + m_capacity), MetadataBytes + m_capacity);
	}
	volatile LONG* owner(unsigned i) noexcept { return reinterpret_cast<volatile LONG*>(cell(i).data()); }
	std::span<std::uint8_t> m_memory;
	std::uint32_t m_capacity;
	unsigned m_write, m_read;
};
} // namespace lmms::vsthost
#endif
