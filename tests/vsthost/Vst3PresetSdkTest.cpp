#include "vsthost/Vst3Preset.h"
#include "pluginterfaces/base/funknownimpl.h"
#include "public.sdk/source/common/memorystream.h"
#include "public.sdk/source/vst/vstpresetfile.h"
#include <iostream>

using namespace lmms::vsthost;
using namespace Steinberg;
#define CHECK(value) do { if (!(value)) { std::cerr << "FAIL " << __LINE__ << ": " << #value << '\n'; return 1; } } while (false)

int main()
{
	std::array<std::uint8_t, 16> cid{};
	for (unsigned i = 0; i < cid.size(); ++i) { cid[i] = static_cast<std::uint8_t>(i); }
	TUID native{}; std::memcpy(native, cid.data(), 16);
	const auto uid = FUID::fromTUID(native);
	const auto text = vst3PresetClassText(cid);
	CHECK(!std::memcmp(text.data(), "030201000504070608090A0B0C0D0E0F", 32));
	char sdkText[33]{}; uid.toString(sdkText); CHECK(!std::memcmp(text.data(), sdkText, 32));
	for (unsigned mode = 0; mode < 3; ++mode)
	{
		std::vector<std::uint8_t> comp{0, 1, 255, 42, 0, 9};
		std::vector<std::uint8_t> cont = mode == 2 ? std::vector<std::uint8_t>{} : std::vector<std::uint8_t>{1, 0, 254};
		std::vector<std::uint8_t> state(16 + comp.size() + (mode ? cont.size() : 0));
		put(state, 0, 1, 4); put(state, 4, comp.size(), 4); put(state, 8, mode ? cont.size() : 0, 4); put(state, 12, mode ? 1 : 0, 4);
		std::copy(comp.begin(), comp.end(), state.begin() + 16);
		if (mode) { std::copy(cont.begin(), cont.end(), state.begin() + 16 + comp.size()); }
		std::vector<std::uint8_t> preset, roundtrip;
		CHECK(encodeVst3Preset(cid, state, preset)); CHECK(decodeVst3Preset(cid, preset, roundtrip) && roundtrip == state);
		// Independent SDK reader validates our container and GUID representation.
		auto encodedStream = owned(new MemoryStream(preset.data(), preset.size()));
		Vst::PresetFile reader(encodedStream);
		CHECK(reader.readChunkList() && reader.getClassID() == uid);
		CHECK(reader.contains(Vst::kComponentState) && reader.contains(Vst::kControllerState) == (mode != 0));
		CHECK(reader.seekToComponentState());
		std::vector<std::uint8_t> sdkComponent(comp.size()); int32 readCount = 0;
		CHECK(encodedStream->read(sdkComponent.data(), static_cast<int32>(sdkComponent.size()), &readCount) == kResultOk);
		CHECK(readCount == comp.size() && sdkComponent == comp);
		if (mode)
		{
			CHECK(reader.seekToControllerState()); std::vector<std::uint8_t> sdkController(cont.size());
			CHECK(encodedStream->read(sdkController.data(), static_cast<int32>(sdkController.size()), &readCount) == kResultOk);
			CHECK(readCount == cont.size() && sdkController == cont);
		}
		auto lowercase = preset;
		for (unsigned i = 8; i < 40; ++i) { if (lowercase[i] >= 'A' && lowercase[i] <= 'F') { lowercase[i] += 'a' - 'A'; } }
		CHECK(decodeVst3Preset(cid, lowercase, roundtrip) && roundtrip == state);
		std::array<std::uint8_t, 16> nullCid{};
		CHECK(!encodeVst3Preset(nullCid, state, roundtrip) && !decodeVst3Preset(nullCid, preset, roundtrip));
		// Independent SDK writer creates native chunks plus optional XML metadata.
		auto component = owned(new MemoryStream(comp.data(), comp.size())); auto controller = owned(new MemoryStream(cont.data(), cont.size()));
		auto sdkStream = owned(new MemoryStream);
		CHECK(Vst::PresetFile::savePreset(sdkStream, uid, component, mode ? controller.get() : nullptr, "<MetaInfo/>", 11));
		CHECK(decodeVst3Preset(cid, std::span(reinterpret_cast<const std::uint8_t*>(sdkStream->getData()), static_cast<std::size_t>(sdkStream->getSize())), roundtrip) && roundtrip == state);
		for (std::size_t length = 0; length < preset.size(); ++length)
		{
			roundtrip = {77}; CHECK(!decodeVst3Preset(cid, std::span(preset).first(length), roundtrip)); CHECK(roundtrip == std::vector<std::uint8_t>{77});
		}
		const auto list = get(preset, 40, 8);
		for (unsigned malformed = 0; malformed < 7; ++malformed)
		{
			auto corrupt = preset;
			switch (malformed)
			{
			case 0: put(corrupt, 40, UINT64_MAX, 8); break;
			case 1: put(corrupt, list + 4, 129, 4); break;
			case 2: put(corrupt, list + 12, 47, 8); break;
			case 3: put(corrupt, list + 20, UINT64_MAX, 8); break;
			case 4: corrupt[8] = 'G'; break;
			case 5: put(corrupt, 4, 2, 4); break;
			case 6: std::memcpy(corrupt.data() + list + 8, "Info", 4); break;
			}
			roundtrip = {77}; CHECK(!decodeVst3Preset(cid, corrupt, roundtrip) && roundtrip == std::vector<std::uint8_t>{77});
		}
		if (mode)
		{
			auto duplicate = preset; std::memcpy(duplicate.data() + list + 28, "Comp", 4);
			CHECK(!decodeVst3Preset(cid, duplicate, roundtrip));
		}
	}
	std::cout << "PASS SDK preset read/write interoperability, Windows CID, component-only/empty controller, metadata, truncation and hostile ranges\n";
	return 0;
}
