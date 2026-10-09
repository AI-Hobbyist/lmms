#ifndef LMMS_VSTHOST_LEGACY_HOST_BRIDGE_H
#define LMMS_VSTHOST_LEGACY_HOST_BRIDGE_H

#include "RemotePluginBase.h"
#include "HostSession.h"
#include "LegacyMessageCodec.h"
#include "RealtimeMidiQueue.h"
#include "Vst2Messages.h"
#include <bit>
#include <functional>
#include <cmath>

namespace lmms::vsthost {
class LegacyHostBridge final : public RemotePluginBase::MessageTransport
{
public:
	using Message = RemotePluginBase::message;
	static constexpr unsigned MaxParameters = 65536;
	explicit LegacyHostBridge(std::function<void(const Message&)> receiver)
		: m_receiver(std::move(receiver))
		, m_input(AudioQueue::MaxSamples)
		, m_output(AudioQueue::MaxSamples)
	{
	}
	bool open(HostSession::Configuration configuration)
	{
		m_loaded.store(false, std::memory_order_release);
		m_invalid = false;
		m_replies.clear();
		const bool opened = m_session.open(std::move(configuration)).get().error == Error::None;
		// The old audio producer is quiescent now. Pending controls belonged to
		// its native instance and must not leak into the replacement generation.
		for (auto& parameter : m_parameters)
		{
			parameter.store(0, std::memory_order_relaxed);
		}
		m_parameterLimit.store(0, std::memory_order_release);
		m_tempo.store(0, std::memory_order_release);
		return opened;
	}
	bool running() const noexcept
	{
		return m_loaded.load(std::memory_order_acquire) && m_session.state() == SessionState::Ready;
	}
	Error error() const noexcept { return m_session.error(); }
	DWORD pid() const noexcept { return m_session.pid(); }
	std::uint64_t generation() const noexcept { return m_session.generation(); }
	int send(const Message& message) override
	{
		if (message.id == IdInitDone || message.id == IdChangeSharedMemoryKey)
		{
			return 1;
		}
		if (message.id == IdQuit)
		{
			m_loaded.store(false, std::memory_order_release);
			return m_session.close().get().error == Error::None ? 1 : 0;
		}
		LegacyCommand command{static_cast<std::uint32_t>(message.id), {}};
		for (std::size_t i = 0; i < message.argumentCount(); ++i)
		{
			command.arguments.push_back(message.getString(static_cast<int>(i)));
		}
		const bool pause = message.id != IdVstIdleUpdate && message.id != IdShowUI && message.id != IdHideUI
			&& message.id != IdToggleUI && message.id != IdIsUIVisible;
		auto reply = m_session
						 .requestPrepared(
							 MessageType::Parameter,
							 [this, pause, command = std::move(command)]() mutable {
								 std::vector<LegacyCommand> commands;
								 if (pause)
								 {
									 flushParameters(commands);
								 }
								 commands.push_back(std::move(command));
								 std::vector<std::uint8_t> payload;
								 if (encodeLegacy(commands, payload) != Error::None)
								 {
									 throw std::runtime_error("VST2 control capacity exceeded");
								 }
								 return payload;
							 },
							 15000, pause)
						 .get();
		if (reply.error != Error::None)
		{
			return 0;
		}
		std::vector<LegacyCommand> replies;
		if (decodeLegacy(reply.payload, replies) != Error::None)
		{
			invalidate();
			return 0;
		}
		for (const auto& command : replies)
		{
			Message decoded(static_cast<int>(command.id));
			for (const auto& argument : command.arguments)
			{
				decoded.addString(argument);
			}
			m_receiver(decoded);
			if (command.id == IdInitDone)
			{
				m_loaded.store(true, std::memory_order_release);
			}
			// Native metadata is dispatched on the control caller, never on audio.
			// Only request acknowledgements are retained for the legacy wait facade.
			if (command.id == IdHostInfoGotten || command.id == IdInitDone || command.id == IdInformationUpdated
				|| command.id == IdIsUIVisible || command.id == IdSaveSettingsToFile
				|| command.id == IdLoadSettingsFromFile || command.id == IdSavePresetFile
				|| command.id == IdLoadPresetFile
				|| (command.id == IdVstProgramNames || command.id == IdVstCurrentProgram
					|| command.id == IdVstSetProgram || command.id == IdVstRotateProgram
					|| command.id == IdVstLoadAllParameterDisplays || command.id == IdVstLoadAllParameterLabels
					|| command.id == IdVstUpdateParameterDisplay || command.id == IdVstUpdateParameterLabel
					|| command.id == IdVstParameterDump))
			{
				m_replies.push_back(std::move(decoded));
			}
		}
		return 1;
	}
	Message receive() override
	{
		if (m_replies.empty())
		{
			return {};
		}
		auto message = std::move(m_replies.front());
		m_replies.pop_front();
		return message;
	}
	bool pending() const override { return !m_replies.empty(); }
	bool invalid() const override { return m_invalid || m_session.error() != Error::None; }
	void invalidate() override
	{
		m_invalid = true;
		m_session.failRealtime(Error::InvalidMessage);
	}
	void midi(std::array<std::uint32_t, 5> values) noexcept
	{
		if (!m_midi.push({generation(), values}))
		{
			m_session.failRealtime(Error::ProcessingFailed);
		}
	}
	void parameter(unsigned index, float value) noexcept
	{
		if (index >= MaxParameters || !std::isfinite(value))
		{
			m_session.failRealtime(Error::InvalidMessage);
			return;
		}
		m_parameters[index].store((UINT64_C(1) << 32) | std::bit_cast<std::uint32_t>(value), std::memory_order_release);
		auto limit = m_parameterLimit.load(std::memory_order_relaxed);
		for (unsigned attempt = 0; attempt < 8 && limit < index + 1; ++attempt)
		{
			m_parameterLimit.compare_exchange_strong(limit, index + 1, std::memory_order_release);
		}
		if (m_parameterLimit.load(std::memory_order_acquire) < index + 1)
		{
			m_session.failRealtime(Error::ProcessingFailed);
		}
	}
	void tempo(unsigned value) noexcept { m_tempo.store((UINT64_C(1) << 32) | value, std::memory_order_release); }
	bool process(
		std::uint32_t frames, unsigned inputs, unsigned outputs, const float* input, float* output, bool offline)
	{
		if (!m_loaded.load(std::memory_order_acquire))
		{
			if (output && frames <= AudioQueue::MaxFrames)
			{
				std::fill_n(output, frames * 2, 0.0f);
			}
			return false;
		}
		if (!frames || frames > AudioQueue::MaxFrames || inputs > AudioQueue::MaxChannels
			|| outputs > AudioQueue::MaxChannels)
		{
			m_session.failRealtime(Error::InvalidMessage);
			return false;
		}
		for (unsigned frame = 0; frame < frames; ++frame)
		{
			for (unsigned channel = 0; channel < inputs; ++channel)
			{
				m_input[frame * inputs + channel] = input && channel < 2 ? input[frame * 2 + channel] : 0.0f;
			}
		}
		const AudioQueue::Layout layout{frames, inputs, outputs};
		const auto in = std::span(m_input).first(frames * inputs);
		const auto out = std::span(m_output).first(frames * outputs);
		bool success = false;
		if (offline)
		{
			auto reply
				= m_session
					  .renderOffline(layout, std::vector<float>(in.begin(), in.end()), [this] { return audioEvents(); })
					  .get();
			success = reply.error == Error::None && reply.payload.size() == out.size_bytes();
			if (success)
			{
				std::memcpy(out.data(), reply.payload.data(), out.size_bytes());
			}
		}
		else
		{
			success = m_session.processPrepared(layout, in, out, [this] { return audioEvents(); });
		}
		if (output)
		{
			for (unsigned frame = 0; frame < frames; ++frame)
			{
				for (unsigned channel = 0; channel < 2; ++channel)
				{
					output[frame * 2 + channel] = success && channel < outputs ? out[frame * outputs + channel] : 0.0f;
				}
			}
		}
		return success;
	}

private:
	void flushParameters(std::vector<LegacyCommand>& commands)
	{
		const auto limit = m_parameterLimit.load(std::memory_order_acquire);
		for (unsigned index = 0; index < limit; ++index)
		{
			if (const auto pending = m_parameters[index].exchange(0, std::memory_order_acq_rel))
			{
				commands.push_back({IdVstSetParameter,
					{std::to_string(index),
						std::to_string(std::bit_cast<float>(static_cast<std::uint32_t>(pending)))}});
			}
		}
		if (const auto pending = m_tempo.exchange(0, std::memory_order_acq_rel))
		{
			commands.push_back({IdVstSetTempo, {std::to_string(static_cast<std::uint32_t>(pending))}});
		}
	}
	std::span<const std::uint8_t> audioEvents() noexcept
	{
		std::uint32_t count = 0;
		RealtimeMidiQueue::Event event{};
		for (unsigned i = 0; i < RealtimeMidiQueue::Capacity && m_midi.pop(event); ++i)
		{
			if (event.generation != generation())
			{
				continue;
			}
			for (unsigned field = 0; field < 5; ++field)
			{
				put(m_events, count + field * 4, event.values[field], 4);
			}
			count += 20;
		}
		const auto limit = m_parameterLimit.load(std::memory_order_acquire);
		for (unsigned index = 0; index < limit && count + 20 <= m_events.size(); ++index)
		{
			if (const auto pending = m_parameters[index].exchange(0, std::memory_order_acq_rel))
			{
				put(m_events, count, 256, 4);
				put(m_events, count + 4, 0, 4);
				put(m_events, count + 8, index, 4);
				put(m_events, count + 12, pending, 4);
				put(m_events, count + 16, 0, 4);
				count += 20;
			}
		}
		if (count + 20 <= m_events.size())
		{
			if (const auto pending = m_tempo.exchange(0, std::memory_order_acq_rel))
			{
				put(m_events, count, 257, 4);
				put(m_events, count + 4, 0, 4);
				put(m_events, count + 8, pending, 4);
				put(m_events, count + 12, 0, 4);
				put(m_events, count + 16, 0, 4);
				count += 20;
			}
		}
		return std::span(m_events).first(count);
	}
	HostSession m_session;
	std::function<void(const Message&)> m_receiver;
	std::deque<Message> m_replies;
	bool m_invalid = false;
	std::atomic<bool> m_loaded{false};
	std::vector<float> m_input, m_output;
	RealtimeMidiQueue m_midi;
	std::array<std::uint8_t, AudioQueue::MaxEventBytes> m_events{};
	std::array<std::atomic<std::uint64_t>, MaxParameters> m_parameters{};
	std::atomic<unsigned> m_parameterLimit{0};
	std::atomic<std::uint64_t> m_tempo{0};
};
} // namespace lmms::vsthost
#endif
