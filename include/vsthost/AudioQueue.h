#ifndef LMMS_VSTHOST_AUDIO_QUEUE_H
#define LMMS_VSTHOST_AUDIO_QUEUE_H

#include "vsthost/Protocol.h"
#include <algorithm>
#include <array>
#include <bit>
#include <cstring>
#include <windows.h>

namespace lmms::vsthost {
// Windows shared memory contains explicit wire bytes, not atomic<T> or native
// C++ objects. Aligned ownership and diagnostic words use Interlocked operations.
// A slot has one DAW producer, one helper consumer and one DAW result consumer.
class AudioQueue
{
public:
	static constexpr std::uint32_t Slots = 3;
	static constexpr std::uint32_t MaxFrames = 4096;
	// Internal native bus storage also accommodates multi-output samplers.
	// Production entries still expose only their existing main stereo pair.
	static constexpr std::uint32_t MaxChannels = MaxAudioChannels;
	static constexpr std::uint32_t MaxSamples = MaxFrames * MaxChannels;
	static constexpr std::uint32_t MaxEventBytes = 16384;
	static constexpr std::uint32_t MaxOutputEventBytes = 32768;
	static constexpr std::uint32_t MetadataBytes = 64;
	static constexpr std::uint32_t SlotBytes = MetadataBytes + 8 * MaxSamples + MaxEventBytes + MaxOutputEventBytes;
	static constexpr std::uint32_t DiagnosticSlotBytes = 7 * 8;
	static constexpr std::uint32_t DispatcherOffset = Slots * (SlotBytes + DiagnosticSlotBytes);
	static constexpr std::uint32_t StorageBytes = DispatcherOffset + 2 * 8;
	enum class HelperActivity : std::uint32_t
	{
		Idle = 1,
		CheckingControl,
		Control,
		ClaimingAudio,
		ProcessingAudio,
		CompletingAudio,
		PumpingMessages,
		DispatchingMessage,
		WaitingForInstance,
		PluginAudioCall,
		AudioRejected
	};
	enum class HelperAudioRejection : std::uint16_t
	{
		Claim = 1,
		Layout,
		RestartEvents,
		Transport,
		EditorFeedback,
		PendingParameters,
		InputEvents,
		PluginOutput,
		OutputEvents,
		Completion,
		Exception
	};
	struct SlotDiagnostic
	{
		LONG observed = 0;
		LONG sampled = 0;
		std::uint64_t sequence = 0;
		std::array<std::uint64_t, 4> ticks{};
		std::uint64_t lastReclaimedSequence = 0;
		std::uint64_t lastReclaimedTicks = 0;
	};
	struct FullDiagnostic
	{
		std::uint64_t ticks = 0;
		std::uint64_t frequency = 0;
		std::uint64_t dispatcher = 0;
		std::uint64_t audioThread = 0;
		std::array<SlotDiagnostic, Slots> slotDiagnostics{};
	};
	void setHelperActivity(HelperActivity activity, bool audioThread = false, std::uint16_t detail = 0) noexcept
	{
		if (valid())
		{
			const auto value = (std::uint64_t(GetCurrentThreadId()) << 32)
				| (std::uint32_t(detail) << 16) | static_cast<std::uint32_t>(activity);
			storeDiagnostic(DispatcherOffset + (audioThread ? 8 : 0), value);
		}
	}
	struct Layout
	{
		std::uint32_t frames, inputs, outputs;
	};
	struct Claim
	{
		std::uint32_t slot;
		Header header;
		Layout layout;
		std::uint32_t eventBytes;
		bool collectOutput = false;
		std::uint32_t outputEventBytes = 0;
	};
	enum class Result
	{
		Ok,
		Empty,
		Full,
		Invalid
	};

