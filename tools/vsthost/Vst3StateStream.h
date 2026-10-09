#ifndef LMMS_VSTHOST_VST3_STATE_STREAM_H
#define LMMS_VSTHOST_VST3_STATE_STREAM_H
#include "vsthost/Protocol.h"
#include "pluginterfaces/base/ibstream.h"
#include "pluginterfaces/base/funknownimpl.h"
#include <algorithm>
#include <cstring>
#include <vector>

namespace lmms::vsthost {
// Control-thread stream. A native writer cannot grow an IPC state beyond its
// negotiated limit; failed operations remain visible even if a plug-in ignores
// their return codes. SDK objects are never encoded in the state envelope.
class Vst3StateStream final : public Steinberg::U::Implements<Steinberg::U::Directly<Steinberg::IBStream>>
{
public:
	explicit Vst3StateStream(std::vector<std::uint8_t> bytes = {}, bool writable = true)
		: m_bytes(std::move(bytes))
		, m_writable(writable)
	{
	}
	Steinberg::tresult PLUGIN_API read(void* destination, Steinberg::int32 requested, Steinberg::int32* count) override
	{
		if (count)
		{
			*count = 0;
		}
		if (requested < 0 || (requested && !destination) || m_position > m_bytes.size())
		{
			return fail();
		}
		const auto available = std::min<std::size_t>(requested, m_bytes.size() - m_position);
		if (available)
		{
			std::memcpy(destination, m_bytes.data() + m_position, available);
		}
		m_position += available;
		if (count)
		{
			*count = static_cast<Steinberg::int32>(available);
		}
		return available == static_cast<std::size_t>(requested) ? Steinberg::kResultOk : Steinberg::kResultFalse;
	}
	Steinberg::tresult PLUGIN_API write(void* source, Steinberg::int32 requested, Steinberg::int32* count) override
	{
		if (count)
		{
			*count = 0;
		}
		if (!m_writable || requested < 0 || (requested && !source)
			|| !validRegion(m_position, requested, MaxControlBytes))
		{
			return fail();
		}
		try
		{
			const auto end = m_position + requested;
			if (end > m_bytes.size())
			{
				m_bytes.resize(end);
			}
			if (requested)
			{
				std::memcpy(m_bytes.data() + m_position, source, requested);
			}
			m_position = end;
			if (count)
			{
				*count = requested;
			}
			return Steinberg::kResultOk;
		}
		catch (...)
		{
			return fail();
		}
	}
	Steinberg::tresult PLUGIN_API seek(
		Steinberg::int64 offset, Steinberg::int32 mode, Steinberg::int64* result) override
	{
		const auto base = mode == kIBSeekSet ? 0
			: mode == kIBSeekCur			 ? static_cast<Steinberg::int64>(m_position)
			: mode == kIBSeekEnd			 ? static_cast<Steinberg::int64>(m_bytes.size())
											 : -1;
		const auto limit = m_writable ? MaxControlBytes : m_bytes.size();
		if (base < 0 || offset < -base || offset > static_cast<Steinberg::int64>(limit) - base)
		{
			return fail();
		}
		m_position = static_cast<std::size_t>(base + offset);
		if (result)
		{
			*result = static_cast<Steinberg::int64>(m_position);
		}
		return Steinberg::kResultOk;
	}
	Steinberg::tresult PLUGIN_API tell(Steinberg::int64* position) override
	{
		if (!position)
		{
			return fail();
		}
		*position = static_cast<Steinberg::int64>(m_position);
		return Steinberg::kResultOk;
	}
	const std::vector<std::uint8_t>& bytes() const noexcept { return m_bytes; }
	bool failed() const noexcept { return m_failed; }

private:
	Steinberg::tresult fail() noexcept
	{
		m_failed = true;
		return Steinberg::kInvalidArgument;
	}
	std::vector<std::uint8_t> m_bytes;
	std::size_t m_position = 0;
	bool m_writable, m_failed = false;
};
} // namespace lmms::vsthost
#endif
