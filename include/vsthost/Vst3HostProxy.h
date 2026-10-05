#ifndef LMMS_VSTHOST_VST3_HOST_PROXY_H
#define LMMS_VSTHOST_VST3_HOST_PROXY_H

#include "HostSession.h"
#include "AudioDelayLine.h"
#include "FixedQuantumBuffer.h"
#include "RealtimeMidiQueue.h"
#include "Vst3BlockEvents.h"
#include "Vst3Commands.h"
#include "Vst3Metadata.h"
#include "Vst3OutputEvents.h"
#include "Vst3ParameterFeedback.h"
#include "Vst3Preset.h"
#include <functional>

namespace lmms::vsthost
{
// SDK-free DAW endpoint. Control methods have one owner, with a quiescent audio
// producer for open/close/configure. process and postEvent never wait or allocate. Offline
// processing is exclusively a renderer operation and uses supervised deadlines.
class Vst3HostProxy
{
public:
	using RealtimeMidiReceiver = bool (*)(void*, const Vst3OutputEvent&, const Vst3Transport&, std::uint32_t) noexcept;
	struct Feedback
	{
		Error error = Error::None;
		std::uint32_t restart = 0;
		std::vector<Vst3ParameterFeedback> edits;
	};
	bool open(HostSession::Configuration configuration, const Vst3Create& instance)
	{
		m_loaded.store(false, std::memory_order_release);
		m_localError.store(Error::None, std::memory_order_release);
		m_metadata = {}; m_dimensions.store(0); m_maxFrames = instance.maxFrames; m_cid = instance.cid; m_deferredCount = 0; m_sampleRate = instance.sampleRate; m_realtime.reset();
		RealtimeMidiQueue::Event discarded;
		while (m_events.pop(discarded)) { }
		std::vector<std::uint8_t> payload;
		if (!encodeVst3Create(instance, payload) ||
			std::all_of(instance.cid.begin(), instance.cid.end(), [](auto byte) { return byte == 0; }))
		{ return fail(Error::InvalidMessage); }
		const auto opened = m_session.open(std::move(configuration)).get();
		if (opened.error != Error::None) { return fail(opened.error); }
		const auto created = m_session.request(MessageType::Create, std::move(payload), 15000, true).get();
		if (created.error != Error::None) { return fail(created.error); }
		const auto& bytes = created.payload;
		if (bytes.size() < 20 || get(bytes, 0, 4) > AudioQueue::MaxChannels || get(bytes, 4, 4) > AudioQueue::MaxChannels ||
			get(bytes, 8, 4) > 1 || get(bytes, 16, 4) > 65536 || bytes.size() != 20 + 4 * get(bytes, 16, 4))
		{ return fail(Error::InvalidMessage); }
		if (!refreshMetadata()) { return false; }
		if (get(bytes, 0, 4) != m_metadata.inputs || get(bytes, 4, 4) != m_metadata.outputs ||
			get(bytes, 8, 4) != m_metadata.sampleSize || get(bytes, 12, 4) != m_metadata.latency ||
			get(bytes, 16, 4) != m_metadata.parameters.size()) { return fail(Error::InvalidMessage); }
		for (std::size_t i = 0; i < m_metadata.parameters.size(); ++i)
		{ if (get(bytes, 20 + i * 4, 4) != m_metadata.parameters[i].id) { return fail(Error::InvalidMessage); } }
		m_loaded.store(true, std::memory_order_release); return true;
	}
	Error close()
	{
		m_loaded.store(false, std::memory_order_release);
		return m_session.close().get().error;
	}
	bool running() const noexcept { return m_loaded.load(std::memory_order_acquire) && error() == Error::None && m_session.state() == SessionState::Ready; }
	Error error() const noexcept
	{
		const auto local = m_localError.load(std::memory_order_acquire);
		return local != Error::None ? local : m_session.error();
	}
	DWORD pid() const noexcept { return m_session.pid(); }
	std::uint64_t generation() const noexcept { return m_session.generation(); }
	ProcessSupervisor::Fault fault() const noexcept { return m_session.fault(); }
	const Vst3Metadata& metadata() const noexcept { return m_metadata; } // Control owner only.
	// Control-owner callback, before native processing resumes; no session requests.
	void setMetadataPublication(std::function<void()> publish) { m_publish = std::move(publish); }
	bool refreshMetadata()
	{
		std::vector<std::uint8_t> selector(4); put(selector, 0, 1, 4);
		const auto reply = m_session.requestPreparedPublished(MessageType::Parameter,
			[this, selector = std::move(selector)]() mutable { retireBuffered(); return std::move(selector); },
			[this](HostSession::Reply& result) { publishMetadata(result); }, 15000);
		return reply.error == Error::None || fail(reply.error);
	}
	Error configure(const Vst3Setup& setup)
	{
		std::vector<std::uint8_t> bytes;
		if (!encodeVst3Setup(setup, bytes)) { return Error::InvalidMessage; }
		const auto reply = m_session.requestPreparedPublished(MessageType::Parameter,
			[this, bytes = std::move(bytes)]() mutable { retireBuffered(); return std::move(bytes); },
			[this, setup](HostSession::Reply& result) {
				m_maxFrames.store(setup.maxFrames, std::memory_order_release); m_sampleRate = setup.sampleRate;
				publishMetadata(result);
			}, 15000);
		if (reply.error != Error::None) { fail(reply.error); }
		return reply.error;
	}
	std::uint32_t bridgeLatency() const noexcept { return 2 * m_maxFrames.load(std::memory_order_acquire); }
	std::uint64_t processingLatency(bool offline) const noexcept
	{ return m_pluginLatency.load(std::memory_order_acquire) + (offline ? 0ULL : bridgeLatency()); }
	Feedback poll() { return parameterControl({}); }
	Feedback setParameter(std::uint32_t id, double value)
	{
		if (!std::isfinite(value) || value < 0 || value > 1) { fail(Error::InvalidMessage); return {error()}; }
		std::vector<std::uint8_t> payload(12); put(payload, 0, id, 4); put(payload, 4, std::bit_cast<std::uint64_t>(value), 8);
		return parameterControl(std::move(payload));
	}
	HostSession::Reply state()
	{
		// Pause can apply an asynchronous native restart. Publish its resulting
		// layout before Resume, while returning the original opaque state bytes.
		std::vector<std::uint8_t> saved;
		auto reply = m_session.requestPreparedFollowedPublished(MessageType::GetState,
			[this] { return stateEvents(); },
			[this, &saved](HostSession::Reply& result) -> std::optional<HostSession::Followup> {
				saved = std::move(result.payload); return metadataFollowup();
			}, [this, &saved](HostSession::Reply& result) {
				publishMetadata(result);
				if (result.error == Error::None) { result.payload = std::move(saved); }
			}, 15000);
		if (reply.error != Error::None) { fail(reply.error); }
		return reply;
	}
	HostSession::Reply preset()
	{
		auto reply = state();
		if (reply.error != Error::None) { return reply; }
		std::vector<std::uint8_t> bytes;
		if (!encodeVst3Preset(m_cid, reply.payload, bytes)) { return {Error::InvalidMessage, {}}; }
		return {Error::None, std::move(bytes)};
	}
	Error restorePreset(std::span<const std::uint8_t> bytes)
	{
		std::vector<std::uint8_t> decoded;
		// An invalid user file never reaches or faults a healthy native instance.
		if (!decodeVst3Preset(m_cid, bytes, decoded)) { return Error::InvalidMessage; }
		return restoreState(std::move(decoded));
	}
	Error restoreState(std::vector<std::uint8_t> bytes)
	{
		if (!validVst3State(bytes)) { return Error::InvalidMessage; }
		const auto reply = m_session.requestPreparedFollowedPublished(MessageType::SetState,
			[this, bytes = std::move(bytes)]() mutable {
				// The restored state supersedes earlier queued automation. Preserve
				// MIDI and events posted after this boundary for subsequent audio.
				stateEvents(); return std::move(bytes);
			}, [this](HostSession::Reply&) -> std::optional<HostSession::Followup> { return metadataFollowup(); },
			[this](HostSession::Reply& result) { publishMetadata(result); });
		if (reply.error != Error::None) { fail(reply.error); }
		return reply.error;
	}
		HostSession::Reply showEditor() { return editorControl(MessageType::ShowEditor); }
		Error hideEditor() { return editorControl(MessageType::HideEditor).error; }
	HostSession::Reply midiOutput() { return m_session.request(MessageType::Midi, {}, 15000).get(); }
	bool postEvent(const Vst3BlockEvent& event) noexcept
	{
		if (!validVst3Event(event, m_maxFrames) || !running()) { return false; }
		const auto bits = std::bit_cast<std::uint64_t>(event.value);
		const RealtimeMidiQueue::Event queued{generation(), {event.type | (event.bus << 3) | (event.channel << 8),
			event.id, event.offset, static_cast<std::uint32_t>(bits), static_cast<std::uint32_t>(bits >> 32)}};
		if (!m_events.push(queued)) { return fail(Error::ProcessingFailed); } return true;
	}
	// Admit the caller's complete realtime assembly and output mapping before
	// metadata publication. Offline rendering must not hold this admission:
	// renderOffline itself quiesces realtime audio on the dispatcher.
	template<class Process> bool withAudio(Process process) noexcept
	{ return m_session.withAudio(std::move(process)); }
	bool process(AudioQueue::Layout layout, std::span<const float> input, std::span<float> output,
		const Vst3Transport& transport, bool offline, std::span<const float> dryInput = {},
		float wet = 1.0f, float dry = 0.0f, std::vector<Vst3OutputEvent>* generated = nullptr,
		RealtimeMidiReceiver receiver = nullptr, void* receiverContext = nullptr)
	{
		std::fill(output.begin(), output.end(), 0.0f);
		if (generated) { if (!offline) { return false; } generated->clear(); }
		if (offline && receiver) { return false; }
		const auto dimensions = m_dimensions.load(std::memory_order_acquire);
		if (!running() || !layout.frames || layout.frames > m_maxFrames ||
			layout.inputs != (dimensions & 255) || layout.outputs != (dimensions >> 8) ||
			input.size() != std::size_t(layout.frames) * layout.inputs || output.size() != std::size_t(layout.frames) * layout.outputs ||
			!validVst3Transport(transport) || (!dryInput.empty() && dryInput.size() != output.size()) ||
			!std::isfinite(wet) || !std::isfinite(dry)) { return false; }
		const auto prepare = [this, layout, transport] { return blockEvents(layout.frames, transport); };
		bool success = false;
		if (offline)
		{
			const auto reply = m_session.renderOffline(layout, std::vector<float>(input.begin(), input.end()), prepare,
				5000, generated ? std::optional<MessageType>{MessageType::Midi} : std::nullopt).get();
			success = reply.error == Error::None && reply.payload.size() == output.size_bytes();
			if (success && generated)
			{
				success = decodeVst3OutputEvents(reply.followupPayload, [&](const auto& event) { generated->push_back(event); return true; });
				if (!success) { generated->clear(); m_session.failRealtime(Error::InvalidMessage); }
			}
			if (success)
			{ std::memcpy(output.data(), reply.payload.data(), output.size_bytes()); mixDry(output, dryInput, wet, dry, m_dryOffline); }
		}
		else
		{
			success = m_session.withAudio([&] {
				const auto packet = blockEvents(layout.frames, transport);
				if (packet.empty() || error() != Error::None) { return false; }
				const auto processed = m_realtime.processWithOutput(layout.frames, input, output,
					std::span(m_block).first(static_cast<std::size_t>(get(packet, 4, 4))), transport,
					[this, receiver](auto quantum, auto samples, auto result, auto events, const auto& position, auto storage, auto& bytes) {
						return m_session.processPrepared(quantum, samples, result, [&] {
							return encodeVst3Events(events, m_packet, quantum.frames, &position);
						}, storage, receiver ? &bytes : nullptr);
					}, [&](const auto& event, const auto& position, auto offset) {
						return !receiver || receiver(receiverContext, event, position, offset);
					});
				if (!processed && receiver) { return fail(Error::ProcessingFailed); }
				if (processed) { mixDry(output, dryInput, wet, dry, m_dryRealtime); }
				return processed;
			});
			if (!success) { std::fill(output.begin(), output.end(), 0.0f); }
		}
		if (error() != Error::None) { std::fill(output.begin(), output.end(), 0.0f); return false; } return success;
	}
private:
		HostSession::Reply editorControl(MessageType type)
		{
			// Native window operations run on the helper's audio owner thread.
			// Retire partial buffers under the existing pause/resume boundary so
			// editor creation/activation cannot be mistaken for an audio deadline miss.
			auto reply = m_session.requestPrepared(type, [this] {
				retireBuffered(); return std::vector<std::uint8_t>{};
			}, 15000, true).get();
			if (reply.error != Error::None) { fail(reply.error); }
			return reply;
		}
	void publishMetadata(HostSession::Reply& result)
	{
		Vst3Metadata metadata;
		if (!decodeVst3Metadata(result.payload, metadata) ||
			!m_realtime.configure({m_maxFrames.load(), metadata.inputs, metadata.outputs}, m_sampleRate))
		{ result.error = Error::InvalidMessage; fail(result.error); return; }
		// Bound delay storage explicitly; reject rather than truncate oversized
		// reports. This preparation runs on the quiescent control owner.
		const auto offlineSamples = std::uint64_t(metadata.latency) * metadata.outputs;
		const auto realtimeSamples = (std::uint64_t(metadata.latency) + bridgeLatency()) * metadata.outputs;
		constexpr std::uint64_t maximumSamples = 32ULL * 1024 * 1024;
		if (realtimeSamples > maximumSamples)
		{ result.error = Error::ProcessingFailed; fail(result.error); return; }
		m_dryRealtime.prepare(static_cast<std::size_t>(realtimeSamples));
		m_dryOffline.prepare(static_cast<std::size_t>(offlineSamples));
		m_dryRealtime.setDelay(static_cast<std::size_t>(realtimeSamples));
		m_dryOffline.setDelay(static_cast<std::size_t>(offlineSamples));
		m_pluginLatency.store(metadata.latency, std::memory_order_release);
		m_metadata = std::move(metadata);
		m_dimensions.store(m_metadata.inputs | (m_metadata.outputs << 8), std::memory_order_release);
		if (m_publish) { m_publish(); }
	}
	static void mixDry(std::span<float> output, std::span<const float> input,
		float wet, float dry, AudioDelayLine<float>& delay) noexcept
	{
		if (input.empty()) { return; }
		for (std::size_t sample = 0; sample < output.size(); ++sample)
		{
			float delayed = 0;
			delay.process(input.subspan(sample, 1), std::span(&delayed, 1));
			output[sample] = wet * output[sample] + dry * delayed;
		}
	}
	void appendDeferred(Vst3BlockEvent event)
	{
		if (m_deferredCount == m_deferred.size())
		{ fail(Error::ProcessingFailed); throw std::runtime_error("VST3 buffered event capacity"); }
		event.offset = 0; m_deferred[m_deferredCount++] = event;
	}
	void retireBuffered(std::uint32_t superseded = 0, bool discardParameter = false)
	{
		// Called only under the session's whole-callback admission barrier.
		const auto boundary = m_events.writePosition();
		const auto matches = [&](const auto& event) { return discardParameter && event.type == 0 && event.id == superseded; };
		std::size_t retained = 0;
		for (std::size_t i = 0; i < m_deferredCount; ++i)
		{ if (!matches(m_deferred[i])) { m_deferred[retained++] = m_deferred[i]; } }
		m_deferredCount = retained;
		for (auto event : m_realtime.pendingEvents())
		{ if (!matches(event)) { appendDeferred(event); } }
		m_realtime.reset(); m_dryRealtime.reset(); m_dryOffline.reset();
		if (!discardParameter) { return; }
		// A setter supersedes matching edits published before this boundary in
		// every staging area. Later posts belong to subsequent callbacks.
		const auto deadline = GetTickCount64() + 500;
		std::size_t count = 0;
		while (m_events.readPosition() < boundary)
		{
			RealtimeMidiQueue::Event queued;
			if (!m_events.pop(queued))
			{
				if (GetTickCount64() >= deadline)
				{ fail(Error::Timeout); throw std::runtime_error("VST3 setter publication barrier timed out"); }
				Sleep(1); continue;
			}
			if (!currentQueued(queued)) { continue; }
			const auto event = decodeQueued(queued);
			if (matches(event)) { continue; }
			if (count == m_block.size())
			{ fail(Error::ProcessingFailed); throw std::runtime_error("VST3 setter event capacity"); }
			auto position = count++;
			while (position && m_block[position - 1].offset > event.offset)
			{ m_block[position] = m_block[position - 1]; --position; }
			m_block[position] = event;
		}
		// The control boundary retires partial audio. Preserve ordering while
		// moving unaffected events to the beginning of the next audio block.
		for (std::size_t i = 0; i < count; ++i) { appendDeferred(m_block[i]); }
	}
	bool fail(Error value) noexcept
	{
		Error empty = Error::None; m_localError.compare_exchange_strong(empty, value, std::memory_order_acq_rel);
		m_session.failRealtime(value); return false;
	}
	HostSession::Followup metadataFollowup()
	{
		return {MessageType::Parameter, [this] {
			retireBuffered();
			std::vector<std::uint8_t> selector(4); put(selector, 0, 1, 4); return selector;
		}};
	}
	Feedback parameterControl(std::vector<std::uint8_t> payload)
	{
		// Ordinary feedback preserves the current pipeline. Setters and requested
		// restarts publish metadata within the same pause/resume transaction.
		const bool setter = !payload.empty();
		Feedback result;
		bool metadata = false;
		const auto reply = m_session.requestPreparedFollowedPublished(MessageType::Parameter,
			[this, payload = std::move(payload), setter]() mutable {
				if (setter) { retireBuffered(static_cast<std::uint32_t>(get(payload, 0, 4)), true); }
				return std::move(payload);
			}, [this, &result, &metadata, setter](HostSession::Reply& reply) -> std::optional<HostSession::Followup> {
				if (!decodeVst3Feedback(reply.payload, result.restart, [&](const auto& edit) { result.edits.push_back(edit); return true; }))
				{ reply.error = Error::InvalidMessage; return {}; }
				metadata = setter || result.restart != 0;
				return metadata ? std::optional(metadataFollowup()) : std::nullopt;
			}, [this, &metadata](HostSession::Reply& reply) { if (metadata) { publishMetadata(reply); } }, 15000, setter);
		if (reply.error != Error::None) { fail(reply.error); result.error = reply.error; result.edits.clear(); return result; }
		for (const auto& edit : result.edits)
		{
			if (std::none_of(m_metadata.parameters.begin(), m_metadata.parameters.end(), [&](const auto& info) { return info.id == edit.id; }))
			{ fail(Error::InvalidMessage); result.error = error(); result.edits.clear(); break; }
		}
		return result;
	}
	bool currentQueued(const RealtimeMidiQueue::Event& queued) const noexcept
	{
		return queued.generation == generation();
	}
	static Vst3BlockEvent decodeQueued(const RealtimeMidiQueue::Event& queued) noexcept
	{
		return {queued.values[0] & 7, queued.values[2], (queued.values[0] >> 3) & 31,
			(queued.values[0] >> 8) & 15, queued.values[1],
			std::bit_cast<double>(std::uint64_t{queued.values[3]} | (std::uint64_t{queued.values[4]} << 32))};
	}
	std::vector<std::uint8_t> stateEvents()
	{
		// requestPrepared has quiesced the sole audio consumer and paused/drained
		// native processing before invoking this callback on the session worker.
		const auto boundary = m_events.writePosition();
		const auto deadline = GetTickCount64() + 500;
		retireBuffered();
		std::size_t count = 0, retained = 0;
		for (std::size_t i = 0; i < m_deferredCount; ++i)
		{
			const auto event = m_deferred[i];
			if (event.type == 0)
			{
				auto position = count++;
				while (position && m_block[position - 1].offset > event.offset)
				{ m_block[position] = m_block[position - 1]; --position; }
				m_block[position] = event;
			}
			else { m_deferred[retained++] = event; }
		}
		m_deferredCount = retained;
		while (m_events.readPosition() < boundary)
		{
			RealtimeMidiQueue::Event queued;
			if (!m_events.pop(queued))
			{
				if (GetTickCount64() >= deadline) { fail(Error::Timeout); throw std::runtime_error("VST3 event publication barrier timed out"); }
				Sleep(1); continue;
			}
			if (!currentQueued(queued)) { continue; }
			const auto event = decodeQueued(queued);
			if (event.type != 0)
			{
				if (m_deferredCount == m_deferred.size()) { fail(Error::ProcessingFailed); throw std::runtime_error("VST3 deferred MIDI capacity"); }
				m_deferred[m_deferredCount++] = event; continue;
			}
			if (count == m_block.size()) { fail(Error::ProcessingFailed); throw std::runtime_error("VST3 state parameter capacity"); }
			auto position = count++;
			while (position && m_block[position - 1].offset > event.offset)
			{ m_block[position] = m_block[position - 1]; --position; }
			m_block[position] = event;
		}
		if (!count) { return {}; }
		// Snapshot edits are applied by zero-sample processing. Retain their
		// established order, but do not send obsolete offsets after setup changes.
		for (std::size_t i = 0; i < count; ++i) { m_block[i].offset = 0; }
		const auto packet = encodeVst3Events(std::span(m_block).first(count), m_packet, m_maxFrames);
		if (packet.empty()) { fail(Error::InvalidMessage); throw std::runtime_error("Invalid VST3 state parameters"); }
		return {packet.begin(), packet.end()};
	}
	std::span<const std::uint8_t> blockEvents(std::uint32_t frames, const Vst3Transport& transport) noexcept
	{
		std::size_t count = 0; RealtimeMidiQueue::Event queued;
		std::size_t deferred = 0, futureCount = 0;
		while (deferred < m_deferredCount || m_events.pop(queued))
		{
			Vst3BlockEvent event;
			if (deferred < m_deferredCount) { event = m_deferred[deferred++]; }
			else
			{
				if (!currentQueued(queued)) { continue; }
				event = decodeQueued(queued);
			}
			if (count == m_block.size()) { fail(Error::ProcessingFailed); return {}; }
			if (!validVst3Event(event, AudioQueue::MaxFrames)) { fail(Error::InvalidMessage); return {}; }
			if (event.offset >= frames)
			{
				if (futureCount == m_deferred.size()) { fail(Error::ProcessingFailed); return {}; }
				event.offset -= frames; m_deferred[futureCount++] = event; continue;
			}
			// Stable bounded insertion preserves FIFO ordering at equal offsets.
			auto position = count++;
			while (position && m_block[position - 1].offset > event.offset)
			{ m_block[position] = m_block[position - 1]; --position; }
			m_block[position] = event;
		}
		m_deferredCount = futureCount;
		return encodeVst3Events(std::span(m_block).first(count), m_packet, frames, &transport);
	}
	std::array<std::uint8_t, 16> m_cid{};
	HostSession m_session;
	FixedQuantumBuffer m_realtime;
	AudioDelayLine<float> m_dryRealtime, m_dryOffline;
	std::atomic<std::uint64_t> m_pluginLatency{0};
	double m_sampleRate = 48000;
	Vst3Metadata m_metadata;
	std::function<void()> m_publish;
	std::atomic<Error> m_localError{Error::None};
	std::atomic<bool> m_loaded{false};
	std::atomic<std::uint32_t> m_dimensions{0};
	std::atomic<std::uint32_t> m_maxFrames{0};
	RealtimeMidiQueue m_events;
	std::array<Vst3BlockEvent, (AudioQueue::MaxEventBytes - 80) / 32> m_block{};
	std::array<Vst3BlockEvent, (AudioQueue::MaxEventBytes - 80) / 32> m_deferred{};
	std::size_t m_deferredCount = 0;
	std::array<std::uint8_t, AudioQueue::MaxEventBytes> m_packet{};
};
} // namespace lmms::vsthost
#endif
