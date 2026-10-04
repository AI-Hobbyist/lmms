#include "vsthost/HostSession.h"
#include "vsthost/LegacyMessageCodec.h"
#include "vsthost/Vst2Messages.h"
#include "vsthost/Vst2Scanner.h"
#include <filesystem>
#include <iostream>
#include <tlhelp32.h>
#include <cmath>

using namespace lmms;
using namespace lmms::vsthost;
namespace
{
int failures = 0;
void check(bool value, const char* name) { if (!value) { ++failures; std::cerr << "FAIL: " << name << '\n'; } }
std::string utf8(const std::filesystem::path& path)
{ const auto value = path.u8string(); return {reinterpret_cast<const char*>(value.data()), value.size()}; }
HostSession::Reply call(HostSession& session, std::vector<LegacyCommand> commands,
	MessageType type = MessageType::Parameter, DWORD timeout = 1500)
{
	std::vector<std::uint8_t> packet;
	check(encodeLegacy(commands, packet) == Error::None, "command encoding");
	return session.request(type, std::move(packet), timeout, true).get();
}
bool loadedInParent(const wchar_t* name)
{
	const auto snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, GetCurrentProcessId());
	if (snapshot == INVALID_HANDLE_VALUE) { check(false, "parent module snapshot"); return true; }
	MODULEENTRY32W module{}; module.dwSize = sizeof(module); bool found = false;
	if (Module32FirstW(snapshot, &module)) { do { found |= _wcsicmp(module.szModule, name) == 0; } while (Module32NextW(snapshot, &module)); }
	CloseHandle(snapshot); return found;
}
}
int wmain(int argc, wchar_t** argv)
{
	if (argc != 3) { return 2; }
	const HostSession::Configuration config{argv[1], {L"headless"}, 3000};
	const std::filesystem::path fixtures(argv[2]);
	const auto shell = utf8(fixtures / L"Vst2Shell.dll");
	HostSession scanner;
	check(scanner.open(config).get().error == Error::None, "scanner opens");
	auto reply = call(scanner, {{IdVstScanPlugin, {shell}}}, MessageType::Scan);
	check(reply.error == Error::None, "shell enumeration succeeds without audio or editor initialization");
	std::vector<LegacyCommand> entries;
	check(decodeLegacy(reply.payload, entries) == Error::None, "shell enumeration decodes");
	const auto found = std::find_if(entries.begin(), entries.end(), [](const auto& item) { return item.id == IdVstShellEntries; });
		check(found != entries.end() && found->arguments == std::vector<std::string>{"2", "1", "4043440900", "Shell Alpha", "16777728", "Shell Beta", "LMMS tests", "1"}, "all child IDs preserved unsigned including high bit and embedded zero bytes, with module metadata");
	check(!loadedInParent(L"Vst2Shell.dll"), "scanner never loads native module in parent");
	check(scanner.close().get().error == Error::None, "scanner closes");
	const auto catalog = scanVst2(config, shell, 1500);
	check(catalog.error == Error::None && catalog.shell && catalog.entries.size() == 2 &&
		catalog.entries[0].id == 0xf1020304u && catalog.entries[1].id == 0x01000200u,
			"production scanner preserves all selected identities");
		check(catalog.vendor == "LMMS tests" && catalog.version == "1", "shell module vendor/version metadata");
	const auto ordinary = scanVst2(config, utf8(fixtures / L"Vst2Baseline.dll"), 1500);
	check(ordinary.error == Error::None && !ordinary.shell && ordinary.entries.size() == 1,
			"production scanner distinguishes ordinary module");
		check(ordinary.vendor == "LMMS tests" && ordinary.version == "1", "ordinary module vendor/version metadata");
	HostSession alpha, beta;
	check(alpha.open(config).get().error == Error::None && beta.open(config).get().error == Error::None, "independent child sessions");
	for (auto* session : {&alpha, &beta})
	{
		const auto id = session == &alpha ? "4043440900" : "16777728";
		reply = call(*session, {{IdBufferSizeInformation, {"64"}}, {IdVstLoadPlugin, {shell, id}}}, MessageType::Create);
		if (reply.error != Error::None) { std::cerr << "selection " << id << " error " << static_cast<unsigned>(reply.error) << " native " << session->fault().nativeCode << '\n'; }
		check(reply.error == Error::None, "selected ID is set before plugin entry");
	}
	check(alpha.pid() != beta.pid(), "shell children have distinct processes");
	std::vector<float> input(128, 0.8f);
	for (auto* session : {&alpha, &beta})
	{
		reply = session->renderOffline({64, 2, 2}, input, [] { return std::span<const std::uint8_t>{}; }).get();
		check(reply.error == Error::None && reply.payload.size() == input.size() * sizeof(float), "child audio returns");
		std::vector<float> output(128); if (reply.payload.size() == output.size() * sizeof(float)) { std::memcpy(output.data(), reply.payload.data(), reply.payload.size()); }
		const float expected = session == &alpha ? 0.2f : 0.6f;
		check(std::all_of(output.begin(), output.end(), [expected](float sample) { return std::abs(sample - expected) < 1e-6f; }), "distinct child gain proves precise ID selection");
	}
	check(alpha.close().get().error == Error::None && beta.close().get().error == Error::None, "shell children close");
	for (const auto* id : {"0", "4294967296", "-1", "1"})
	{
		check(scanner.open(config).get().error == Error::None, "invalid selection session opens");
		reply = call(scanner, {{IdVstLoadPlugin, {shell, id}}}, MessageType::Create);
		check(reply.error != Error::None, "invalid or absent shell ID rejected");
		scanner.close().get();
	}
	for (const auto* mode : {L"Hang", L"Crash", L"Duplicate"})
	{
		check(scanner.open(config).get().error == Error::None, "fault scan opens");
		const auto path = utf8(fixtures / (std::wstring(L"Vst2Shell") + mode + L".dll"));
		const auto start = GetTickCount64();
		reply = call(scanner, {{IdVstScanPlugin, {path}}}, MessageType::Scan, 250);
		check(reply.error != Error::None && GetTickCount64() - start < 2000, "scan fault is bounded and isolated");
		check(scanner.fault().stage == ProcessSupervisor::Stage::Scan, "fault reports scan stage");
		if (std::wstring_view(mode) == L"Crash") { check(scanner.fault().nativeCode == 0xe0000053u, "scan exception diagnosis"); }
		scanner.close().get();
	}
		std::stop_source cancellation;
		auto pending = std::async(std::launch::async, [&]
		{
			return scanVst2(config, utf8(fixtures / L"Vst2ShellHang.dll"), 30000, cancellation.get_token());
		});
		check(pending.wait_for(std::chrono::milliseconds(200)) == std::future_status::timeout, "scan is still pending before cancellation");
		const auto cancelStart = GetTickCount64(); cancellation.request_stop();
		check(pending.wait_for(std::chrono::seconds(2)) == std::future_status::ready, "scan cancellation wakes dispatcher");
		const auto cancelled = pending.get();
		check(cancelled.cancelled && cancelled.error != Error::None && cancelled.error != Error::Timeout &&
			cancelled.entries.empty() && GetTickCount64() - cancelStart < 2000, "cancelled scan discards entries without waiting for scan timeout");
		check(cancelled.fault.stage == ProcessSupervisor::Stage::Scan, "cancelled scan preserves its stage");
		check(scanVst2(config, shell, 1500).error == Error::None, "scanning recovers after cancellation");
		std::cout << "VST2 shell: " << failures << " failures\n";
	return failures ? 1 : 0;
}
