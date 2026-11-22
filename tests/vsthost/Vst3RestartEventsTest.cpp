#include "Vst3Instance.h"
#include <bit>
#include <cstdlib>
#include <iostream>
#include <new>
#include <string_view>

using namespace lmms::vsthost;
namespace {
bool watchAllocations = false;
std::size_t allocations = 0;
}
void* operator new(std::size_t bytes)
{
	if (watchAllocations)
	{
		++allocations;
	}
	if (auto* value = std::malloc(bytes ? bytes : 1))
	{
		return value;
	}
	throw std::bad_alloc();
}
void operator delete(void* value) noexcept
{
	std::free(value);
}
void operator delete(void* value, std::size_t) noexcept
{
	std::free(value);
}
#define CHECK(value) \
	do \
	{ \
		if (!(value)) \
		{ \
			std::cerr << "FAIL " << __LINE__ << ": " << #value << '\n'; \
			return 1; \
		} \
	} while (false)

int wmain(int argc, wchar_t** argv)
{
	CHECK(argc == 2);
	if (std::wstring_view(argv[1]) == L"--bulk-parameters")
	{
		constexpr Steinberg::int32 parameterCount = 2623;
		Vst3ParameterChanges changes(parameterCount);
		changes.reset(256);
		watchAllocations = true;
		for (Steinberg::int32 i = 0; i < parameterCount; ++i)
		{
			Steinberg::int32 queueIndex = -1, pointIndex = -1;
			auto* queue = changes.addParameterData(static_cast<Steinberg::Vst::ParamID>(i) * 1000, queueIndex);
			CHECK(queue && queueIndex == i && queue->addPoint(0, .25, pointIndex) == Steinberg::kResultOk);
		}
		Steinberg::int32 queueIndex = -1, pointIndex = -1, offset = -1;
		double value = 0;
		auto* queue = changes.addParameterData(0, queueIndex);
		CHECK(queue && queueIndex == 0);
		CHECK(queue->addPoint(120, .75, pointIndex) == Steinberg::kResultOk && pointIndex == 1);
		CHECK(queue->addPoint(60, .50, pointIndex) == Steinberg::kResultOk && pointIndex == 1);
		CHECK(queue->addPoint(60, .60, pointIndex) == Steinberg::kResultOk && pointIndex == 1);
		CHECK(queue->getPoint(1, offset, value) == Steinberg::kResultOk && offset == 60 && value == .60);
		CHECK(queue->getPoint(2, offset, value) == Steinberg::kResultOk && offset == 120 && value == .75);
		CHECK(changes.getParameterCount() == parameterCount && !changes.failed());
		changes.reset(256);
		CHECK(changes.getParameterCount() == 0 && changes.addParameterData(0, queueIndex) != nullptr);
		watchAllocations = false;
		CHECK(allocations == 0);
		std::cout << "PASS: 2623 refreshed VST3 parameters with ordered automation and no allocations\n";
		return 0;
	}
	try
	{
		std::vector<VST3::Hosting::ClassInfo> classes;
		{
			Vst3Module module;
			std::string error;
			CHECK(module.open(argv[1], error));
			classes = module.classes();
		}
		CHECK(classes.size() == 3);
		for (unsigned classIndex = 0; classIndex < 2; ++classIndex)
		{
			std::array<std::uint8_t, 16> cid{};
			std::memcpy(cid.data(), classes[classIndex].ID().data(), cid.size());
			const float defaultGain = classIndex ? 0.75f : 0.25f;
			std::array<float, 128> input, output;
			input.fill(1);
			std::array<std::uint8_t, 16384> storage{};
			// A valid first event must not survive rejection of the second event.
			for (unsigned malformed = 0; malformed < 8; ++malformed)
			{
				Vst3Instance instance;
				instance.open(argv[1], cid);
				instance.setup(48000, 64, true);
				const std::array<Vst3BlockEvent, 2> events{{{0, 7, 0, 0, 0xf0000101, 0.625}, {1, 11, 0, 3, 60, 0.5}}};
				const auto packet = encodeVst3Events(events, storage, 64);
				CHECK(!packet.empty());
				auto corrupt = std::vector<std::uint8_t>(packet.begin(), packet.end());
				switch (malformed)
				{
				case 0:
					put(corrupt, 40, 0, 4);
					put(corrupt, 52, 0, 4);
					put(corrupt, 56, UINT32_MAX, 4);
					break;
				case 1:
					put(corrupt, 48, 1, 4);
					break;
				case 2:
					put(corrupt, 52, 16, 4);
					break;
				case 3:
					put(corrupt, 60, 1, 4);
					break;
				case 4:
					put(corrupt, 44, 64, 4);
					break;
				case 5:
					corrupt.pop_back();
					break;
				case 6:
					put(corrupt, 64, 0x7ff8000000000000ULL, 8);
					break;
				default:
					break;
				}
				allocations = 0;
				watchAllocations = true;
				const bool retained = instance.deferRestartEvents(corrupt, malformed == 7 ? 65 : 64);
				watchAllocations = false;
				CHECK(!retained && allocations == 0);
				CHECK(instance.process(64, input, output));
				for (auto sample : output)
				{
					CHECK(sample == defaultGain);
				}
				instance.close();
			}
			{
				Vst3Instance instance;
				instance.open(argv[1], cid);
				instance.setup(48000, 64, true);
				std::array<Vst3BlockEvent, 511> almostFull;
				almostFull.fill({0, 7, 0, 0, 0xf0000101, 0.375});
				auto packet = encodeVst3Events(almostFull, storage, 64);
				CHECK(!packet.empty());
				allocations = 0;
				watchAllocations = true;
				const bool filled = instance.deferRestartEvents(packet, 64);
				watchAllocations = false;
				CHECK(filled && allocations == 0);
				const std::array<Vst3BlockEvent, 2> tooMany{
					{{0, 3, 0, 0, 0xf0000101, 0.625}, {0, 4, 0, 0, 0xf0000101, 0.875}}};
				packet = encodeVst3Events(tooMany, storage, 64);
				CHECK(!packet.empty());
				CHECK(!instance.deferRestartEvents(packet, 64));
				// Exactly one slot must still be available after the rejected packet.
				const std::array<Vst3BlockEvent, 1> last{{{0, 9, 0, 0, 0xf0000101, 0.5}}};
				packet = encodeVst3Events(last, storage, 64);
				CHECK(instance.deferRestartEvents(packet, 64));
				CHECK(!instance.deferRestartEvents(packet, 64));
				const auto saved = instance.state();
				CHECK(saved.size() == 32);
				CHECK(
					std::bit_cast<double>(get(saved, 16, 8)) == 0.5 && std::bit_cast<double>(get(saved, 24, 8)) == 0.5);
				CHECK(instance.process(64, input, output));
				for (auto sample : output)
				{
					CHECK(sample == 0.5f);
				}
				// A successful barrier retires automation and makes space reusable.
				CHECK(instance.deferRestartEvents(packet, 64));
				CHECK(instance.process(64, input, output));
				instance.close();
			}
			{
				Vst3Instance instance;
				instance.open(argv[1], cid);
				instance.setup(48000, 64, true);
				CHECK(instance.controller()->setParamNormalized(42, 0.0625) == Steinberg::kResultOk);
				instance.serviceControl(false);
				CHECK(instance.restartPending());
				const std::array<Vst3BlockEvent, 2> retained{{{0, 7, 0, 0, 0xf0000101, 0.375}, {1, 11, 0, 3, 60, 0.5}}};
				CHECK(instance.deferRestartEvents(encodeVst3Events(retained, storage, 64), 64));
				instance.serviceControl();
				CHECK(!instance.restartPending());
				CHECK(instance.process(64, input, output));
				for (auto sample : output)
				{
					CHECK(sample == 0.875f);
				}
				const std::array<Vst3BlockEvent, 1> noteOff{{{2, 0, 0, 3, 60, 0}}};
				CHECK(instance.process(64, input, output, encodeVst3Events(noteOff, storage, 64)));
				for (auto sample : output)
				{
					CHECK(sample == 0.375f);
				}
				instance.close();
			}
			{
				Vst3Instance instance;
				instance.open(argv[1], cid);
				instance.setup(48000, 64, true);
				CHECK(instance.controller()->setParamNormalized(42, 0.03125) == Steinberg::kResultOk);
				instance.serviceControl();
				std::vector<std::uint8_t> selector(4);
				put(selector, 0, 1, 4);
				Vst3Metadata metadata;
				CHECK(decodeVst3Metadata(instance.parameterControl(selector), metadata));
				CHECK(metadata.buses.size() == 4 && metadata.buses[0].name == "Renamed Main"
					&& metadata.buses[1].name == "Renamed Main" && metadata.buses[2].name == "MIDI");
				CHECK(instance.process(64, input, output));
				for (auto sample : output)
				{
					CHECK(sample == defaultGain);
				}
				instance.close();
			}
		}
	}
	catch (const std::exception& error)
	{
		std::cerr << error.what() << '\n';
		return 1;
	}
	std::cout
		<< "PASS native retained event capacity, atomic rejection, zero allocations and asynchronous reload replay\n";
	return 0;
}