	explicit AudioQueue(std::span<std::uint8_t> memory) noexcept
		: m_memory(memory)
	{
		if (memory.size() != StorageBytes || reinterpret_cast<std::uintptr_t>(memory.data()) % 8 != 0)
		{
			m_memory = {};
		}
	}
	bool valid() const noexcept { return !m_memory.empty(); }
	// Only call before publishing the mapping to a child, never on a live queue.
	bool initialize() noexcept
	{
		if (!valid())
		{
			return false;
		}
		std::fill(m_memory.begin(), m_memory.end(), 0);
		return true;
	}
	Result submit(Header header, Layout layout, std::span<const float> input, std::span<const std::uint8_t> events = {},
		bool collectOutput = false, FullDiagnostic* full = nullptr) noexcept
	{
		if (!valid() || !validLayout(layout) || input.size() != layout.frames * layout.inputs
			|| events.size() > MaxEventBytes || header.type != MessageType::Process)
		{
			return Result::Invalid;
		}
		for (std::uint32_t i = 0; i < Slots; ++i)
		{
			const auto observed = InterlockedCompareExchange(owner(i), Writing, Free);
			if (observed != Free)
			{
				if (full)
				{
					full->slotDiagnostics[i].observed = observed;
				}
				continue;
			}
			auto bytes = slot(i);
			header.payloadBytes = static_cast<std::uint32_t>(input.size() * 4 + events.size());
			const auto encoded = encode(header);
			std::copy(encoded.begin(), encoded.end(), bytes.begin() + 4);
			put(bytes, 44, layout.frames, 4);
			put(bytes, 48, layout.inputs, 4);
			put(bytes, 52, layout.outputs, 4);
			put(bytes, 56, events.size(), 4);
			put(bytes, 60, collectOutput ? 1 : 0, 4);
			writeSamples(bytes.subspan(MetadataBytes, input.size() * 4), input);
			std::copy(events.begin(), events.end(), bytes.begin() + EventOffset);
			storeDiagnostic(diagnosticOffset(i) + DiagnosticSequence, header.sequence);
			storeDiagnostic(diagnosticOffset(i) + ClaimedAt, 0);
			storeDiagnostic(diagnosticOffset(i) + CompletedAt, 0);
			storeDiagnostic(diagnosticOffset(i) + ReclaimedAt, 0);
			storeDiagnostic(diagnosticOffset(i) + SubmittedAt, clockTicks());
			InterlockedExchange(owner(i), Submitted);
			return Result::Ok;
		}
		if (full)
		{
			full->ticks = clockTicks();
			full->dispatcher = loadDiagnostic(DispatcherOffset);
			full->audioThread = loadDiagnostic(DispatcherOffset + 8);
			LARGE_INTEGER frequency{};
			QueryPerformanceFrequency(&frequency);
			full->frequency = frequency.QuadPart;
			for (std::uint32_t i = 0; i < Slots; ++i)
			{
				auto& diagnostic = full->slotDiagnostics[i];
				diagnostic.sampled = InterlockedCompareExchange(owner(i), Free, Free);
				diagnostic.sequence = loadDiagnostic(diagnosticOffset(i) + DiagnosticSequence);
				diagnostic.lastReclaimedSequence = loadDiagnostic(diagnosticOffset(i) + LastReclaimedSequence);
				diagnostic.lastReclaimedTicks = loadDiagnostic(diagnosticOffset(i) + LastReclaimedAt);
				for (std::uint32_t phase = 0; phase < diagnostic.ticks.size(); ++phase)
				{
					diagnostic.ticks[phase] = loadDiagnostic(diagnosticOffset(i) + SubmittedAt + phase * 8);
				}
			}
		}
		return Result::Full;
	}
	Result claim(Claim& claim, std::span<float> input, std::span<std::uint8_t> events) noexcept
	{
		if (!valid())
		{
			return Result::Invalid;
		}
		// Preserve submission order even when a lower numbered slot is reused.
		std::uint32_t selected = Slots;
		std::uint64_t oldest = UINT64_MAX;
		for (std::uint32_t i = 0; i < Slots; ++i)
		{
			if (InterlockedCompareExchange(owner(i), Submitted, Submitted) != Submitted)
			{
				continue;
			}
			const auto sequence = get(slot(i), 28, 8);
			if (selected == Slots || sequence < oldest)
			{
				selected = i;
				oldest = sequence;
			}
		}
		if (selected == Slots)
		{
			return Result::Empty;
		}
		if (InterlockedCompareExchange(owner(selected), Processing, Submitted) != Submitted)
		{
			return Result::Empty;
		}
		storeDiagnostic(diagnosticOffset(selected) + ClaimedAt, clockTicks());
		Claim parsed{};
		if (!readClaim(selected, MessageType::Process, parsed)
			|| input.size() < parsed.layout.frames * parsed.layout.inputs || events.size() < parsed.eventBytes)
		{
			reclaim(selected);
			return Result::Invalid;
		}
		const auto samples = parsed.layout.frames * parsed.layout.inputs;
		readSamples(slot(selected).subspan(MetadataBytes, samples * 4), input.first(samples));
		std::copy_n(slot(selected).begin() + EventOffset, parsed.eventBytes, events.begin());
		claim = parsed;
		return Result::Ok;
	}
	Result complete(
		const Claim& claim, std::span<const float> output, std::span<const std::uint8_t> outputEvents = {}) noexcept
	{
		if (!valid() || claim.slot >= Slots || !validLayout(claim.layout)
			|| output.size() != claim.layout.frames * claim.layout.outputs || outputEvents.size() > MaxOutputEventBytes
			|| (!claim.collectOutput && !outputEvents.empty())
			|| InterlockedCompareExchange(owner(claim.slot), Processing, Processing) != Processing)
		{
			return Result::Invalid;
		}
		Claim current{};
		if (!readClaim(claim.slot, MessageType::Process, current)
			|| !matches(current.header, claim.header.session, claim.header.generation, claim.header.sequence)
			|| current.layout.frames != claim.layout.frames || current.layout.outputs != claim.layout.outputs
			|| current.collectOutput != claim.collectOutput)
		{
			return Result::Invalid;
		}
		auto bytes = slot(claim.slot);
		writeSamples(bytes.subspan(OutputOffset, output.size() * 4), output);
		auto header = current.header;
		header.type = MessageType::ProcessDone;
		header.payloadBytes = static_cast<std::uint32_t>(output.size() * 4 + outputEvents.size());
		put(bytes, 60, outputEvents.size(), 4);
		std::copy(outputEvents.begin(), outputEvents.end(), bytes.begin() + OutputEventOffset);
		const auto encoded = encode(header);
		std::copy(encoded.begin(), encoded.end(), bytes.begin() + 4);
		storeDiagnostic(diagnosticOffset(claim.slot) + CompletedAt, clockTicks());
		InterlockedExchange(owner(claim.slot), Ready);
		return Result::Ok;
	}
	Result receive(std::uint64_t session, std::uint64_t generation, std::uint64_t sequence, std::span<float> output,
		std::span<std::uint8_t> outputEvents = {}, std::uint32_t* outputEventBytes = nullptr) noexcept
	{
		if (outputEventBytes)
		{
			*outputEventBytes = 0;
		}
		if (!valid())
		{
			return Result::Invalid;
		}
		for (std::uint32_t i = 0; i < Slots; ++i)
		{
			if (InterlockedCompareExchange(owner(i), Ready, Ready) != Ready)
			{
				continue;
			}
			Claim current{};
			if (!readClaim(i, MessageType::ProcessDone, current))
			{
				reclaim(i);
				return Result::Invalid;
			}
			if (current.header.session != session || current.header.generation != generation
				|| current.header.sequence < sequence)
			{
				reclaim(i);
				continue;
			}
			if (current.header.sequence != sequence)
			{
				continue;
			}
			const auto samples = current.layout.frames * current.layout.outputs;
			if (output.size() != samples || (outputEventBytes && outputEvents.size() < current.outputEventBytes))
			{
				reclaim(i);
				return Result::Invalid;
			}
			readSamples(slot(i).subspan(OutputOffset, samples * 4), output);
			if (outputEventBytes)
			{
				std::copy_n(slot(i).begin() + OutputEventOffset, current.outputEventBytes, outputEvents.begin());
				*outputEventBytes = current.outputEventBytes;
			}
			reclaim(i);
			return Result::Ok;
		}
		return Result::Empty;
	}
	// Host only, after its producer is quiescent and the helper has acknowledged
	// Pause/drained submitted work. Validate completed frames before reclaiming
	// their ownership words; never reinitialize a live shared mapping.
	bool discardCompleted(std::uint64_t session, std::uint64_t generation, std::uint64_t nextSequence) noexcept
	{
		if (!valid())
		{
			return false;
		}
		for (std::uint32_t i = 0; i < Slots; ++i)
		{
			const auto ownership = InterlockedCompareExchange(owner(i), Free, Free);
			if (ownership == Free)
			{
				continue;
			}
			Claim current{};
			if (ownership != Ready || !readClaim(i, MessageType::ProcessDone, current)
				|| current.header.session != session || current.header.generation != generation
				|| current.header.sequence >= nextSequence)
			{
				return false;
			}
			reclaim(i);
		}
		return true;
	}

private:
	enum : LONG
	{
		Free,
		Writing,
		Submitted,
		Processing,
		Ready
	};
	static constexpr std::uint32_t DiagnosticSequence = 0;
	static constexpr std::uint32_t SubmittedAt = 8;
	static constexpr std::uint32_t ClaimedAt = 16;
	static constexpr std::uint32_t CompletedAt = 24;
	static constexpr std::uint32_t ReclaimedAt = 32;
	static constexpr std::uint32_t LastReclaimedSequence = 40;
	static constexpr std::uint32_t LastReclaimedAt = 48;
	static_assert(DiagnosticSlotBytes == LastReclaimedAt + 8 && SlotBytes % 8 == 0);
	static constexpr std::uint32_t diagnosticOffset(std::uint32_t i) noexcept
	{
		return Slots * SlotBytes + i * DiagnosticSlotBytes;
	}
	static constexpr std::uint32_t OutputOffset = MetadataBytes + 4 * MaxSamples;
	static constexpr std::uint32_t EventOffset = MetadataBytes + 8 * MaxSamples;
	static constexpr std::uint32_t OutputEventOffset = EventOffset + MaxEventBytes;
	static std::uint64_t clockTicks() noexcept
	{
		LARGE_INTEGER ticks{};
		QueryPerformanceCounter(&ticks);
		return ticks.QuadPart;
	}
	volatile LONG64* diagnosticWord(std::uint32_t offset) noexcept
	{
		return reinterpret_cast<volatile LONG64*>(m_memory.data() + offset);
	}
	void storeDiagnostic(std::uint32_t offset, std::uint64_t value) noexcept
	{
		InterlockedExchange64(diagnosticWord(offset), static_cast<LONG64>(value));
	}
	std::uint64_t loadDiagnostic(std::uint32_t offset) noexcept
	{
		return InterlockedCompareExchange64(diagnosticWord(offset), 0, 0);
	}
	void reclaim(std::uint32_t i) noexcept
	{
		const auto ticks = clockTicks();
		storeDiagnostic(diagnosticOffset(i) + ReclaimedAt, ticks);
		storeDiagnostic(diagnosticOffset(i) + LastReclaimedSequence, loadDiagnostic(diagnosticOffset(i) + DiagnosticSequence));
		storeDiagnostic(diagnosticOffset(i) + LastReclaimedAt, ticks);
		InterlockedExchange(owner(i), Free);
	}
	std::span<std::uint8_t> m_memory;
	std::span<std::uint8_t> slot(std::uint32_t i) noexcept { return m_memory.subspan(i * SlotBytes, SlotBytes); }
	volatile LONG* owner(std::uint32_t i) noexcept
	{
		return reinterpret_cast<volatile LONG*>(m_memory.data() + i * SlotBytes);
	}
	static bool validLayout(Layout layout) noexcept
	{
		return layout.frames > 0 && layout.frames <= MaxFrames && layout.inputs <= MaxChannels
			&& layout.outputs <= MaxChannels;
	}
	bool readClaim(std::uint32_t i, MessageType type, Claim& claim) noexcept
	{
		auto bytes = slot(i);
		Header header{};
		if (decode(bytes.subspan(4, HeaderBytes), header) != Error::None || header.type != type)
		{
			return false;
		}
		const Layout layout{static_cast<std::uint32_t>(get(bytes, 44, 4)),
			static_cast<std::uint32_t>(get(bytes, 48, 4)), static_cast<std::uint32_t>(get(bytes, 52, 4))};
		const auto eventBytes = static_cast<std::uint32_t>(get(bytes, 56, 4));
		if (!validLayout(layout) || eventBytes > MaxEventBytes)
		{
			return false;
		}
		const auto resultBytes = static_cast<std::uint32_t>(get(bytes, 60, 4));
		if (type == MessageType::Process ? resultBytes > 1 : resultBytes > MaxOutputEventBytes)
		{
			return false;
		}
		const auto expected = type == MessageType::Process ? layout.frames * layout.inputs * 4 + eventBytes
														   : layout.frames * layout.outputs * 4 + resultBytes;
		if (header.payloadBytes != expected)
		{
			return false;
		}
		claim = {i, header, layout, eventBytes, type == MessageType::Process && resultBytes != 0,
			type == MessageType::ProcessDone ? resultBytes : 0};
		return true;
	}
	static void writeSamples(std::span<std::uint8_t> bytes, std::span<const float> samples) noexcept
	{
		static_assert(sizeof(float) == 4 && std::endian::native == std::endian::little);
		std::memcpy(bytes.data(), samples.data(), bytes.size());
	}
	static void readSamples(std::span<const std::uint8_t> bytes, std::span<float> samples) noexcept
	{
		std::memcpy(samples.data(), bytes.data(), bytes.size());
	}
};
} // namespace lmms::vsthost
#endif
