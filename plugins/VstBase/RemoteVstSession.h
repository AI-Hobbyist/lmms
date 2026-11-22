#ifndef LMMS_REMOTE_VST_SESSION_H
#define LMMS_REMOTE_VST_SESSION_H

#include "vsthost/AudioQueue.h"

#include <array>
#include "vsthost/ControlChannel.h"
#include "vsthost/LegacyMessageCodec.h"
#include "vsthost/SharedRegion.h"
#include <charconv>
#include <functional>
#include <thread>
#include <mmsystem.h>

namespace lmms {
// Only used in the child process. Control replies can receive host callbacks
// from either the editor or audio thread.
class VstReplyCollector final : public RemotePluginBase::MessageTransport
{
public:
	std::function<bool(const RemotePluginBase::message&)> scanProgress;

	int send(const RemotePluginBase::message& message) override
	{
		if (message.id == IdVstShellScanProgress)
		{
			return scanProgress && scanProgress(message);
		}
		std::lock_guard lock(m_mutex);
		vsthost::LegacyCommand command{static_cast<std::uint32_t>(message.id), {}};
		if (messages.size() >= vsthost::MaxLegacyCommands || message.argumentCount() > vsthost::MaxLegacyArguments)
		{
			failed = true;
			return 0;
		}
		for (std::size_t i = 0; i < message.argumentCount(); ++i)
		{
			auto argument = message.getString(static_cast<int>(i));
			if (argument.size() > vsthost::MaxControlBytes - std::min(bytes, vsthost::MaxControlBytes))
			{
				failed = true;
				return 0;
			}
			bytes += static_cast<std::uint32_t>(argument.size());
			command.arguments.push_back(std::move(argument));
		}
		messages.push_back(std::move(command));
		return 1;
	}
	RemotePluginBase::message receive() override { return {}; }
	bool pending() const override { return false; }
	bool invalid() const override
	{
		std::lock_guard lock(m_mutex);
		return failed;
	}
	void invalidate() override
	{
		std::lock_guard lock(m_mutex);
		failed = true;
	}
	void clear()
	{
		std::lock_guard lock(m_mutex);
		messages.clear();
		bytes = 0;
		failed = false;
	}
	vsthost::Error encode(std::vector<std::uint8_t>& payload, std::uint32_t capacity) const
	{
		std::lock_guard lock(m_mutex);
		return failed ? vsthost::Error::InvalidMessage : vsthost::encodeLegacy(messages, payload, capacity);
	}

private:
	mutable std::mutex m_mutex;
	std::vector<vsthost::LegacyCommand> messages;
	std::uint32_t bytes = 0;
	bool failed = false;
};

inline bool validVstCommand(const vsthost::LegacyCommand& command, bool initialized)
{
	const auto count = command.arguments.size();
	const auto id = command.id;
	for (const auto& argument : command.arguments)
	{
		if (argument.find('\0') != std::string::npos)
		{
			return false;
		}
	}
	if (!initialized && id != IdVstLoadPlugin && id != IdVstScanPlugin && id != IdSyncKey
		&& id != IdSampleRateInformation && id != IdBufferSizeInformation && id != IdVstSetLanguage
		&& id != IdVstSetTempo)
	{
		return false;
	}
	switch (id)
	{
	case IdVstLoadPlugin: {
		if (initialized || (count != 1 && count != 2) || command.arguments[0].empty())
		{
			return false;
		}
		if (count == 1)
		{
			return true;
		}
		std::uint32_t shellId = 0;
		const auto& text = command.arguments[1];
		const auto parsed = std::from_chars(text.data(), text.data() + text.size(), shellId);
		return parsed.ec == std::errc{} && parsed.ptr == text.data() + text.size() && shellId != 0;
	}
	case IdVstScanPlugin:
		return !initialized && (count == 1 || (count == 2 && command.arguments[1] == "progress"))
			&& !command.arguments[0].empty();
	case IdSyncKey:
	case IdSampleRateInformation:
	case IdBufferSizeInformation:
	case IdVstSetLanguage:
	case IdVstSetTempo:
	case IdVstSetProgram:
	case IdVstRotateProgram:
	case IdVstUpdateParameterDisplay:
	case IdVstUpdateParameterLabel:
	case IdSaveSettingsToFile:
	case IdSavePresetFile:
	case IdLoadPresetFile:
		return count == 1;
	case IdLoadSettingsFromFile:
	case IdVstSetParameter:
		return count == 2;
	case IdVstGetParameterDump:
	case IdVstParameterAutomation:
	case IdVstMidiCapabilities:
	case IdVstCurrentProgram:
	case IdVstIdleUpdate:
	case IdVstProgramNames:
	case IdVstLoadAllParameterLabels:
	case IdVstLoadAllParameterDisplays:
	case IdShowUI:
	case IdHideUI:
	case IdToggleUI:
	case IdIsUIVisible:
		return count == 0;
	case IdVstSetParameterDump: {
		if (!count)
		{
			return false;
		}
		unsigned parameters = 0;
		const auto& text = command.arguments[0];
		const auto parsed = std::from_chars(text.data(), text.data() + text.size(), parameters);
		return parsed.ec == std::errc{} && parsed.ptr == text.data() + text.size()
			&& parameters <= (vsthost::MaxLegacyArguments - 1) / 3 && count == 1 + 3 * parameters;
	}
	default:
		return false;
	}
}

inline int runVstSession(int argc, char** argv, int position)
{
	using namespace vsthost;
	struct TimerResolution
	{
		bool active = timeBeginPeriod(1) == TIMERR_NOERROR;
		~TimerResolution()
		{
			if (active)
			{
				timeEndPeriod(1);
			}
		}
	} timerResolution;
	SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
	if (argc != position + 6)
	{
		return 2;
	}
	const auto session = std::stoull(argv[position + 3]);
	const auto generation = std::stoull(argv[position + 4]);
	const auto capacityValue = std::stoull(argv[position + 5]);
	if (capacityValue > MaxControlBytes)
	{
		return 2;
	}
	const auto capacity = static_cast<std::uint32_t>(capacityValue);
	SharedRegion controlRegion, audioRegion;
	if (!controlRegion.attach(toWString(argv[position + 1]).get(), ControlChannel::storageBytes(capacity))
		|| !audioRegion.attach(toWString(argv[position + 2]).get(), AudioQueue::StorageBytes))
	{
		return 3;
	}
	ControlChannel control(controlRegion.bytes(), ControlChannel::Side::Helper, capacity);
	AudioQueue audio(audioRegion.bytes());
	using Activity = AudioQueue::HelperActivity;
	audio.setHelperActivity(Activity::CheckingControl);
	auto collector = std::make_unique<VstReplyCollector>();
	auto* replies = collector.get();
	auto plugin = std::make_unique<RemoteVstPlugin>(std::move(collector));
	if (!RemoteVstPlugin::setupMessageWindow())
	{
		return 3;
	}
	std::vector<float> input(AudioQueue::MaxSamples), output(AudioQueue::MaxSamples);
	std::vector<float> planarInput(AudioQueue::MaxSamples), planarOutput(AudioQueue::MaxSamples);
	std::vector<std::uint8_t> events(AudioQueue::MaxEventBytes);
	std::array<std::uint8_t, AudioQueue::MaxOutputEventBytes> outputEvents{};
	bool paused = false;
	bool pauseRequested = false;
	auto process = [&] {
		audio.setHelperActivity(Activity::ClaimingAudio, true);
		auto layoutLock = plugin->lockSessionLayout();
		AudioQueue::Claim claim{};
		const auto result = audio.claim(claim, input, events);
		if (result != AudioQueue::Result::Ok)
		{
			return result;
		}
		if (!matches(claim.header, session, generation, claim.header.sequence) || !plugin->isInitialized()
			|| claim.layout.inputs != plugin->inputCount() || claim.layout.outputs != plugin->outputCount()
			|| claim.layout.frames != plugin->bufferSize() || claim.eventBytes % 20 != 0)
		{
			return AudioQueue::Result::Invalid;
		}
		audio.setHelperActivity(Activity::ProcessingAudio, true);
		for (std::uint32_t offset = 0; offset < claim.eventBytes; offset += 20)
		{
			const auto type = get(events, offset, 4), channel = get(events, offset + 4, 4);
			const auto first = get(events, offset + 8, 4), second = get(events, offset + 12, 4);
			const auto frame = get(events, offset + 16, 4);
			if (type == 256)
			{
				if (channel || frame
					|| !plugin->setRealtimeParameter(
						static_cast<unsigned>(first), std::bit_cast<float>(static_cast<std::uint32_t>(second))))
				{
					return AudioQueue::Result::Invalid;
				}
				continue;
			}
			if (type == 257)
			{
				if (channel || second || frame || first > 1000)
				{
					return AudioQueue::Result::Invalid;
				}
				plugin->setBPM(static_cast<bpm_t>(first));
				continue;
			}
			if (type > 255 || channel > 15 || first > 16383 || second > 127 || frame >= claim.layout.frames)
			{
				return AudioQueue::Result::Invalid;
			}
			plugin->processMidiEvent(MidiEvent(static_cast<MidiEventTypes>(type), static_cast<int>(channel),
										 static_cast<int>(first), static_cast<int>(second)),
				static_cast<f_cnt_t>(frame));
		}
		for (std::uint32_t frame = 0; frame < claim.layout.frames; ++frame)
		{
			for (std::uint32_t channel = 0; channel < claim.layout.inputs; ++channel)
			{
				planarInput[channel * claim.layout.frames + frame] = input[frame * claim.layout.inputs + channel];
			}
		}
		plugin->setShmIsValid(true);
		audio.setHelperActivity(Activity::PluginAudioCall, true);
		const auto generated = plugin->processWithMidiOutput(reinterpret_cast<const SampleFrame*>(planarInput.data()),
			reinterpret_cast<SampleFrame*>(planarOutput.data()),
			claim.collectOutput ? std::span<std::uint8_t>(outputEvents) : std::span<std::uint8_t>{});
		audio.setHelperActivity(Activity::CompletingAudio, true);
		for (std::uint32_t frame = 0; frame < claim.layout.frames; ++frame)
		{
			for (std::uint32_t channel = 0; channel < claim.layout.outputs; ++channel)
			{
				output[frame * claim.layout.outputs + channel] = planarOutput[channel * claim.layout.frames + frame];
			}
		}
		return audio.complete(claim, std::span(output).first(claim.layout.frames * claim.layout.outputs), generated);
	};
	// Keep the editor's Win32 message pump responsive to synchronous
	// messages from the plugin. Only host control transactions exclude DSP.
	std::mutex instanceMutex;
	std::atomic<bool> audioFailed{false};
	std::jthread audioThread([&](std::stop_token stop) {
		__processingThreadId = GetCurrentThreadId();
		while (!stop.stop_requested())
		{
			{
				audio.setHelperActivity(Activity::WaitingForInstance, true);
				std::lock_guard lock(instanceMutex);
				if (pauseRequested)
				{
					for (unsigned slot = 0; slot < AudioQueue::Slots; ++slot)
					{
						const auto drained = process();
						if (drained == AudioQueue::Result::Invalid)
						{
							audioFailed.store(true, std::memory_order_release);
							return;
						}
						if (drained != AudioQueue::Result::Ok)
						{
							break;
						}
					}
					paused = true;
					pauseRequested = false;
				}
				else if (!paused && plugin && plugin->isInitialized() && process() == AudioQueue::Result::Invalid)
				{
					audioFailed.store(true, std::memory_order_release);
					return;
				}
			}
			audio.setHelperActivity(Activity::Idle, true);
			Sleep(1);
		}
	});
	ControlChannel::Frame frame;
	bool controlPending = false;
	while (true)
	{
		if (audioFailed.load(std::memory_order_acquire))
		{
			return 8;
		}
		audio.setHelperActivity(Activity::CheckingControl);
		if (!controlPending)
		{
			const auto received = control.receive(frame);
			if (received != Error::None && received != Error::InvalidState)
			{
				return 4;
			}
			controlPending = received == Error::None;
			if (controlPending && !matches(frame.header, session, generation, frame.header.sequence))
			{
				return 5;
			}
		}
		std::unique_lock instanceLock(instanceMutex, std::defer_lock);
		if (controlPending && instanceLock.try_lock() && frame.header.type == MessageType::Pause && !paused)
		{
			pauseRequested = true;
			instanceLock.unlock();
		}
		if (instanceLock.owns_lock())
		{
			controlPending = false;
			audio.setHelperActivity(Activity::Control, false, static_cast<std::uint16_t>(frame.header.type));
			std::vector<std::uint8_t> payload;
			Error error = Error::None;
			const bool scan = frame.header.type == MessageType::Scan;
			const auto sendScanFrame = [&](Header header, std::span<const std::uint8_t> bytes) {
				// Scan requests remain under the host watchdog while this single-slot channel drains.
				Error result;
				do
				{
					result = control.send(header, bytes);
					if (result == Error::InvalidState)
					{
						Sleep(1);
					}
				} while (result == Error::InvalidState);
				return result;
			};
			if (frame.header.type == MessageType::Resume)
			{
				paused = false;
			}
			else if (frame.header.type == MessageType::Close)
			{
				plugin.reset();
				__plugin = nullptr;
			}
			else if (frame.header.type != MessageType::Hello && frame.header.type != MessageType::Pause)
			{
				std::vector<LegacyCommand> commands;
				error = decodeLegacy(frame.payload, commands, capacity);
				replies->clear();
				if (scan)
				{
					replies->scanProgress = [&](const RemotePluginBase::message& message) {
						const auto name = message.getString(2);
						std::vector<std::uint8_t> update(8 + name.size());
						put(update, 0, message.getInt(0), 4);
						put(update, 4, message.getInt(1), 4);
						std::copy(name.begin(), name.end(), update.begin() + 8);
						auto header = frame.header;
						header.type = MessageType::ScanProgress;
						header.payloadBytes = static_cast<std::uint32_t>(update.size());
						return sendScanFrame(header, update) == Error::None;
					};
				}
				if (error == Error::None)
				{
					for (const auto& command : commands)
					{
						if (!validVstCommand(command, plugin->isInitialized()))
						{
							error = Error::InvalidMessage;
							break;
						}
						RemotePluginBase::message message(static_cast<int>(command.id));
						for (const auto& argument : command.arguments)
						{
							message.addString(argument);
						}
						audio.setHelperActivity(Activity::Control, false, static_cast<std::uint16_t>(command.id));
						if (!plugin->processMessage(message))
						{
							error = Error::LoadFailed;
							break;
						}
						if (command.id == IdVstLoadPlugin && !plugin->isInitialized())
						{
							error = Error::LoadFailed;
							break;
						}
					}
				}
				if (error == Error::None)
				{
					error = replies->encode(payload, capacity);
				}
				replies->scanProgress = {};
			}
			const bool close = frame.header.type == MessageType::Close;
			if (scan)
			{
				frame.header.type = MessageType::ScanResult;
			}
			if (error != Error::None)
			{
				frame.header.type = MessageType::Fault;
				payload.resize(4);
				put(payload, 0, static_cast<unsigned>(error), 4);
			}
			frame.header.payloadBytes = static_cast<std::uint32_t>(payload.size());
			if ((scan ? sendScanFrame(frame.header, payload) : control.send(frame.header, payload)) != Error::None)
			{
				return 7;
			}
			if (close)
			{
				return 0;
			}
			instanceLock.unlock();
		}
		audio.setHelperActivity(Activity::PumpingMessages);
		MSG message;
		while (PeekMessage(&message, nullptr, 0, 0, PM_REMOVE))
		{
			if (message.message == WM_QUIT)
			{
				return 0;
			}
			audio.setHelperActivity(Activity::DispatchingMessage, false, static_cast<std::uint16_t>(message.message));
			TranslateMessage(&message);
			DispatchMessage(&message);
			audio.setHelperActivity(Activity::PumpingMessages);
		}
		audio.setHelperActivity(Activity::Idle);
		Sleep(1);
	}
}
} // namespace lmms
#endif
