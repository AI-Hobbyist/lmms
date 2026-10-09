#include "vsthost/LegacyHostBridge.h"
#include <filesystem>
#include <iostream>
#include <cstdlib>

// Observe allocations on the caller thread only; the control dispatcher is
// deliberately allowed to allocate. The realtime methods must not invoke it.
thread_local bool observeAllocations = false;
thread_local unsigned allocations = 0;
void* operator new(std::size_t bytes)
{
	if (observeAllocations)
	{
		++allocations;
	}
	if (auto* pointer = std::malloc(bytes ? bytes : 1))
	{
		return pointer;
	}
	throw std::bad_alloc();
}
void* operator new[](std::size_t bytes)
{
	return ::operator new(bytes);
}
void operator delete(void* pointer) noexcept
{
	std::free(pointer);
}
void operator delete[](void* pointer) noexcept
{
	std::free(pointer);
}
void operator delete(void* pointer, std::size_t) noexcept
{
	std::free(pointer);
}
void operator delete[](void* pointer, std::size_t) noexcept
{
	std::free(pointer);
}

using namespace lmms;
using namespace lmms::vsthost;
namespace {
int failures = 0;
void check(bool result, const char* description)
{
	if (!result)
	{
		++failures;
		std::cerr << "FAIL: " << description << '\n';
	}
}
std::string utf8(const std::filesystem::path& path)
{
	const auto value = path.u8string();
	return {reinterpret_cast<const char*>(value.data()), value.size()};
}
}
int wmain(int argc, wchar_t** argv)
{
	if (argc != 3)
	{
		return 2;
	}
	const HostSession::Configuration configuration{argv[1], {L"headless"}, 3000};
	std::vector<RemotePluginBase::message> metadata;
	auto bridge = std::make_unique<LegacyHostBridge>([&](const auto& message) { metadata.push_back(message); });
	check(bridge->open(configuration), "production bridge opens");
	check(bridge->send(RemotePluginBase::message(IdSampleRateInformation).addInt(44100)) != 0, "sample rate control");
	check(bridge->send(RemotePluginBase::message(IdBufferSizeInformation).addInt(64)) != 0, "block size control");
	check(bridge->send(RemotePluginBase::message(IdVstLoadPlugin).addString(utf8(argv[2]))) != 0, "native DLL load");
	check(std::any_of(metadata.begin(), metadata.end(), [](const auto& m) { return m.id == IdInitDone; }),
		"metadata callback on control caller");
	std::array<float, 128> input{}, output{};
	input.fill(0.75f);
	observeAllocations = true;
	bridge->parameter(0, 0.25f);
	bridge->tempo(120);
	bridge->midi({0x90, 0, 60, 127, 11});
	bridge->midi({0x80, 0, 60, 0, 37});
	const auto start = GetTickCount64();
	const bool submitted = bridge->process(64, 2, 2, input.data(), output.data(), false);
	const auto duration = GetTickCount64() - start;
	observeAllocations = false;
	check(submitted && allocations == 0 && duration < 10, "RT MIDI/parameter/tempo/audio allocate zero and never wait");
	check(
		std::all_of(output.begin(), output.end(), [](float sample) { return sample == 0; }), "RT first block silence");
	Sleep(25);
	observeAllocations = true;
	const bool received = bridge->process(64, 2, 2, input.data(), output.data(), false);
	observeAllocations = false;
	check(received && allocations == 0, "RT completed block allocate zero");
	for (unsigned frame = 0; frame < 64; ++frame)
	{
		check(output[frame * 2] == 0.1875f + (frame >= 11 && frame < 37 ? 1.0f : 0.0f),
			"RT binary parameter and exact MIDI offsets");
	}
	bridge->parameter(0, 0.75f);
	check(bridge->send(IdVstGetParameterDump) != 0, "control barrier flushes pending RT parameters");
	const auto dump
		= std::find_if(metadata.rbegin(), metadata.rend(), [](const auto& m) { return m.id == IdVstParameterDump; });
	check(dump != metadata.rend() && dump->argumentCount() == 4 && dump->getFloat(3) == 0.75f,
		"state getter observes parameter");
	check(bridge->process(64, 2, 2, input.data(), output.data(), true), "offline has bounded renderer request");
	check(std::all_of(output.begin(), output.end(), [](float sample) { return sample == 0.5625f; }),
		"offline current block has exact samples without bridge delay");
	check(bridge->process(64, 2, 2, input.data(), output.data(), false), "offline to RT transition");
	check(std::all_of(output.begin(), output.end(), [](float sample) { return sample == 0; }),
		"RT transition restarts known pipeline");
	check(bridge->send(IdQuit) != 0, "production bridge closes");
	bridge.reset();
	// Exercise recovery through the exact transport installed in RemotePlugin,
	// including the interval between the session handshake and native DLL load.
	bridge = std::make_unique<LegacyHostBridge>([](const auto&) {});
	check(bridge->open(configuration), "fault bridge opens");
	check(!bridge->process(64, 2, 2, input.data(), output.data(), false), "unloaded bridge returns silence");
	const auto hangingDll = std::filesystem::path(argv[2]).parent_path() / L"Vst2FaultAudioHang.dll";
	check(bridge->send(RemotePluginBase::message(IdBufferSizeInformation).addInt(64)) != 0, "fault block size");
	check(bridge->send(RemotePluginBase::message(IdVstLoadPlugin).addString(utf8(hangingDll))) != 0, "fault DLL load");
	const auto previousGeneration = bridge->generation();
	check(bridge->process(64, 2, 2, input.data(), output.data(), false), "hanging native block submitted without wait");
	Sleep(25);
	check(!bridge->process(64, 2, 2, input.data(), output.data(), false), "late native block fails promptly");
	check(std::all_of(output.begin(), output.end(), [](float sample) { return sample == 0; }),
		"faulted bridge remains silent");
	bridge->parameter(0, 0.9f);
	check(bridge->open(configuration), "fault bridge reopens");
	check(bridge->generation() == previousGeneration + 1, "recovery advances generation");
	check(!bridge->process(64, 2, 2, input.data(), output.data(), false), "reopen does not reuse loaded state");
	check(bridge->send(RemotePluginBase::message(IdBufferSizeInformation).addInt(64)) != 0, "recovery block size");
	check(bridge->send(RemotePluginBase::message(IdVstLoadPlugin).addString(utf8(argv[2]))) != 0,
		"recovery loads healthy DLL");
	check(bridge->process(64, 2, 2, input.data(), output.data(), true), "recovered bridge renders");
	check(std::all_of(output.begin(), output.end(), [](float sample) { return sample == 0.375f; }),
		"recovery known wave");
	check(bridge->send(IdQuit) != 0, "recovered bridge closes");
	bridge.reset();
	DWORD before = 0, after = 0;
	GetProcessHandleCount(GetCurrentProcess(), &before);
	for (unsigned cycle = 0; cycle < 50; ++cycle)
	{
		auto instance = std::make_unique<LegacyHostBridge>([](const auto&) {});
		check(instance->open(configuration), "resource lifetime open");
		check(instance->send(RemotePluginBase::message(IdVstLoadPlugin).addString(utf8(argv[2]))) != 0,
			"resource lifetime native load");
		check(instance->send(IdQuit) != 0, "resource lifetime close");
	}
	GetProcessHandleCount(GetCurrentProcess(), &after);
	check(after <= before + 2, "50 production lifetimes release mappings/jobs/process handles");
	std::cout << "Production realtime bridge: " << failures << " failures\n";
	return failures ? 1 : 0;
}
