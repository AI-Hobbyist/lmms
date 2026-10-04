#include "Vst3Module.h"
#include "Vst3Instance.h"
#include "vsthost/AudioQueue.h"
#include "vsthost/ControlChannel.h"
#include "vsthost/SharedRegion.h"
#include "vsthost/Vst3CatalogCodec.h"
#include "vsthost/Vst3Commands.h"
#include <charconv>
#include <memory>
#include <windows.h>

using namespace lmms::vsthost;
namespace
{
bool number(const wchar_t* text, std::uint64_t& value)
{
	std::string ascii;
	for (; *text; ++text) { if (*text < L'0' || *text > L'9') { return false; } ascii.push_back(static_cast<char>(*text)); }
	if (ascii.empty()) { return false; }
	const auto parsed = std::from_chars(ascii.data(), ascii.data() + ascii.size(), value);
	return parsed.ec == std::errc{} && parsed.ptr == ascii.data() + ascii.size();
}

Error scan(std::span<const std::uint8_t> payload, std::unique_ptr<Vst3Module>& module, std::vector<std::uint8_t>& reply)
{
	if (module || payload.empty() || payload.size() > 4 * 32767 ||
		std::find(payload.begin(), payload.end(), 0) != payload.end()) { return Error::InvalidMessage; }
	const auto size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
		reinterpret_cast<const char*>(payload.data()), static_cast<int>(payload.size()), nullptr, 0);
	if (size <= 0 || size > 32767) { return Error::InvalidMessage; }
	std::wstring path(size, L'\0');
	if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, reinterpret_cast<const char*>(payload.data()),
		static_cast<int>(payload.size()), path.data(), size) != size) { return Error::InvalidMessage; }
	module = std::make_unique<Vst3Module>();
	std::string error;
	if (!module->open(path, error)) { return Error::LoadFailed; }
	std::vector<Vst3Class> classes;
	for (const auto& info : module->classes())
	{
		Vst3Class entry;
		std::memcpy(entry.cid.data(), info.ID().data(), entry.cid.size());
		entry.flags = info.classFlags(); entry.category = info.category(); entry.name = info.name();
		entry.vendor = info.vendor(); entry.version = info.version(); entry.sdkVersion = info.sdkVersion();
		entry.subcategories = info.subCategoriesString(); classes.push_back(std::move(entry));
	}
	return encodeVst3Catalog(classes, reply) ? Error::None : Error::InvalidMessage;
}
}

