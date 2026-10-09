#include "vsthost/Vst3HostProxy.h"
#include "vsthost/FixedQuantumBuffer.h"
#include "vsthost/Vst3Scanner.h"
#include <filesystem>
#include <iostream>
#include <cstdlib>
#include <new>
#include <thread>
#include <chrono>

using namespace lmms::vsthost;
#define CHECK(value) \
	do \
	{ \
		if (!(value)) \
		{ \
			std::cerr << "FAIL " << __LINE__ << ": " << #value << '\n'; \
			return 1; \
		} \
	} while (false)
namespace {
thread_local bool watchAllocations = false;
thread_local unsigned allocations = 0;
}
void* operator new(std::size_t size)
{
	if (watchAllocations)
	{
		++allocations;
	}
	if (auto* address = std::malloc(size ? size : 1))
	{
		return address;
	}
	throw std::bad_alloc{};
}
void operator delete(void* address) noexcept
{
	std::free(address);
}
void operator delete(void* address, std::size_t) noexcept
{
	std::free(address);
}

int wmain(int argc, wchar_t** argv)
{
	CHECK(argc == 3);
	const std::wstring helper = argv[1];
	const std::filesystem::path module = argv[2];
	const auto utf8 = module.u8string();
	const std::string path(reinterpret_cast<const char*>(utf8.data()), utf8.size());
	const auto scanned = scanVst3(helper, module.wstring());
	CHECK(scanned.error == Error::None && scanned.classes.size() == 3);
	for (unsigned index = 0; index < 2; ++index)
	{
		Vst3HostProxy proxy, peer;
		const Vst3Create create{scanned.classes[index].cid, 48000, 512, true, path};
		CHECK(SetEnvironmentVariableW(L"LMMS_VST3_FIXTURE_VERIFY_SETUP", L"1"));
		CHECK(proxy.open({helper, {}, 3000}, create));
		CHECK(SetEnvironmentVariableW(L"LMMS_VST3_FIXTURE_VERIFY_SETUP", nullptr));
		CHECK(peer.open({helper, {}, 3000}, create));
		CHECK(proxy.running() && peer.running() && proxy.pid() != peer.pid());
		CHECK(proxy.metadata().inputs == 2 && proxy.metadata().outputs == 2 && proxy.metadata().sampleSize == index);
		CHECK(proxy.metadata().parameters.size() == 2 && proxy.metadata().parameters[0].id == 0xf0000101);
		CHECK(proxy.metadata().buses.size() == 4 && proxy.metadata().buses[0].name == "Main"
			&& proxy.metadata().buses[2].name == "MIDI" && proxy.metadata().buses[2].channels == 16);

		// Caller-side port packing remains in the audio admission until unpacking
		// finishes; control publication closes admission without waiting in audio.
		std::atomic<bool> assemblyEntered{false}, assemblyFinished{false}, releaseAssembly{false};
		std::atomic<bool> published{false}, publicationAfterAssembly{false};
		bool admittedAssembly = false, refreshed = false;
		peer.setMetadataPublication([&] {
			publicationAfterAssembly.store(assemblyFinished.load());
			published.store(true);
		});
		std::thread assembly([&] {
			admittedAssembly = peer.withAudio([&] {
				assemblyEntered.store(true);
				const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
				while (!releaseAssembly.load() && std::chrono::steady_clock::now() < deadline)
				{
					Sleep(1);
				}
				assemblyFinished.store(true);
				return true;
			});
		});
		const auto enteredDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(1);
		while (!assemblyEntered.load() && std::chrono::steady_clock::now() < enteredDeadline)
		{
			Sleep(1);
		}
		std::thread control([&] { refreshed = peer.refreshMetadata(); });
		bool admissionClosed = false;
		const auto closedDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(1);
		while (std::chrono::steady_clock::now() < closedDeadline)
		{
			if (!peer.withAudio([] { return true; }))
			{
				admissionClosed = true;
				break;
			}
			Sleep(1);
		}
		const auto heldPublication = !published.load();
		releaseAssembly.store(true);
		assembly.join();
		control.join();
		CHECK(assemblyEntered.load() && admittedAssembly && admissionClosed && heldPublication && refreshed
			&& publicationAfterAssembly.load());
		peer.setMetadataPublication({});
		CHECK(GetModuleHandleW(L"Vst3Native.vst3") == nullptr);
		std::vector<float> input(128, 1), output(128, -1);
		Vst3Transport transport;
		transport.flags = 1;
		CHECK(proxy.postEvent({2, 23, 0, 3, 60, 0}));
		CHECK(proxy.postEvent({0, 7, 0, 0, 0xf0000101, 0.375}));
		CHECK(proxy.postEvent({1, 11, 0, 3, 60, 0.5}));
		CHECK(proxy.process({64, 2, 2}, input, output, transport, true));
		for (unsigned frame = 0; frame < 64; ++frame)
		{
			const float expected
				= (frame < 7 ? (index ? 0.75f : 0.25f) : 0.375f) + (frame >= 11 && frame < 23 ? 0.5f : 0);
			CHECK(output[frame * 2] == expected && output[frame * 2 + 1] == expected);
		}
		const auto dsp = proxy.poll();
		CHECK(dsp.error == Error::None && dsp.edits.size() == 1);
		CHECK(dsp.edits[0].phase == 3 && dsp.edits[0].id == 0xf0000101 && dsp.edits[0].offset == 63
			&& dsp.edits[0].value == 0.375);
		const auto midi = proxy.midiOutput();
		CHECK(midi.error == Error::None && !midi.payload.empty());
		const auto saved = proxy.state();
		CHECK(saved.error == Error::None && saved.payload.size() == 32);
		CHECK(peer.restoreState(saved.payload) == Error::None);
		CHECK(peer.process({64, 2, 2}, input, output, transport, true));
		CHECK(output[0] == 0.375f);
		CHECK(proxy.setParameter(0xf0000101, 0.5).error == Error::None);
		CHECK(proxy.postEvent({0, 5, 0, 0, 0xf0000101, 0.125}));
		CHECK(proxy.setParameter(0xf0000101, 0.5).error == Error::None);
		const auto updated = proxy.state();
		CHECK(updated.error == Error::None);
		CHECK(std::bit_cast<double>(get(updated.payload, 16, 8)) == 0.5
			&& std::bit_cast<double>(get(updated.payload, 24, 8)) == 0.5);
		const auto preset = proxy.preset();
		CHECK(preset.error == Error::None);
		CHECK(preset.payload.size() == 112 && !std::memcmp(preset.payload.data(), "VST3", 4));
		CHECK(peer.restorePreset(preset.payload) == Error::None);
		CHECK(peer.process({64, 2, 2}, input, output, transport, true) && output[0] == 0.5f);
		const auto snapshotPid = proxy.pid();
		CHECK(proxy.postEvent({0, 32, 0, 0, 0xf0000101, 0.625}));
		CHECK(proxy.postEvent({2, 40, 0, 3, 60, 0}));
		CHECK(proxy.postEvent({0, 7, 0, 0, 0xf0000101, 0.25}));
		CHECK(proxy.postEvent({1, 9, 0, 3, 60, 0.25}));
		CHECK(proxy.postEvent({0, 32, 0, 0, 0xf0000101, 0.875}));
		const auto pendingState = proxy.state();
		CHECK(pendingState.error == Error::None);
		CHECK(proxy.pid() == snapshotPid && proxy.running());
		CHECK(std::bit_cast<double>(get(pendingState.payload, 16, 8)) == 0.875);
		CHECK(std::bit_cast<double>(get(pendingState.payload, 24, 8)) == 0.875);
		CHECK(proxy.state().payload == pendingState.payload); // Repeated saves must preserve deferred MIDI.
		CHECK(peer.restoreState(pendingState.payload) == Error::None);
		CHECK(peer.process({64, 2, 2}, input, output, transport, true) && output[0] == 0.875f);
		CHECK(proxy.process({64, 2, 2}, input, output, transport, true));
		for (unsigned frame = 0; frame < 64; ++frame)
		{
			CHECK(output[frame * 2] == (frame >= 9 && frame < 40 ? 1.125f : 0.875f));
		}
		CHECK(proxy.poll().error == Error::None);
		CHECK(peer.postEvent({0, 0, 0, 0, 0xf0000101, 0.125}));
		CHECK(peer.postEvent({1, 3, 0, 2, 60, 0.5}));
		CHECK(peer.postEvent({2, 6, 0, 2, 60, 0}));
		CHECK(peer.restorePreset(preset.payload)
			== Error::None); // Older pending automation must not overwrite restored state.
		CHECK(peer.process({64, 2, 2}, input, output, transport, true));
		for (unsigned frame = 0; frame < 64; ++frame)
		{
			CHECK(output[frame * 2] == (frame >= 3 && frame < 6 ? 1.0f : 0.5f));
		}
		CHECK(proxy.setParameter(0xf0000101, 0.5).error == Error::None);
		auto wrongClass = preset.payload;
		wrongClass[8] = wrongClass[8] == 'F' ? '0' : 'F';
		CHECK(peer.restorePreset(wrongClass) == Error::InvalidMessage && peer.running());
		auto overlapping = preset.payload;
		put(overlapping, 96, 48, 8);
		CHECK(peer.restorePreset(overlapping) == Error::InvalidMessage && peer.running());
		unsigned restorePublications = 0;
		bool restorePublicationValid = true;
		peer.setMetadataPublication([&] {
			restorePublicationValid
				= !peer.process({64, 2, 2}, input, output, transport, false) && restorePublicationValid;
			++restorePublications;
		});
		CHECK(peer.restoreState(saved.payload) == Error::None);
		CHECK(restorePublicationValid && restorePublications == 1);
		peer.setMetadataPublication({});
		const auto shown = proxy.showEditor();
		CHECK(shown.error == Error::None && shown.payload.size() == 8);
		const auto window = reinterpret_cast<HWND>(static_cast<std::uintptr_t>(get(shown.payload, 0, 8)));
		DWORD owner = 0;
		GetWindowThreadProcessId(window, &owner);
		CHECK(owner == proxy.pid() && IsWindowVisible(window));
		const auto child = FindWindowExW(window, nullptr, L"STATIC", L"LMMS VST3 fixture editor");
		CHECK(child);
		DWORD_PTR answer = 0;
		CHECK(SendMessageTimeoutW(child, WM_APP + 38, 0, 0, SMTO_ABORTIFHUNG, 1500, &answer) && answer == 1);
		const auto gui = proxy.poll();
		CHECK(gui.error == Error::None && gui.edits.size() == 3);
		for (unsigned phase = 0; phase < 3; ++phase)
		{
			CHECK(gui.edits[phase].phase == phase && gui.edits[phase].id == 0xf0000101);
		}
		CHECK(gui.edits[1].value == 0.625);
		CHECK(proxy.hideEditor() == Error::None && !IsWindowVisible(window));
		unsigned restartPublications = 0;
		bool restartPublicationValid = true;
		const auto restartOwner = GetCurrentThreadId();
		proxy.setMetadataPublication([&] {
			restartPublicationValid
				= restartPublicationValid && GetCurrentThreadId() == restartOwner && proxy.metadata().latency == 17;
			restartPublicationValid
				= !proxy.process({64, 2, 2}, input, output, transport, false) && restartPublicationValid;
			++restartPublications;
		});
		CHECK(proxy.setParameter(42, 0.25).error == Error::None);
		CHECK(restartPublicationValid && restartPublications == 1 && proxy.processingLatency(true) == 17);
		const auto changed = proxy.poll();
		CHECK(changed.error == Error::None && changed.restart != 0);
		CHECK(restartPublicationValid && restartPublications == 2);
		proxy.setMetadataPublication({});
		CHECK(proxy.processingLatency(true) == 17 && proxy.processingLatency(false) == 1041);
		CHECK(proxy.metadata().latency == 17 && proxy.metadata().parameters[0].id == 42
			&& proxy.metadata().parameters[1].id == 0xf0000101);
		CHECK(proxy.setParameter(0xf0000101, 0.875).error == Error::None);
		CHECK(proxy.process({64, 2, 2}, input, output, transport, true));
		CHECK(output[0] == 0.875f);
		const auto configuredPid = proxy.pid();
		const auto publicationOwner = GetCurrentThreadId();
		unsigned publications = 0;
		bool publicationValid = true;
		proxy.setMetadataPublication([&] {
			publicationValid = publicationValid && GetCurrentThreadId() == publicationOwner;
			publicationValid = !proxy.process({64, 2, 2}, input, output, transport, false) && publicationValid;
			publicationValid = publicationValid
				&& std::all_of(output.begin(), output.end(), [](float sample) { return sample == 0; });
			++publications;
		});
		CHECK(proxy.configure({96000, 128, false}) == Error::None);
		CHECK(publications == 1 && proxy.bridgeLatency() == 256);
		CHECK(proxy.pid() == configuredPid && proxy.running());
		CHECK(proxy.process({64, 2, 2}, input, output, transport, true) && output[0] == 0.875f);
		CHECK(proxy.configure({44100, 256, true}) == Error::None);
		CHECK(publicationValid && publications == 2 && proxy.bridgeLatency() == 512);
		proxy.setMetadataPublication({});
		CHECK(proxy.pid() == configuredPid && proxy.running());
		CHECK(proxy.process({64, 2, 2}, input, output, transport, true) && output[0] == 0.875f);
		CHECK(proxy.configure({0, 256, true}) == Error::InvalidMessage && proxy.running());
		CHECK(proxy.configure({44100, 0, true}) == Error::InvalidMessage && proxy.running());
		CHECK(proxy.configure({44100, 4097, true}) == Error::InvalidMessage && proxy.running());
		const auto generation = proxy.generation();
		CHECK(proxy.restoreState({1, 2, 3}) == Error::InvalidMessage && proxy.running());
		CHECK(proxy.pid() == configuredPid && proxy.generation() == generation);
		CHECK(proxy.process({64, 2, 2}, input, output, transport, true) && output[0] == 0.875f);
		// A structurally valid envelope can still contain rejected native state.
		std::vector<std::uint8_t> invalidOpaque(16);
		put(invalidOpaque, 0, 1, 4);
		CHECK(proxy.restoreState(invalidOpaque) != Error::None);
		CHECK(!proxy.running());
		CHECK(peer.process({64, 2, 2}, input, output, transport, true));
		CHECK(output[0] == 0.375f);
		CHECK(proxy.open({helper, {}, 3000}, create));
		CHECK(proxy.generation() > generation);
		CHECK(proxy.close() == Error::None && peer.close() == Error::None);
		CHECK(!IsWindow(window));
		Vst3Create realtime = create;
		realtime.offline = false;
		realtime.maxFrames = 64;
		CHECK(proxy.open({helper, {}, 3000}, realtime));
		CHECK(proxy.postEvent({2, 8, 0, 3, 60, 0}));
		CHECK(proxy.postEvent({1, 5, 0, 3, 60, 0.25}));
		CHECK(proxy.state().error == Error::None); // Exercise deferred MIDI in allocation-watched realtime processing.
		watchAllocations = true;
		allocations = 0;
		const auto posted = proxy.postEvent({0, 0, 0, 0, 0xf0000101, 0.5});
		const auto first = proxy.withAudio([&] { return proxy.process({64, 2, 2}, input, output, transport, false); });
		watchAllocations = false;
		CHECK(posted && first && allocations == 0);
		CHECK(std::all_of(output.begin(), output.end(), [](auto sample) { return sample == 0; }));
		Sleep(40);
		CHECK(proxy.poll().error == Error::None); // GUI polling must preserve the pending audio sequence.
		watchAllocations = true;
		allocations = 0;
		const auto second = proxy.process({64, 2, 2}, input, output, transport, false);
		watchAllocations = false;
		CHECK(second && allocations == 0);
		CHECK(std::all_of(output.begin(), output.end(), [](auto sample) { return sample == 0; }));
		CHECK(proxy.bridgeLatency() == 128);
		Sleep(40);
		watchAllocations = true;
		allocations = 0;
		const auto third = proxy.process({64, 2, 2}, input, output, transport, false);
		watchAllocations = false;
		CHECK(third && allocations == 0 && output[0] == 0.5f);
		for (unsigned frame = 0; frame < 64; ++frame)
		{
			CHECK(output[frame * 2] == (frame >= 5 && frame < 8 ? 0.75f : 0.5f));
		}
		CHECK(proxy.close() == Error::None);
		// Exercise fixed quantum assembly against the real isolated helper. Callback
		// sizes vary, while the transport submitted to HostSession stays bounded.
		CHECK(proxy.open({helper, {}, 3000}, realtime));
		CHECK(proxy.bridgeLatency() == 128);
		CHECK(proxy.postEvent({0, 3, 0, 0, 0xf0000101, 0.625}));
		CHECK(proxy.process({17, 2, 2}, std::span(input).first(34), std::span(output).first(34), transport, false));
		const auto partialState = proxy.state();
		CHECK(partialState.error == Error::None);
		CHECK(std::bit_cast<double>(get(partialState.payload, 16, 8)) == 0.625);
		CHECK(std::bit_cast<double>(get(partialState.payload, 24, 8)) == 0.625);
		CHECK(proxy.postEvent({0, 2, 0, 0, 0xf0000101, 0.125}));
		CHECK(proxy.process({17, 2, 2}, std::span(input).first(34), std::span(output).first(34), transport, false));
		CHECK(proxy.restoreState(partialState.payload) == Error::None);
		CHECK(proxy.state().payload == partialState.payload); // Restore supersedes partial-quantum automation.
		CHECK(proxy.postEvent({0, 2, 0, 0, 0xf0000101, 0.125}));
		CHECK(proxy.process({17, 2, 2}, std::span(input).first(34), std::span(output).first(34), transport, false));
		CHECK(proxy.configure({48000, 64, false}) == Error::None); // Retain an edit in the deferred area.
		CHECK(proxy.setParameter(0xf0000101, 0.75).error == Error::None);
		const auto deferredSetter = proxy.state();
		CHECK(deferredSetter.error == Error::None);
		CHECK(std::bit_cast<double>(get(deferredSetter.payload, 16, 8)) == 0.75);
		CHECK(std::bit_cast<double>(get(deferredSetter.payload, 24, 8)) == 0.75);
		CHECK(proxy.postEvent({0, 40, 0, 0, 0xf0000101, 0.625}));
		CHECK(proxy.postEvent({0, 32, 0, 0, 0xf0000101, 0.125}));
		CHECK(proxy.process({17, 2, 2}, std::span(input).first(34), std::span(output).first(34), transport, false));
		CHECK(proxy.configure({48000, 16, false}) == Error::None);
		const auto futureState = proxy.state();
		CHECK(futureState.error == Error::None);
		CHECK(std::bit_cast<double>(get(futureState.payload, 16, 8)) == 0.625);
		CHECK(std::bit_cast<double>(get(futureState.payload, 24, 8)) == 0.625);
		CHECK(proxy.configure({48000, 64, false}) == Error::None);
		CHECK(proxy.setParameter(0xf0000101, 0.75).error == Error::None);
		CHECK(proxy.postEvent({1, 4, 0, 3, 60, 0.5}));
		CHECK(proxy.process({17, 2, 2}, std::span(input).first(34), std::span(output).first(34), transport, false));
		const auto midiBoundary = proxy.state();
		CHECK(midiBoundary.error == Error::None);
		CHECK(proxy.state().payload == midiBoundary.payload);
		CHECK(proxy.setParameter(0xf0000101, 0.75).error == Error::None); // Preserve deferred MIDI through a setter.
		for (unsigned block = 0; block < 3; ++block)
		{
			CHECK(proxy.process({64, 2, 2}, input, output, transport, false));
			Sleep(20);
		}
		for (auto sample : output)
		{
			CHECK(sample == 1.25f);
		} // Buffered MIDI rebased to zero and delivered after saving.
		CHECK(proxy.postEvent({2, 0, 0, 3, 60, 0}));
		for (unsigned block = 0; block < 3; ++block)
		{
			CHECK(proxy.process({64, 2, 2}, input, output, transport, false));
			Sleep(20);
		}
		for (auto sample : output)
		{
			CHECK(sample == 0.75f);
		}
		CHECK(proxy.postEvent({1, 4, 0, 3, 60, 0.5}));
		CHECK(proxy.postEvent({0, 3, 0, 0, 0xf0000101, 0.125}));
		CHECK(proxy.setParameter(0xf0000101, 0.5).error == Error::None);
		for (unsigned block = 0; block < 3; ++block)
		{
			CHECK(proxy.process({64, 2, 2}, input, output, transport, false));
			Sleep(20);
		}
		for (auto sample : output)
		{
			CHECK(sample == 1.0f);
		} // Setter filters the old edit but preserves queued MIDI.
		CHECK(proxy.close() == Error::None);
		realtime.maxFrames = 512;
		CHECK(proxy.open({helper, {}, 3000}, realtime));
		CHECK(proxy.bridgeLatency() == 1024);
		std::vector<float> stream(4096 * 2), rendered(stream.size());
		for (unsigned frame = 0; frame < 4096; ++frame)
		{
			stream[frame * 2] = float(frame + 1);
			stream[frame * 2 + 1] = -float(frame + 1);
		}
		constexpr std::array<unsigned, 6> callbackSizes{1, 17, 65, 511, 512, 7};
		unsigned cursor = 0, callback = 0;
		while (cursor < 4096)
		{
			const auto frames = std::min(callbackSizes[callback++ % callbackSizes.size()], 4096u - cursor);
			Vst3Transport position;
			position.flags = 1;
			position.samples = cursor;
			position.continuous = cursor;
			position.music = cursor * 120.0 / (60 * 48000);
			watchAllocations = true;
			allocations = 0;
			const bool processed = proxy.process({frames, 2, 2}, std::span(stream).subspan(cursor * 2, frames * 2),
				std::span(rendered).subspan(cursor * 2, frames * 2), position, false);
			watchAllocations = false;
			CHECK(processed && allocations == 0 && proxy.running());
			cursor += frames;
			// Timing/pacing here separates assembly correctness from the later
			// actual-device deadline acceptance gate.
			Sleep(20);
		}
		for (unsigned frame = 0; frame < 4096; ++frame)
		{
			const auto expected = frame < proxy.bridgeLatency()
				? 0.0f
				: stream[(frame - proxy.bridgeLatency()) * 2] * (index ? 0.75f : 0.25f);
			CHECK(rendered[frame * 2] == expected && rendered[frame * 2 + 1] == -expected);
		}
		CHECK(proxy.close() == Error::None);
		CHECK(proxy.open({helper, {}, 3000}, realtime));
		CHECK(proxy.configure({48000, 64, false}) == Error::None);
		CHECK(proxy.postEvent({1, 9, 0, 3, 60, 0.5}));
		CHECK(proxy.postEvent({2, 40, 0, 3, 60, 0}));
		CHECK(proxy.state().error == Error::None);
		std::vector<float> midiInput(512 * 2, 1), midiRendered(512 * 2);
		for (unsigned frame = 0; frame < 512;)
		{
			const auto frames = std::min(17u, 512u - frame);
			Vst3Transport position;
			position.flags = 1;
			position.samples = frame;
			position.continuous = frame;
			CHECK(proxy.process({frames, 2, 2}, std::span(midiInput).subspan(frame * 2, frames * 2),
				std::span(midiRendered).subspan(frame * 2, frames * 2), position, false));
			frame += frames;
			Sleep(10);
		}
		for (unsigned frame = 0; frame < 512; ++frame)
		{
			const auto expected
				= frame < 128 ? 0.0f : (index ? 0.75f : 0.25f) + (frame >= 128 + 9 && frame < 128 + 40 ? 0.5f : 0.0f);
			CHECK(midiRendered[frame * 2] == expected && midiRendered[frame * 2 + 1] == expected);
		}
		CHECK(proxy.close() == Error::None);
		CHECK(proxy.open({helper, {}, 3000}, realtime));
		const auto concurrentPid = proxy.pid();
		std::atomic<bool> stopAudio{false}, audioValid{true};
		std::atomic<unsigned> callbackCount{0}, mutedCallbacks{0};
		std::jthread audio([&] {
			std::array<float, 34> samples, result;
			samples.fill(1);
			Vst3Transport position;
			position.flags = 1;
			while (!stopAudio.load(std::memory_order_acquire))
			{
				watchAllocations = true;
				allocations = 0;
				const bool ok = proxy.process({17, 2, 2}, samples, result, position, false);
				watchAllocations = false;
				if (allocations
					|| (!ok
						&& (proxy.error() != Error::None
							|| !std::all_of(result.begin(), result.end(), [](auto value) { return value == 0; }))))
				{
					audioValid = false;
				}
				if (!ok)
				{
					++mutedCallbacks;
				}
				++callbackCount;
				position.samples += 17;
				position.continuous += 17;
				Sleep(2);
			}
		});
		bool controlsValid = true;
		for (unsigned operation = 0; operation < 40; ++operation)
		{
			controlsValid &= proxy.postEvent({0, 0, 0, 0, 0xf0000101, 0.125});
			controlsValid &= proxy.setParameter(0xf0000101, (operation & 1) ? 0.5 : 0.75).error == Error::None;
			controlsValid &= proxy.state().error == Error::None;
			controlsValid
				&= proxy.configure({(operation & 1) ? 44100.0 : 48000.0, (operation & 1) ? 256u : 512u, false})
				== Error::None;
			Sleep(2);
		}
		stopAudio.store(true, std::memory_order_release);
		audio.join();
		CHECK(controlsValid && audioValid && callbackCount > 0 && mutedCallbacks > 0);
		CHECK(proxy.running() && proxy.pid() == concurrentPid);
		CHECK(proxy.setParameter(0xf0000101, 0.875).error == Error::None);
		const auto concurrentState = proxy.state();
		CHECK(concurrentState.error == Error::None);
		CHECK(std::bit_cast<double>(get(concurrentState.payload, 16, 8)) == 0.875);
		CHECK(std::bit_cast<double>(get(concurrentState.payload, 24, 8)) == 0.875);
		CHECK(proxy.close() == Error::None);
		// A GUI I/O restart may arrive before the host polls. Old queued stereo
		// work must become silent without being interpreted using the new mono
		// layout; publication then precedes the first new mono audio block.
		for (unsigned restartCase = 0; restartCase < 7; ++restartCase)
		{
			const Vst3Create asyncCreate{scanned.classes[index].cid, 48000, 64, false, path};
			CHECK(proxy.open({helper, {}, 3000}, asyncCreate));
			const auto asyncPid = proxy.pid();
			const auto asyncShown = proxy.showEditor();
			CHECK(asyncShown.error == Error::None && asyncShown.payload.size() == 8);
			const auto asyncWindow = reinterpret_cast<HWND>(static_cast<std::uintptr_t>(get(asyncShown.payload, 0, 8)));
			const auto asyncChild = FindWindowExW(asyncWindow, nullptr, L"STATIC", L"LMMS VST3 fixture editor");
			CHECK(asyncChild);
			// Let initial native window creation/painting finish before the explicitly
			// paced audio oracle. This is not an actual-device deadline test.
			Sleep(100);
			const auto asyncInitialState = proxy.state();
			CHECK(asyncInitialState.error == Error::None);
			std::array<float, 128> stereoInput, stereoOutput;
			stereoInput.fill(1);
			for (unsigned block = 0; block < 3; ++block)
			{
				const bool processed = proxy.process({64, 2, 2}, stereoInput, stereoOutput, transport, false);
				if (!processed)
				{
					Sleep(100);
					std::cerr << "async warmup block=" << block << " error=" << unsigned(proxy.error())
							  << " stage=" << unsigned(proxy.fault().stage) << " native=" << proxy.fault().nativeCode
							  << '\n';
				}
				CHECK(processed);
				Sleep(20);
			}
			for (auto sample : stereoOutput)
			{
				CHECK(sample == (index ? 0.75f : 0.25f));
			}
			DWORD_PTR asyncAnswer = 0;
			CHECK(SendMessageTimeoutW(asyncChild, WM_APP + 39, 0, 0, SMTO_ABORTIFHUNG, 1500, &asyncAnswer)
				&& asyncAnswer == 1);
			Sleep(20);
			CHECK(proxy.postEvent({0, 7, 0, 0, 0xf0000101, 0.375}));
			CHECK(proxy.postEvent({1, 11, 0, 3, 60, 0.5}));
			for (unsigned block = 0; block < 4; ++block)
			{
				if (restartCase == 1 && block == 2)
				{
					CHECK(proxy.postEvent({2, 17, 0, 3, 60, 0}));
				}
				CHECK(proxy.process({64, 2, 2}, stereoInput, stereoOutput, transport, false));
				Sleep(20);
				CHECK(proxy.running() && proxy.pid() == asyncPid && proxy.metadata().inputs == 2
					&& proxy.metadata().outputs == 2);
			}
			for (auto sample : stereoOutput)
			{
				CHECK(sample == 0.f);
			}
			bool asyncPublished = false, asyncPublicationValid = true;
			proxy.setMetadataPublication([&] {
				asyncPublicationValid = proxy.metadata().inputs == 1 && proxy.metadata().outputs == 1;
				asyncPublicationValid = asyncPublicationValid && proxy.metadata().buses.size() == 4
					&& proxy.metadata().buses[0].channels == 1 && proxy.metadata().buses[1].channels == 1;

				asyncPublicationValid
					= !proxy.process({64, 2, 2}, stereoInput, stereoOutput, transport, false) && asyncPublicationValid;
				asyncPublished = true;
			});
			if (restartCase == 4)
			{
				const auto pendingSnapshot = proxy.state();
				CHECK(pendingSnapshot.error == Error::None && pendingSnapshot.payload.size() == 32);
				CHECK(std::bit_cast<double>(get(pendingSnapshot.payload, 16, 8)) == 0.375);
				CHECK(std::bit_cast<double>(get(pendingSnapshot.payload, 24, 8)) == 0.375);
			}
			else if (restartCase == 5)
			{
				CHECK(proxy.restoreState(asyncInitialState.payload) == Error::None);
			}
			else if (restartCase == 6)
			{
				CHECK(proxy.setParameter(0xf0000101, 0.625).error == Error::None);
			}
			else
			{
				const auto asyncFeedback = proxy.poll();
				CHECK(asyncFeedback.error == Error::None && (asyncFeedback.restart & 2) != 0);
			}
			CHECK(asyncPublished && asyncPublicationValid);
			proxy.setMetadataPublication({});
			if (restartCase == 2)
			{
				CHECK(proxy.setParameter(0xf0000101, 0.625).error == Error::None);
			}
			if (restartCase == 3)
			{
				CHECK(proxy.restoreState(asyncInitialState.payload) == Error::None);
			}
			const float asyncGain = (restartCase == 2 || restartCase == 6) ? 0.625f
				: (restartCase == 3 || restartCase == 5)				   ? (index ? 0.75f : 0.25f)
																		   : 0.375f;
			std::array<float, 64> monoInput, monoOutput;
			monoInput.fill(1);
			for (unsigned block = 0; block < 3; ++block)
			{
				CHECK(proxy.process({64, 1, 1}, monoInput, monoOutput, transport, false));
				Sleep(20);
			}
			for (auto sample : monoOutput)
			{
				CHECK(sample == asyncGain + (restartCase == 1 ? 0.f : 0.5f));
			}
			CHECK(proxy.postEvent({2, 0, 0, 3, 60, 0}));
			for (unsigned block = 0; block < 3; ++block)
			{
				CHECK(proxy.process({64, 1, 1}, monoInput, monoOutput, transport, false));
				Sleep(20);
			}
			for (auto sample : monoOutput)
			{
				CHECK(sample == asyncGain);
			}
			CHECK(proxy.running() && proxy.pid() == asyncPid && proxy.close() == Error::None);
		}
		// Real processor delay plus bridge delay must align the dry and wet
		// impulses, including silent tail blocks and callbacks smaller than Q.
		for (const bool delayed : {false, true})
		{
			for (const bool offline : {false, true})
			{
				for (const float wet : {0.0f, 0.5f, 1.0f})
				{
					CHECK(SetEnvironmentVariableW(L"LMMS_VST3_FIXTURE_AUDIO_LATENCY", delayed ? L"1" : nullptr));
					const Vst3Create mixCreate{scanned.classes[index].cid, 48000, 64, offline, path};
					CHECK(proxy.open({helper, {}, 3000}, mixCreate));
					CHECK(SetEnvironmentVariableW(L"LMMS_VST3_FIXTURE_AUDIO_LATENCY", nullptr));
					const unsigned latency = (offline ? 0 : 128) + (delayed ? 17 : 0);
					CHECK(proxy.processingLatency(offline) == latency
						&& proxy.processingLatency(true) == (delayed ? 17 : 0));
					std::array<float, 1024> impulse{}, mixed{};
					impulse[0] = 1;
					impulse[1] = -1;
					impulse[198] = -2;
					impulse[199] = 2;
					constexpr std::array<unsigned, 5> callbackSizes{1, 17, 63, 7, 64};
					unsigned cursor = 0, callback = 0;
					while (cursor < 512)
					{
						const auto count = std::min(callbackSizes[callback++ % callbackSizes.size()], 512 - cursor);
						const auto samples = std::span(impulse).subspan(cursor * 2, count * 2);
						transport.continuous = cursor;
						transport.samples = cursor;
						watchAllocations = !offline;
						allocations = 0;
						const auto processed = proxy.process({count, 2, 2}, samples,
							std::span(mixed).subspan(cursor * 2, count * 2), transport, offline, samples, wet, 1 - wet);
						watchAllocations = false;
						CHECK(processed && allocations == 0);
						cursor += count;
						if (!offline)
						{
							Sleep(20);
						}
					}
					const auto gain = wet * (index ? 0.75f : 0.25f) + 1 - wet;
					for (unsigned frame = 0; frame < 512; ++frame)
					{
						const auto expected = frame < latency ? 0 : impulse[(frame - latency) * 2] * gain;
						CHECK(mixed[frame * 2] == expected && mixed[frame * 2 + 1] == -expected);
					}
					CHECK(proxy.close() == Error::None);
				}
			}
		}
	}
	for (unsigned index = 0; index < 2; ++index)
	{
		CHECK(SetEnvironmentVariableW(L"LMMS_VST3_FIXTURE_DENSE_OUTPUT", L"1"));
		Vst3HostProxy routed;
		CHECK(routed.open({helper, {}, 3000}, {scanned.classes[index].cid, 48000, 64, false, path}));
		CHECK(SetEnvironmentVariableW(L"LMMS_VST3_FIXTURE_DENSE_OUTPUT", nullptr));
		struct Receipt
		{
			unsigned cursor = 0, frames = 0, count = 0;
			bool valid = true;
			std::array<unsigned, 8 * 512> seen{};
		} receipt;
		const auto receive = +[](void* context, const Vst3OutputEvent& event, const Vst3Transport& source,
								  unsigned offset) noexcept {
			auto& value = *static_cast<Receipt*>(context);
			const auto ordinal = event.event.channel * 128 + event.event.id;
			if (event.sequence >= 8 || ordinal >= 512 || offset >= value.frames)
			{
				value.valid = false;
				return false;
			}
			value.valid &= event.frames == 64 && event.event.type == 1 && event.event.bus == 0
				&& event.event.offset == ordinal % 64 && event.event.value == 0.5;
			value.valid &= source.continuous == 8000000000LL + event.sequence * 64
				&& source.samples == 4000000000LL + event.sequence * 64 && source.flags == 1 && source.tempo == 120;
			value.valid &= event.sequence * 64 + event.event.offset + 128 == value.cursor + offset;
			++value.seen[event.sequence * 512 + ordinal];
			++value.count;
			return value.valid;
		};
		std::array<float, 1024> input{}, output{};
		input.fill(1);
		constexpr std::array<unsigned, 5> sizes{1, 17, 63, 7, 64};
		unsigned callback = 0;
		while (receipt.cursor < 512)
		{
			receipt.frames = std::min(sizes[callback++ % sizes.size()], 512 - receipt.cursor);
			Vst3Transport source;
			source.flags = 1;
			source.continuous = 8000000000LL + receipt.cursor;
			source.samples = 4000000000LL + receipt.cursor;
			source.music = receipt.cursor * 120.0 / (60 * 48000);
			watchAllocations = true;
			allocations = 0;
			const auto processed = routed.process({receipt.frames, 2, 2},
				std::span(input).subspan(receipt.cursor * 2, receipt.frames * 2),
				std::span(output).subspan(receipt.cursor * 2, receipt.frames * 2), source, false, {}, 1, 0, nullptr,
				receive, &receipt);
			watchAllocations = false;
			CHECK(processed && allocations == 0 && receipt.valid);
			receipt.cursor += receipt.frames;
			Sleep(20);
		}
		CHECK(receipt.count == 6 * 512);
		for (unsigned block = 0; block < 8; ++block)
		{
			for (unsigned i = 0; i < 512; ++i)
			{
				CHECK(receipt.seen[block * 512 + i] == (block < 6 ? 1u : 0u));
			}
		}
		for (unsigned frame = 0; frame < 512; ++frame)
		{
			const auto expected = frame < 128 ? 0.0f : (index ? 0.75f : 0.25f);
			CHECK(output[frame * 2] == expected && output[frame * 2 + 1] == expected);
		}
		const auto later = routed.midiOutput();
		CHECK(later.error == Error::None && later.payload.size() == 8);
		CHECK(routed.close() == Error::None);
	}
	std::cout
		<< "PASS SDK-free DAW proxy: exact class/ParamID, metadata, sorted sample events, state, GUI, latency/title restart, isolation, reload and allocation-free realtime pipeline\n";
	return 0;
}
