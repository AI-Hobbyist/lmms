#ifndef LMMS_VSTHOST_FIXED_QUANTUM_BUFFER_H
#define LMMS_VSTHOST_FIXED_QUANTUM_BUFFER_H

#include "AudioQueue.h"
#include "Vst3BlockEvents.h"
#include "Vst3OutputEvents.h"
#include <array>
#include <vector>

namespace lmms::vsthost
{
// One audio owner. Configure/reset require quiescence. The submit callback
// returns the preceding quantum, so assembly plus the bridge costs two quanta.
// Storage is allocated only by configure; process never waits or allocates.
class FixedQuantumBuffer
{
public:
	bool configure(AudioQueue::Layout quantum, double sampleRate)
	{
		if (!quantum.frames || quantum.frames > AudioQueue::MaxFrames ||
			quantum.inputs > AudioQueue::MaxChannels || quantum.outputs > AudioQueue::MaxChannels ||
			!std::isfinite(sampleRate) || sampleRate < 8000 || sampleRate > 768000) { return false; }
		m_input.resize(std::size_t(quantum.frames) * quantum.inputs);
		m_output.resize(std::size_t(quantum.frames) * quantum.outputs);
		m_quantum = quantum; m_sampleRate = sampleRate; reset(); return true;
	}
	void reset() noexcept
	{
		m_position = 0; m_eventCount = 0; m_outputEventCount = 0; m_havePrevious = false;
		std::fill(m_input.begin(), m_input.end(), 0.0f);
		std::fill(m_output.begin(), m_output.end(), 0.0f);
	}
	std::uint32_t latency() const noexcept { return 2 * m_quantum.frames; }
	std::span<const Vst3BlockEvent> pendingEvents() const noexcept
	{ return std::span(m_events).first(m_eventCount); }
	template<class Submit> bool process(std::uint32_t frames, std::span<const float> input,
		std::span<float> output, std::span<const Vst3BlockEvent> events,
		const Vst3Transport& transport, Submit submit) noexcept
	{
		return processWithOutput(frames, input, output, events, transport,
			[&](auto layout, auto samples, auto result, auto staged, const auto& position, auto, auto& bytes) {
				bytes = 0; return submit(layout, samples, result, staged, position);
			}, [](const auto&, const auto&, auto) { return true; });
	}
	// Submit returns the preceding quantum's encoded MIDI output in reusable
	// storage. Delivery accompanies the matching audio segment; source identity
	// and transport are preserved, with a separate offset in the current callback.
	template<class Submit, class Deliver> bool processWithOutput(std::uint32_t frames,
		std::span<const float> input, std::span<float> output, std::span<const Vst3BlockEvent> events,
		const Vst3Transport& transport, Submit submit, Deliver deliver) noexcept
	{
		std::fill(output.begin(), output.end(), 0.0f);
		if (!m_quantum.frames || !frames || frames > AudioQueue::MaxFrames ||
			input.size() != std::size_t(frames) * m_quantum.inputs ||
			output.size() != std::size_t(frames) * m_quantum.outputs || !validVst3Transport(transport) ||
			transport.continuous > INT64_MAX - frames ||
			((transport.flags & 1) && transport.samples > INT64_MAX - frames)) { return false; }
		// Validate before advancing audio or consuming any events.
		for (std::size_t i = 0; i < events.size(); ++i)
		{
			if (!validVst3Event(events[i], frames) || (i && events[i - 1].offset > events[i].offset)) { return false; }
		}
		std::uint32_t consumed = 0; std::size_t eventIndex = 0;
		while (consumed < frames)
		{
			const auto count = std::min(frames - consumed, m_quantum.frames - m_position);
			if (!m_position)
			{
				m_transport = transport;
				m_transport.continuous += consumed;
				if (transport.flags & 1)
				{
					m_transport.samples += consumed;
					m_transport.music += consumed * transport.tempo / (60 * m_sampleRate);
					const auto barLength = transport.numerator * 4.0 / transport.denominator;
					m_transport.bar = std::floor(m_transport.music / barLength) * barLength;
				}
			}
			while (eventIndex < events.size() && events[eventIndex].offset < consumed + count)
			{
				if (m_eventCount == m_events.size()) { reset(); std::fill(output.begin(), output.end(), 0.0f); return false; }
				auto event = events[eventIndex++]; event.offset = m_position + event.offset - consumed;
				m_events[m_eventCount++] = event;
			}
			std::copy_n(input.begin() + std::size_t(consumed) * m_quantum.inputs,
				std::size_t(count) * m_quantum.inputs, m_input.begin() + std::size_t(m_position) * m_quantum.inputs);
			std::copy_n(m_output.begin() + std::size_t(m_position) * m_quantum.outputs,
				std::size_t(count) * m_quantum.outputs, output.begin() + std::size_t(consumed) * m_quantum.outputs);
			// Output events need not be sorted. Scan the bounded batch so split
			// callbacks deliver each event once, preserving equal-offset order.
			for (std::size_t i = 0; i < m_outputEventCount; ++i)
			{
				const auto& event = m_outputEvents[i];
				if (event.event.offset >= m_position && event.event.offset < m_position + count &&
					!deliver(event, m_outputTransport, consumed + event.event.offset - m_position))
				{ reset(); std::fill(output.begin(), output.end(), 0.0f); return false; }
			}
			m_position += count; consumed += count;
			if (m_position == m_quantum.frames)
			{
				std::uint32_t bytes = 0;
				if (!submit(m_quantum, std::span<const float>(m_input), std::span<float>(m_output),
					pendingEvents(), m_transport, std::span(m_outputPacket), bytes) || bytes > m_outputPacket.size())
				{ reset(); std::fill(output.begin(), output.end(), 0.0f); return false; }
				m_outputEventCount = 0;
				if (bytes && (!m_havePrevious || !decodeVst3OutputEvents(std::span(m_outputPacket).first(bytes),
					[&](const Vst3OutputEvent& event) {
						if (event.frames != m_quantum.frames || m_outputEventCount == m_outputEvents.size()) { return false; }
						m_outputEvents[m_outputEventCount++] = event; return true;
					})))
				{ reset(); std::fill(output.begin(), output.end(), 0.0f); return false; }
				m_outputTransport = m_previousTransport;
				m_previousTransport = m_transport; m_havePrevious = true;
				m_position = 0; m_eventCount = 0;
			}
		}
		return true;
	}
private:
	AudioQueue::Layout m_quantum{};
	double m_sampleRate = 0;
	std::uint32_t m_position = 0;
	std::size_t m_eventCount = 0;
	std::vector<float> m_input, m_output;
	std::array<Vst3BlockEvent, (AudioQueue::MaxEventBytes - 80) / 32> m_events{};
	std::array<std::uint8_t, AudioQueue::MaxOutputEventBytes> m_outputPacket{};
	std::array<Vst3OutputEvent, 512> m_outputEvents{};
	std::size_t m_outputEventCount = 0;
	bool m_havePrevious = false;
	Vst3Transport m_transport, m_previousTransport, m_outputTransport;
};
}
#endif