int wmain(int argc, wchar_t** argv)
{
	SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
	SetUnhandledExceptionFilter([](EXCEPTION_POINTERS* exception) -> LONG
	{
		TerminateProcess(GetCurrentProcess(), exception->ExceptionRecord->ExceptionCode);
		return EXCEPTION_EXECUTE_HANDLER;
	});
	if (argc != 7 || std::wstring_view(argv[1]) != L"--host-session") { return 2; }
	std::uint64_t session = 0, generation = 0, capacity = 0;
	if (!number(argv[4], session) || !number(argv[5], generation) || !number(argv[6], capacity) ||
		!session || !generation || capacity > MaxControlBytes || !ControlChannel::storageBytes(static_cast<std::uint32_t>(capacity))) { return 2; }
	SharedRegion controlRegion, audioRegion;
	if (!controlRegion.attach(argv[2], ControlChannel::storageBytes(static_cast<std::uint32_t>(capacity))) ||
		!audioRegion.attach(argv[3], AudioQueue::StorageBytes)) { return 3; }
	ControlChannel channel(controlRegion.bytes(), ControlChannel::Side::Helper, static_cast<std::uint32_t>(capacity));
	AudioQueue audio(audioRegion.bytes());
	std::vector<float> input(AudioQueue::MaxSamples), output(AudioQueue::MaxSamples);
	std::vector<std::uint8_t> events(AudioQueue::MaxEventBytes);
	std::vector<std::uint8_t> outputEvents(AudioQueue::MaxOutputEventBytes);
	std::unique_ptr<Vst3Instance> instance;
	std::unique_ptr<Vst3Module> module;
	std::uint64_t sequence = 0;
	bool hello = false, paused = false;
	auto processAudio = [&]() -> AudioQueue::Result
	{
		AudioQueue::Claim claim{};
		const auto claimed = audio.claim(claim, input, events);
		if (claimed != AudioQueue::Result::Ok) { return claimed; }
		if (!instance || claim.header.session != session || claim.header.generation != generation ||
			claim.layout.inputs != instance->inputChannels() || claim.layout.outputs != instance->outputChannels())
		{ return AudioQueue::Result::Invalid; }
		const auto in = std::span(input).first(claim.layout.frames * claim.layout.inputs);
		const auto out = std::span(output).first(claim.layout.frames * claim.layout.outputs);
		// Keep the old queue layout until the host drains it and publishes the new
		// metadata. A native restart may already make further DSP calls unsafe.
		if (instance->restartPending())
		{
			if (!instance->deferRestartEvents(std::span(events).first(claim.eventBytes), claim.layout.frames))
			{ return AudioQueue::Result::Invalid; }
			std::fill(out.begin(), out.end(), 0.f);
		}
		else if (!instance->process(claim.layout.frames, in, out, std::span(events).first(claim.eventBytes), claim.header.sequence, claim.collectOutput))
		{ return AudioQueue::Result::Invalid; }
		std::uint32_t written = 0;
		if (claim.collectOutput && !instance->blockMidiOutput(outputEvents, written)) { return AudioQueue::Result::Invalid; }
		return audio.complete(claim, out, std::span(outputEvents).first(written));
	};
	for (;;)
	{
		ControlChannel::Frame frame;
		const auto received = channel.receive(frame);
		if (received == Error::InvalidState)
		{
			try { if (instance) { instance->serviceControl(false); } } catch (...) { return 9; }
			if (!paused && instance && processAudio() == AudioQueue::Result::Invalid) { return 8; }
			MSG message{};
			while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) { TranslateMessage(&message); DispatchMessageW(&message); }
			Sleep(1); continue;
		}
		if (received != Error::None) { return 4; }
		if (!matches(frame.header, session, generation, sequence + 1)) { return 5; }
		sequence = frame.header.sequence;
		std::vector<std::uint8_t> payload;
		Error error = Error::None;
		try
		{
			if (instance && frame.header.type != MessageType::Close) { instance->serviceControl(false); }
			if (!hello)
			{
				if (frame.header.type != MessageType::Hello || !frame.payload.empty()) { error = Error::InvalidMessage; }
				else { hello = true; }
			}
			else if (frame.header.type == MessageType::Scan && !instance) { error = scan(frame.payload, module, payload); }
			else if (frame.header.type == MessageType::Create && !instance && !module)
			{
				Vst3Create command;
				if (!decodeVst3Create(frame.payload, command)) { error = Error::InvalidMessage; }
				else
				{
					// Construct the native path from validated UTF-8, without ANSI conversion.
					const auto wideSize = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, command.path.data(), static_cast<int>(command.path.size()), nullptr, 0);
					if (wideSize <= 0 || wideSize > 32767) { error = Error::InvalidMessage; }
					else
					{
						std::wstring path(wideSize, L'\0');
						MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, command.path.data(), static_cast<int>(command.path.size()), path.data(), wideSize);
						instance = std::make_unique<Vst3Instance>(); instance->open(path, command.cid);
						instance->setup(command.sampleRate, command.maxFrames, command.offline);
						payload.resize(20 + instance->parameters().size() * 4);
						put(payload, 0, instance->inputChannels(), 4); put(payload, 4, instance->outputChannels(), 4);
						put(payload, 8, instance->sampleSize(), 4); put(payload, 12, instance->latency(), 4);
						put(payload, 16, instance->parameters().size(), 4);
						for (std::uint32_t index = 0; index < instance->parameters().size(); ++index)
						{ put(payload, 20 + index * 4, instance->parameters()[index].id, 4); }
					}
				}
			}
			else if (frame.header.type == MessageType::Pause && frame.payload.empty())
			{
				for (unsigned slot = 0; slot < AudioQueue::Slots; ++slot)
				{ if (processAudio() == AudioQueue::Result::Invalid) { error = Error::ProcessingFailed; break; } }
				paused = true;
				if (error == Error::None && instance) { instance->serviceControl(); }
			}
			else if (frame.header.type == MessageType::Resume && frame.payload.empty()) { paused = false; }
			else if (frame.header.type == MessageType::GetState && instance && (frame.payload.empty() || paused)) { payload = instance->state(frame.payload); }
			else if (frame.header.type == MessageType::SetState && instance) { instance->restoreState(frame.payload); }
			else if (frame.header.type == MessageType::Parameter && instance)
			{
				const bool metadata = (frame.payload.size() == 4 && get(frame.payload, 0, 4) == 1) ||
					(frame.payload.size() == 20 && get(frame.payload, 0, 4) == 2);
				if (metadata && !paused) { error = Error::InvalidState; }
				else
				{
					if (metadata) { instance->serviceControl(); }
					payload = instance->parameterControl(frame.payload);
				}
			}
			else if (frame.header.type == MessageType::Midi && frame.payload.empty() && instance) { payload = instance->midiOutput(); }
			else if (frame.header.type == MessageType::ShowEditor && frame.payload.empty() && instance)
			{ payload.resize(8); put(payload, 0, reinterpret_cast<std::uintptr_t>(instance->showEditor()), 8); }
			else if (frame.header.type == MessageType::HideEditor && frame.payload.empty() && instance) { instance->hideEditor(); }
			else if (frame.header.type == MessageType::Close && frame.payload.empty())
			{ if (instance) { instance->close(); instance.reset(); } module.reset(); }
			else { error = Error::InvalidMessage; }
		}
		catch (...) { error = Error::LoadFailed; }
		const auto type = error != Error::None ? MessageType::Fault :
			frame.header.type == MessageType::Scan ? MessageType::ScanResult : frame.header.type;
		if (error != Error::None) { payload.resize(4); put(payload, 0, static_cast<unsigned>(error), 4); }
		if (channel.send({type, session, generation, sequence, static_cast<std::uint32_t>(payload.size())}, payload) != Error::None) { return 6; }
		if (frame.header.type == MessageType::Close || error != Error::None) { return error == Error::None ? 0 : 7; }
	}
}
