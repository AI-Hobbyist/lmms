#include "vsthost/AudioQueue.h"
#include "vsthost/ControlChannel.h"
#include "vsthost/SharedRegion.h"
#include <string>
#include <vector>

using namespace lmms::vsthost;
int wmain(int argc, wchar_t** argv)
{
	SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
	std::wstring mode;
	int position = 1;
	if (argc > 3 && std::wstring(argv[1]) == L"--mode") { mode = argv[2]; position = 3; }
	if (argc != position + 6 || std::wstring(argv[position]) != L"--host-session") { return 2; }
	const auto session = std::stoull(argv[position + 3]);
	const auto generation = std::stoull(argv[position + 4]);
	const auto capacity = static_cast<std::uint32_t>(std::stoul(argv[position + 5]));
	SharedRegion controlRegion, audioRegion;
	if (!controlRegion.attach(argv[position + 1], ControlChannel::storageBytes(capacity)) ||
		!audioRegion.attach(argv[position + 2], AudioQueue::StorageBytes)) { return 3; }
	ControlChannel control(controlRegion.bytes(), ControlChannel::Side::Helper, capacity);
	AudioQueue audio(audioRegion.bytes());
	std::vector<float> input(AudioQueue::MaxSamples), output(AudioQueue::MaxSamples);
	std::vector<std::uint8_t> events(AudioQueue::MaxEventBytes);
	bool paused = false;
	auto process = [&]
	{
		AudioQueue::Claim claim{};
		const auto result = audio.claim(claim, input, events);
		if (result != AudioQueue::Result::Ok) { return result; }
		if (!matches(claim.header, session, generation, claim.header.sequence)) { return AudioQueue::Result::Invalid; }
		if (mode == L"hang-audio") { Sleep(INFINITE); }
		if (mode == L"slow-audio") { Sleep(30); }
		if (mode == L"crash-audio") { RaiseException(0xE0000042, EXCEPTION_NONCONTINUABLE, 0, nullptr); }
		const auto count = claim.layout.frames * claim.layout.outputs;
		for (std::uint32_t frame = 0; frame < claim.layout.frames; ++frame)
		{
			for (std::uint32_t channel = 0; channel < claim.layout.outputs; ++channel)
			{
				output[frame * claim.layout.outputs + channel] = channel < claim.layout.inputs ?
					input[frame * claim.layout.inputs + channel] * 0.25f : 0.0f;
			}
		}
		return audio.complete(claim, std::span(output).first(count));
	};
	while (true)
	{
		ControlChannel::Frame frame;
		const auto received = control.receive(frame);
		if (received != Error::None && received != Error::InvalidState) { return 4; }
		if (received == Error::None)
		{
			if (!matches(frame.header, session, generation, frame.header.sequence)) { return 5; }
			if (frame.header.type == MessageType::GetState && mode == L"hang-state") { Sleep(INFINITE); }
			if (frame.header.type == MessageType::GetState && mode == L"crash-state") { RaiseException(0xE0000042, EXCEPTION_NONCONTINUABLE, 0, nullptr); }
			if (frame.header.type == MessageType::Close && mode == L"hang-close") { Sleep(INFINITE); }
			if (frame.header.type == MessageType::Pause)
			{
				// Drain the already published audio before acknowledging the barrier.
				for (unsigned i = 0; i < AudioQueue::Slots; ++i)
				{ if (process() != AudioQueue::Result::Ok) { break; } }
				paused = true;
			}
			if (frame.header.type == MessageType::Resume) { paused = false; }
			if (mode == L"old-generation" && frame.header.type == MessageType::Hello)
			{
				auto old = frame.header; --old.generation;
				if (control.send(old, frame.payload) != Error::None) { return 6; }
				const auto deadline = GetTickCount64() + 2000;
				while (control.send(frame.header, frame.payload) == Error::InvalidState)
				{ if (GetTickCount64() >= deadline) { return 7; } Sleep(1); }
			}
			else
			{
				if (mode == L"wrong-sequence" && frame.header.type == MessageType::Hello) { ++frame.header.sequence; }
				if (control.send(frame.header, frame.payload) != Error::None) { return 8; }
			}
			if (frame.header.type == MessageType::Close) { return 0; }
		}
		if (!paused)
		{
			const auto result = process();
			if (result == AudioQueue::Result::Invalid) { return 9; }
		}
		Sleep(1);
	}
}
