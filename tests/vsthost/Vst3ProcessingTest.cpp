#include "vsthost/Vst3Scanner.h"
#include "vsthost/Vst3Commands.h"
#include "vsthost/Vst3BlockEvents.h"
#include "vsthost/Vst3OutputEvents.h"
#include "vsthost/Vst3ParameterFeedback.h"
#include "vsthost/Vst3Metadata.h"
#include <filesystem>
#include <iostream>

using namespace lmms::vsthost;
#define CHECK(condition) \
	do \
	{ \
		if (!(condition)) \
		{ \
			std::cerr << "FAIL " << __LINE__ << ": " << #condition << '\n'; \
			return 1; \
		} \
	} while (false)

int wmain(int argc, wchar_t** argv)
{
	if (argc != 3)
	{
		return 2;
	}
	const std::wstring helper = argv[1];
	const std::filesystem::path plugin = argv[2];
	const auto encodedPath = plugin.u8string();
	const std::string path(reinterpret_cast<const char*>(encodedPath.data()), encodedPath.size());
	const auto scanned = scanVst3(helper, plugin.wstring());
	CHECK(scanned.error == Error::None && scanned.classes.size() == 3);
	for (unsigned index = 0; index < 2; ++index)
	{
		HostSession session, peer;
		CHECK(session.open({helper, {}, 5000}).get().error == Error::None);
		CHECK(peer.open({helper, {}, 5000}).get().error == Error::None);
		CHECK(session.pid() != peer.pid());
		std::vector<std::uint8_t> command;
		CHECK(encodeVst3Create({scanned.classes[index].cid, 48000, 512, true, path}, command));
		const auto created = session.request(MessageType::Create, command, 5000).get();
		CHECK(created.error == Error::None);
		CHECK(created.payload.size() == 28);
		CHECK(get(created.payload, 0, 4) == 2 && get(created.payload, 4, 4) == 2);
		CHECK(get(created.payload, 8, 4) == index);
		CHECK(get(created.payload, 16, 4) == 2);
		CHECK(get(created.payload, 20, 4) == 0xf0000101 && get(created.payload, 24, 4) == 42);
		std::vector<std::uint8_t> metadataCommand(4);
		put(metadataCommand, 0, 1, 4);
		const auto initialMetadata = session.request(MessageType::Parameter, metadataCommand, 3000, true).get();
		Vst3Metadata metadata;
		CHECK(initialMetadata.error == Error::None && decodeVst3Metadata(initialMetadata.payload, metadata));
		CHECK(metadata.inputs == 2 && metadata.outputs == 2 && metadata.sampleSize == index && metadata.latency == 0);
		CHECK(metadata.hasEditor);
		CHECK(metadata.buses.size() == 4);
		for (unsigned busIndex = 0; busIndex < 4; ++busIndex)
		{
			const auto& bus = metadata.buses[busIndex];
			CHECK(bus.media == busIndex / 2 && bus.direction == busIndex % 2 && bus.index == 0 && bus.active);
			CHECK(bus.channelOffset == 0 && bus.type == 0 && bus.name == (busIndex < 2 ? "Main" : "MIDI"));
			CHECK(bus.channels == (busIndex < 2 ? 2u : 16u));
		}

		auto badMetadata = initialMetadata.payload;
		put(badMetadata, 24, 2, 4);
		Vst3Metadata rejectedMetadata;
		CHECK(!decodeVst3Metadata(badMetadata, rejectedMetadata));
		for (unsigned length = 0; length < 28; ++length)
		{
			CHECK(!decodeVst3Metadata(std::span(initialMetadata.payload).first(length), rejectedMetadata));
		}
		CHECK(metadata.parameters.size() == 2 && metadata.parameters[0].title == "Gain"
			&& metadata.parameters[0].id == 0xf0000101);
		CHECK(peer.request(MessageType::Create, command, 5000).get().error == Error::None);
		const auto state = session.request(MessageType::GetState, {}, 3000, true).get();
		CHECK(state.error == Error::None && state.payload.size() == 32);
		CHECK(get(state.payload, 4, 4) == 8 && get(state.payload, 8, 4) == 8 && get(state.payload, 12, 4) == 1);
		double componentGain = 0, controllerGain = 0;
		std::memcpy(&componentGain, state.payload.data() + 16, 8);
		std::memcpy(&controllerGain, state.payload.data() + 24, 8);
		CHECK(componentGain == (index ? 0.75 : 0.25) && controllerGain == componentGain);
		CHECK(peer.request(MessageType::SetState, state.payload, 3000, true).get().error == Error::None);
		CHECK(peer.request(MessageType::GetState, {}, 3000, true).get().payload == state.payload);
		for (const auto frames : {32u, 127u, 512u})
		{
			std::vector<float> input(frames * 2);
			for (unsigned sample = 0; sample < input.size(); ++sample)
			{
				input[sample] = static_cast<float>(static_cast<int>(sample % 13) - 6) / 8;
			}
			const auto rendered
				= session.renderOffline(
							 {frames, 2, 2}, input, [] { return std::span<const std::uint8_t>{}; }, 3000)
					  .get();
			CHECK(rendered.error == Error::None && rendered.payload.size() == input.size() * sizeof(float));
			std::vector<float> output(input.size());
			std::memcpy(output.data(), rendered.payload.data(), rendered.payload.size());
			for (unsigned sample = 0; sample < input.size(); ++sample)
			{
				CHECK(output[sample] == input[sample] * (index ? 0.75f : 0.25f));
			}
		}
		auto changedState = state.payload;
		const double changedGain = 0.5;
		std::memcpy(changedState.data() + 16, &changedGain, 8);
		std::memcpy(changedState.data() + 24, &changedGain, 8);
		CHECK(session.request(MessageType::SetState, changedState, 3000, true).get().error == Error::None);
		CHECK(session.request(MessageType::GetState, {}, 3000, true).get().payload == changedState);
		const auto changedAudio
			= session
				  .renderOffline(
					  {64, 2, 2}, std::vector<float>(128, 1), [] { return std::span<const std::uint8_t>{}; }, 3000)
				  .get();
		CHECK(changedAudio.error == Error::None && changedAudio.payload.size() == 512);
		float changedSample = 0;
		std::memcpy(&changedSample, changedAudio.payload.data(), 4);
		CHECK(changedSample == 0.5f);
		std::vector<std::uint8_t> parameter(12);
		put(parameter, 0, 0xf0000101, 4);
		put(parameter, 4, std::bit_cast<std::uint64_t>(0.375), 8);
		const auto setParameter = session.request(MessageType::Parameter, parameter, 3000).get();
		CHECK(setParameter.error == Error::None && setParameter.payload.size() == 12
			&& get(setParameter.payload, 4, 4) == 0);
		const auto savedParameter = session.request(MessageType::GetState, {}, 3000, true).get();
		CHECK(savedParameter.error == Error::None && savedParameter.payload.size() == 32);
		std::memcpy(&componentGain, savedParameter.payload.data() + 16, 8);
		std::memcpy(&controllerGain, savedParameter.payload.data() + 24, 8);
		CHECK(componentGain == 0.375 && controllerGain == 0.375);
		const auto polledParameter = session.request(MessageType::Parameter, {}, 3000).get();
		CHECK(polledParameter.error == Error::None && get(polledParameter.payload, 4, 4) == 0);
		// An unprocessed setter must not overwrite a later restored state.
		put(parameter, 4, std::bit_cast<std::uint64_t>(0.875), 8);
		CHECK(session.request(MessageType::Parameter, parameter, 3000).get().error == Error::None);
		CHECK(session.request(MessageType::SetState, changedState, 3000, true).get().error == Error::None);
		CHECK(session.request(MessageType::GetState, {}, 3000, true).get().payload == changedState);
		const auto shown = session.request(MessageType::ShowEditor, {}, 3000).get();
		CHECK(shown.error == Error::None && shown.payload.size() == 8);
		const auto editorWindow = reinterpret_cast<HWND>(static_cast<std::uintptr_t>(get(shown.payload, 0, 8)));
		CHECK(IsWindow(editorWindow) && IsWindowVisible(editorWindow));
		DWORD editorPid = 0;
		GetWindowThreadProcessId(editorWindow, &editorPid);
		CHECK(editorPid == session.pid());
		const auto editorChild = FindWindowExW(editorWindow, nullptr, L"STATIC", L"LMMS VST3 fixture editor");
		CHECK(editorChild != nullptr);
		DWORD_PTR editorResult = 0;
		CHECK(SendMessageTimeoutW(editorChild, WM_APP + 38, 0, 0, SMTO_ABORTIFHUNG, 1500, &editorResult)
			&& editorResult == 1);
		RECT editorRectangle{};
		CHECK(GetClientRect(editorWindow, &editorRectangle));
		CHECK(editorRectangle.right == 317 && editorRectangle.bottom == 173);
		const auto edits = session.request(MessageType::Parameter, {}, 3000).get();
		CHECK(edits.error == Error::None && edits.payload.size() == 108 && get(edits.payload, 4, 4) == 3);
		for (unsigned phase = 0; phase < 3; ++phase)
		{
			CHECK(get(edits.payload, 12 + phase * 32, 4) == phase
				&& get(edits.payload, 16 + phase * 32, 4) == 0xf0000101);
		}
		CHECK(std::bit_cast<double>(get(edits.payload, 52, 8)) == 0.625);
		const auto guiAudio
			= session
				  .renderOffline(
					  {64, 2, 2}, std::vector<float>(128, 1), [] { return std::span<const std::uint8_t>{}; }, 3000)
				  .get();
		CHECK(guiAudio.error == Error::None && guiAudio.payload.size() == 512);
		float guiSample = 0;
		std::memcpy(&guiSample, guiAudio.payload.data(), 4);
		CHECK(guiSample == 0.625f);
		CHECK(session.request(MessageType::HideEditor, {}, 3000).get().error == Error::None
			&& !IsWindowVisible(editorWindow));
		CHECK(session.request(MessageType::ShowEditor, {}, 3000).get().error == Error::None
			&& IsWindowVisible(editorWindow));
		const std::array<Vst3BlockEvent, 4> blockEvents{
			{{0, 7, 0, 0, 0xf0000101, 0.25}, {1, 11, 0, 3, 60, 0.5}, {3, 17, 0, 3, 60, 0.375}, {2, 23, 0, 3, 60, 0}}};
		std::array<std::uint8_t, 136> eventStorage{};
		const auto eventPacket = encodeVst3Events(blockEvents, eventStorage, 64);
		CHECK(eventPacket.size() == eventStorage.size());
		for (std::size_t length = 1; length < eventPacket.size(); ++length)
		{
			CHECK(!decodeVst3Events(eventPacket.first(length), 64, [](const auto&) { return true; }));
		}
		auto malformedEvents = eventStorage;
		put(malformedEvents, 8 + 32 + 4, 64, 4);
		unsigned malformedMutations = 0;
		CHECK(!decodeVst3Events(malformedEvents, 64, [&](const auto&) {
			++malformedMutations;
			return true;
		}));
		CHECK(malformedMutations == 0);
		const auto eventAudio
			= session.renderOffline({64, 2, 2}, std::vector<float>(128, 1), [&] { return eventPacket; }, 3000).get();
		CHECK(eventAudio.error == Error::None && eventAudio.payload.size() == 512);
		std::array<float, 128> eventSamples{};
		std::memcpy(eventSamples.data(), eventAudio.payload.data(), 512);
		for (unsigned frame = 0; frame < 64; ++frame)
		{
			const float expected = frame < 7 ? 0.625f
				: frame < 11				 ? 0.25f
				: frame < 17				 ? 0.75f
				: frame < 23				 ? 0.625f
											 : 0.25f;
			CHECK(eventSamples[frame * 2] == expected && eventSamples[frame * 2 + 1] == expected);
		}
		const auto midi = session.request(MessageType::Midi, {}, 3000).get();
		CHECK(midi.error == Error::None && midi.payload.size() == 152);
		unsigned midiIndex = 1;
		CHECK(decodeVst3OutputEvents(midi.payload, [&](const Vst3OutputEvent& output) {
			if (output.sequence != 5 || output.frames != 64)
			{
				return false;
			}
			const auto& event = output.event;
			if (midiIndex >= 4)
			{
				return false;
			}
			const auto& expected = blockEvents[midiIndex++];
			return event.type == expected.type && event.offset == expected.offset && event.id == expected.id
				&& event.channel == 3 && event.value == expected.value;
		}));
		CHECK(midiIndex == 4);
		const auto dspFeedback = session.request(MessageType::Parameter, {}, 3000).get();
		CHECK(dspFeedback.error == Error::None && dspFeedback.payload.size() == 44);
		std::uint32_t restart = 0;
		CHECK(decodeVst3Feedback(dspFeedback.payload, restart, [](const Vst3ParameterFeedback& edit) {
			return edit.phase == 3 && edit.id == 0xf0000101 && edit.value == 0.25 && edit.frames == 64
				&& edit.offset == 63 && edit.sequence == 5;
		}));
		const auto automatedState = session.request(MessageType::GetState, {}, 3000, true).get();
		CHECK(automatedState.error == Error::None && automatedState.payload.size() == 32);
		std::memcpy(&componentGain, automatedState.payload.data() + 16, 8);
		std::memcpy(&controllerGain, automatedState.payload.data() + 24, 8);
		CHECK(componentGain == 0.25 && controllerGain == componentGain);
		CHECK(peer.request(MessageType::SetState, automatedState.payload, 3000, true).get().error == Error::None);
		CHECK(peer.request(MessageType::GetState, {}, 3000, true).get().payload == automatedState.payload);
		const Vst3Transport transport{7, 3, 8, 137.5, 123456, 17.25, 16.5, 16, 20, 234567};
		std::array<std::uint8_t, 80> contextStorage{};
		const auto contextPacket = encodeVst3Events({}, contextStorage, 64, &transport);
		CHECK(contextPacket.size() == 80);
		for (std::size_t length = 1; length < 80; ++length)
		{
			CHECK(!decodeVst3Events(contextPacket.first(length), 64, [](const auto&) { return true; }));
		}
		CHECK(session.renderOffline(
						 {64, 2, 2}, std::vector<float>(128, 1), [&] { return contextPacket; }, 3000)
				  .get()
				  .error
			== Error::None);
		CHECK(session
				  .renderOffline(
					  {64, 2, 2}, std::vector<float>(128, 1), [] { return std::span<const std::uint8_t>{}; }, 3000)
				  .get()
				  .error
			== Error::None);
		const std::array<Vst3BlockEvent, 4> midiControllers{
			{{4, 5, 0, 3, 7, 0.125}, {4, 9, 0, 4, 7, 1}, {4, 13, 0, 3, 128, 0.375}, {4, 21, 0, 3, 129, 0.875}}};
		const auto controllerPacket = encodeVst3Events(midiControllers, eventStorage, 64);
		CHECK(controllerPacket.size() == 136);
		const auto controllerAudio
			= session.renderOffline(
						 {64, 2, 2}, std::vector<float>(128, 1), [&] { return controllerPacket; }, 3000)
				  .get();
		CHECK(controllerAudio.error == Error::None && controllerAudio.payload.size() == 512);
		std::memcpy(eventSamples.data(), controllerAudio.payload.data(), 512);
		for (unsigned frame = 0; frame < 64; ++frame)
		{
			const float expected = frame < 5 ? 0.25f : frame < 13 ? 0.125f : frame < 21 ? 0.375f : 0.875f;
			CHECK(eventSamples[frame * 2] == expected && eventSamples[frame * 2 + 1] == expected);
		}
		const auto controllerState = session.request(MessageType::GetState, {}, 3000, true).get();
		CHECK(controllerState.error == Error::None && controllerState.payload.size() == 32);
		std::memcpy(&componentGain, controllerState.payload.data() + 16, 8);
		std::memcpy(&controllerGain, controllerState.payload.data() + 24, 8);
		CHECK(componentGain == 0.875 && controllerGain == componentGain);
		// Simulate MIDI learn: assignment 7 is removed and 10 takes its place.
		put(parameter, 0, 42, 4);
		put(parameter, 4, std::bit_cast<std::uint64_t>(1.0), 8);
		CHECK(session.request(MessageType::Parameter, parameter, 3000).get().error == Error::None);
		const auto learned = session.request(MessageType::Parameter, {}, 3000).get();
		CHECK(learned.error == Error::None && (get(learned.payload, 8, 4) & 32));
		const std::array<Vst3BlockEvent, 2> learnedControllers{{{4, 2, 0, 3, 7, 0.25}, {4, 7, 0, 3, 10, 0.5}}};
		const auto learnedPacket = encodeVst3Events(learnedControllers, eventStorage, 64);
		const auto learnedAudio
			= session.renderOffline({64, 2, 2}, std::vector<float>(128, 1), [&] { return learnedPacket; }, 3000).get();
		CHECK(learnedAudio.error == Error::None && learnedAudio.payload.size() == 512);
		std::memcpy(eventSamples.data(), learnedAudio.payload.data(), 512);
		for (unsigned frame = 0; frame < 64; ++frame)
		{
			CHECK(eventSamples[frame * 2] == (frame < 7 ? 0.875f : 0.5f));
		}
		put(parameter, 4, std::bit_cast<std::uint64_t>(0.25), 8);
		CHECK(session.request(MessageType::Parameter, parameter, 3000).get().error == Error::None);
		const auto refreshed = session.request(MessageType::Parameter, metadataCommand, 3000, true).get();
		CHECK(refreshed.error == Error::None && decodeVst3Metadata(refreshed.payload, metadata));
		CHECK(metadata.latency == 17 && metadata.parameters.size() == 2);
		CHECK(metadata.parameters[0].id == 42 && metadata.parameters[1].id == 0xf0000101);
		CHECK(metadata.parameters[1].title == "Program Gain" && metadata.parameters[1].steps == 3
			&& metadata.parameters[1].value == 0.3125 && metadata.parameters[1].defaultValue == 0.625);
		for (std::size_t length = 0; length < refreshed.payload.size(); ++length)
		{
			CHECK(!decodeVst3Metadata(std::span(refreshed.payload).first(length), metadata));
			CHECK(metadata.parameters.empty());
		}
		const auto refreshedAudio
			= session
				  .renderOffline(
					  {64, 2, 2}, std::vector<float>(128, 1), [] { return std::span<const std::uint8_t>{}; }, 3000)
				  .get();
		CHECK(refreshedAudio.error == Error::None && refreshedAudio.payload.size() == 512);
		std::memcpy(eventSamples.data(), refreshedAudio.payload.data(), 512);
		for (const auto sample : eventSamples)
		{
			CHECK(sample == 0.3125f);
		}
		// Stop the explicit transport before testing a fresh processor lifecycle.
		const Vst3Transport stopped{};
		const auto stoppedPacket = encodeVst3Events({}, contextStorage, 64, &stopped);
		CHECK(session.renderOffline(
						 {64, 2, 2}, std::vector<float>(128, 1), [&] { return stoppedPacket; }, 3000)
				  .get()
				  .error
			== Error::None);
		put(parameter, 4, std::bit_cast<std::uint64_t>(0.125), 8);
		CHECK(session.request(MessageType::Parameter, parameter, 3000).get().error == Error::None);
		const auto monoMetadata = session.request(MessageType::Parameter, metadataCommand, 3000, true).get();
		CHECK(monoMetadata.error == Error::None && decodeVst3Metadata(monoMetadata.payload, metadata));
		CHECK(metadata.inputs == 1 && metadata.outputs == 1);
		const auto mono
			= session
				  .renderOffline(
					  {64, 1, 1}, std::vector<float>(64, 1), [] { return std::span<const std::uint8_t>{}; }, 3000)
				  .get();
		CHECK(mono.error == Error::None && mono.payload.size() == 256);
		std::array<float, 64> monoSamples{};
		std::memcpy(monoSamples.data(), mono.payload.data(), 256);
		for (const auto sample : monoSamples)
		{
			CHECK(sample == 0.3125f);
		}
		const auto beforeReload = session.request(MessageType::GetState, {}, 3000, true).get();
		CHECK(beforeReload.error == Error::None);
		const auto originalPid = session.pid();
		put(parameter, 4, std::bit_cast<std::uint64_t>(0.0625), 8);
		CHECK(session.request(MessageType::Parameter, parameter, 3000).get().error == Error::None);
		const auto reloadMetadata = session.request(MessageType::Parameter, metadataCommand, 3000, true).get();
		CHECK(reloadMetadata.error == Error::None && decodeVst3Metadata(reloadMetadata.payload, metadata));
		CHECK(metadata.inputs == 2 && metadata.outputs == 2 && metadata.latency == 0
			&& metadata.parameters[0].title == "Gain");
		CHECK(session.pid() == originalPid); // DLL reload, same supervised helper.
		CHECK(session.request(MessageType::GetState, {}, 3000, true).get().payload == beforeReload.payload);
		const auto reopenedEditor = session.request(MessageType::ShowEditor, {}, 3000).get();
		CHECK(reopenedEditor.error == Error::None && reopenedEditor.payload.size() == 8);
		const auto newWindow = reinterpret_cast<HWND>(static_cast<std::uintptr_t>(get(reopenedEditor.payload, 0, 8)));
		CHECK(IsWindowVisible(newWindow));
		const auto newChild = FindWindowExW(newWindow, nullptr, L"STATIC", L"LMMS VST3 fixture editor");
		CHECK(newChild != nullptr);
		const auto reloaded
			= session
				  .renderOffline(
					  {64, 2, 2}, std::vector<float>(128, 1), [] { return std::span<const std::uint8_t>{}; }, 3000)
				  .get();
		CHECK(reloaded.error == Error::None && reloaded.payload.size() == 512);
		std::memcpy(eventSamples.data(), reloaded.payload.data(), 512);
		for (const auto sample : eventSamples)
		{
			CHECK(sample == 0.3125f);
		}
		CHECK(session.close().get().error == Error::None);
		CHECK(!IsWindow(newWindow) && !IsWindow(newChild));
		CHECK(!IsWindow(editorWindow) && !IsWindow(editorChild));
		const std::array<Vst3BlockEvent, 2> peerNotes{{{1, 1, 0, 3, 60, 0.5}, {2, 31, 0, 3, 60, 0}}};
		const auto peerPacket = encodeVst3Events(peerNotes, eventStorage, 32);
		CHECK(peer.renderOffline(
					  {32, 2, 2}, std::vector<float>(64, 1), [&] { return peerPacket; }, 3000)
				  .get()
				  .error
			== Error::None);
		CHECK(peer.renderOffline(
					  {127, 2, 2}, std::vector<float>(254, 1), [&] { return peerPacket; }, 3000)
				  .get()
				  .error
			== Error::None);
		const auto peerMidi = peer.request(MessageType::Midi, {}, 3000).get();
		CHECK(peerMidi.error == Error::None && peerMidi.payload.size() == 200);
		unsigned peerIndex = 0;
		CHECK(decodeVst3OutputEvents(peerMidi.payload, [&](const Vst3OutputEvent& output) {
			if (peerIndex >= 4 || output.sequence != peerIndex / 2 || output.frames != (peerIndex < 2 ? 32u : 127u))
			{
				return false;
			}
			const auto& expected = peerNotes[peerIndex++ % 2];
			return output.event.type == expected.type && output.event.offset == expected.offset
				&& output.event.value == expected.value;
		}));
		CHECK(peerIndex == 4);
		const auto peerFeedback = peer.request(MessageType::Parameter, {}, 3000).get();
		CHECK(peerFeedback.error == Error::None && peerFeedback.payload.size() == 76);
		unsigned feedbackIndex = 0;
		CHECK(decodeVst3Feedback(peerFeedback.payload, restart, [&](const Vst3ParameterFeedback& edit) {
			if (feedbackIndex >= 2 || edit.phase != 3 || edit.sequence != feedbackIndex
				|| edit.frames != (feedbackIndex == 0 ? 32u : 127u))
			{
				return false;
			}
			++feedbackIndex;
			return edit.offset == edit.frames - 1 && edit.id == 0xf0000101 && edit.value == 0.25;
		}));
		CHECK(feedbackIndex == 2);
		for (std::size_t length = 0; length < peerFeedback.payload.size(); ++length)
		{
			CHECK(!decodeVst3Feedback(
				std::span(peerFeedback.payload).first(length), restart, [](const auto&) { return true; }));
		}
		for (std::size_t length = 0; length < peerMidi.payload.size(); ++length)
		{
			CHECK(!decodeVst3OutputEvents(std::span(peerMidi.payload).first(length), [](const auto&) { return true; }));
		}
		auto badOutput = peerMidi.payload;
		put(badOutput, 8 + 3 * 48 + 8, 0, 4);
		unsigned badOutputMutations = 0;
		CHECK(!decodeVst3OutputEvents(badOutput, [&](const auto&) {
			++badOutputMutations;
			return true;
		}));
		CHECK(badOutputMutations == 0);
		CHECK(peer.close().get().error == Error::None);
		SetEnvironmentVariableW(L"LMMS_VST3_FIXTURE_DENSE_OUTPUT", L"1");
		HostSession dense;
		CHECK(dense.open({helper, {}, 5000}).get().error == Error::None);
		std::vector<std::uint8_t> denseCreate;
		CHECK(encodeVst3Create({scanned.classes[index].cid, 48000, 512, true, path}, denseCreate));
		CHECK(dense.request(MessageType::Create, denseCreate, 5000).get().error == Error::None);
		SetEnvironmentVariableW(L"LMMS_VST3_FIXTURE_DENSE_OUTPUT", nullptr);
		// Leave a full batch for the control consumer before enabling collection.
		CHECK(dense
				  .renderOffline(
					  {64, 2, 2}, std::vector<float>(128, 1), [] { return std::span<const std::uint8_t>{}; }, 3000)
				  .get()
				  .error
			== Error::None);
		for (unsigned block = 0; block < 48; ++block)
		{
			const unsigned frames = block % 2 ? 127 : 512;
			const auto rendered = dense
									  .renderOffline(
										  {frames, 2, 2}, std::vector<float>(frames * 2, 1),
										  [] { return std::span<const std::uint8_t>{}; }, 3000, MessageType::Midi)
									  .get();
			CHECK(rendered.error == Error::None && rendered.followupPayload.size() == 8 + 512 * 48);
			unsigned count = 0;
			CHECK(decodeVst3OutputEvents(rendered.followupPayload, [&](const Vst3OutputEvent& event) {
				const auto ordinal = count++;
				return event.sequence == block + 1 && event.frames == frames && event.event.type == 1
					&& event.event.offset == ordinal % frames && event.event.bus == 0
					&& event.event.channel == ordinal / 128 && event.event.id == ordinal % 128
					&& event.event.value == 0.5;
			}));
			CHECK(count == 512);
			const auto later = dense.request(MessageType::Midi, {}, 3000).get();
			CHECK(later.error == Error::None && later.payload.size() == (block ? 8u : 8u + 512u * 48u));
			if (!block)
			{
				unsigned oldCount = 0;
				CHECK(decodeVst3OutputEvents(later.payload, [&](const Vst3OutputEvent& event) {
					const auto ordinal = oldCount++;
					return event.sequence == 0 && event.frames == 64 && event.event.offset == ordinal % 64
						&& event.event.channel == ordinal / 128 && event.event.id == ordinal % 128;
				}));
				CHECK(oldCount == 512);
				CHECK(dense.request(MessageType::Midi, {}, 3000).get().payload.size() == 8);
			}
		}
		CHECK(dense.close().get().error == Error::None);
		SetEnvironmentVariableW(L"LMMS_VST3_FIXTURE_DENSE_OUTPUT", L"1");
		CHECK(dense.open({helper, {}, 5000}).get().error == Error::None);
		CHECK(encodeVst3Create({scanned.classes[index].cid, 48000, 512, false, path}, denseCreate));
		CHECK(dense.request(MessageType::Create, denseCreate, 5000).get().error == Error::None);
		SetEnvironmentVariableW(L"LMMS_VST3_FIXTURE_DENSE_OUTPUT", nullptr);
		// Reuse caller storage while receiving the preceding audio quantum.
		std::array<float, 1024> realtimeInput{}, realtimeOutput{};
		realtimeInput.fill(1);
		std::array<std::uint8_t, AudioQueue::MaxOutputEventBytes> realtimeFeedback{};
		for (unsigned block = 0; block <= 48; ++block)
		{
			std::uint32_t bytes = 0;
			CHECK(dense.processPrepared(
				{512, 2, 2}, realtimeInput, realtimeOutput, [] { return std::span<const std::uint8_t>{}; },
				realtimeFeedback, block ? &bytes : nullptr));
			if (!block)
			{
				CHECK(bytes == 0);
				for (const auto sample : realtimeOutput)
				{
					CHECK(sample == 0);
				}
			}
			else if (block == 1)
			{
				// First result was submitted without collection. Its retained
				// control batch must not consume the next collecting slot.
				CHECK(bytes == 0);
				const auto old = dense.request(MessageType::Midi, {}, 3000).get();
				CHECK(old.error == Error::None && old.payload.size() == 8 + 512 * 48);
				unsigned oldCount = 0;
				CHECK(decodeVst3OutputEvents(old.payload, [&](const Vst3OutputEvent& event) {
					const auto ordinal = oldCount++;
					return event.sequence == 0 && event.frames == 512 && event.event.offset == ordinal
						&& event.event.channel == ordinal / 128 && event.event.id == ordinal % 128;
				}));
				CHECK(oldCount == 512);
				for (const auto sample : realtimeOutput)
				{
					CHECK(sample == (index ? 0.75f : 0.25f));
				}
			}
			else
			{
				CHECK(bytes == 8 + 512 * 48);
				unsigned count = 0;
				CHECK(
					decodeVst3OutputEvents(std::span(realtimeFeedback).first(bytes), [&](const Vst3OutputEvent& event) {
						const auto ordinal = count++;
						return event.sequence == block - 1 && event.frames == 512 && event.event.type == 1
							&& event.event.offset == ordinal && event.event.bus == 0
							&& event.event.channel == ordinal / 128 && event.event.id == ordinal % 128
							&& event.event.value == 0.5;
					}));
				CHECK(count == 512);
				for (const auto sample : realtimeOutput)
				{
					CHECK(sample == (index ? 0.75f : 0.25f));
				}
			}
			// Test-only pacing; processPrepared itself must not wait.
			Sleep(20);
		}
		const auto realtimeLater = dense.request(MessageType::Midi, {}, 3000).get();
		CHECK(realtimeLater.error == Error::None && realtimeLater.payload.size() == 8);
		CHECK(dense.close().get().error == Error::None);
		SetEnvironmentVariableW(L"LMMS_VST3_FIXTURE_DENSE_OUTPUT", L"1");
		SetEnvironmentVariableW(L"LMMS_VST3_FIXTURE_DENSE_OVERFLOW", L"1");
		CHECK(encodeVst3Create({scanned.classes[index].cid, 48000, 512, true, path}, denseCreate));
		CHECK(dense.open({helper, {}, 5000}).get().error == Error::None);
		CHECK(dense.request(MessageType::Create, denseCreate, 5000).get().error == Error::None);
		SetEnvironmentVariableW(L"LMMS_VST3_FIXTURE_DENSE_OUTPUT", nullptr);
		SetEnvironmentVariableW(L"LMMS_VST3_FIXTURE_DENSE_OVERFLOW", nullptr);
		CHECK(dense
				  .renderOffline(
					  {64, 2, 2}, std::vector<float>(128, 1), [] { return std::span<const std::uint8_t>{}; }, 3000,
					  MessageType::Midi)
				  .get()
				  .error
			!= Error::None);
		CHECK(dense.close().get().error == Error::None);
		CHECK(dense.open({helper, {}, 5000}).get().error == Error::None);
		CHECK(dense.request(MessageType::Create, denseCreate, 5000).get().error == Error::None);
		const auto recovered = dense
								   .renderOffline(
									   {64, 2, 2}, std::vector<float>(128, 1),
									   [] { return std::span<const std::uint8_t>{}; }, 3000, MessageType::Midi)
								   .get();
		CHECK(recovered.error == Error::None && recovered.followupPayload.size() == 8);
		CHECK(dense.close().get().error == Error::None);
	}
	CHECK(GetModuleHandleW(plugin.filename().c_str()) == nullptr);
	HostSession rejected;
	CHECK(rejected.open({helper, {}, 5000}).get().error == Error::None);
	std::vector<std::uint8_t> command;
	CHECK(encodeVst3Create({scanned.classes[2].cid, 48000, 512, false, path}, command));
	CHECK(rejected.request(MessageType::Create, command, 5000).get().error != Error::None);
	CHECK(rejected.close().get().error == Error::None);
	std::cout
		<< "PASS separate controller lifecycle, opaque ParamID metadata, independent instances, float32/float64 and variable-block processing\n";
	return 0;
}
