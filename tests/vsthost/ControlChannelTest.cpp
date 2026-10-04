#include "vsthost/ControlChannel.h"
#include "vsthost/ProcessSupervisor.h"
#include "vsthost/SharedRegion.h"
#include <array>
#include <iostream>

using namespace lmms::vsthost;
constexpr std::uint32_t Capacity = 1024;
int echo(const wchar_t* name)
{
	SharedRegion region;
	if (!region.attach(name, ControlChannel::storageBytes(Capacity))) { return 2; }
	ControlChannel helper(region.bytes(), ControlChannel::Side::Helper, Capacity);
	ControlChannel::Frame frame;
	const auto error = helper.wait(frame, 2000, [] { return true; });
	if (error != Error::None || !matches(frame.header, 7, 2, 3)) { return 3; }
	frame.header.type = MessageType::Hello;
	return helper.send(frame.header, frame.payload) == Error::None ? 0 : 4;
}
bool crossProcess(const wchar_t* executable)
{
	const auto name = L"Local\\LMMS-VstControl-Test-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64());
	SharedRegion region;
	if (!region.create(name, ControlChannel::storageBytes(Capacity))) { return false; }
	ControlChannel host(region.bytes(), ControlChannel::Side::Host, Capacity);
	if (!host.initialize()) { return false; }
	std::array<std::uint8_t, Capacity> payload{};
	for (unsigned i = 0; i < payload.size(); ++i) { payload[i] = static_cast<std::uint8_t>(i); }
	if (host.send({MessageType::Hello, 7, 2, 3, Capacity}, payload) != Error::None) { return false; }
	ProcessSupervisor child;
	child.expectExit();
	if (!child.start(executable, {L"--echo", name}, 5000)) { return false; }
	ControlChannel::Frame reply;
	const auto error = host.wait(reply, 5000, [&] { return child.running(); });
	child.close(2000);
	DWORD exitCode = 1; GetExitCodeProcess(child.processHandle(), &exitCode);
	return error == Error::None && matches(reply.header, 7, 2, 3) &&
		reply.payload == std::vector<std::uint8_t>(payload.begin(), payload.end()) && exitCode == 0;
}
int wmain(int argc, wchar_t** argv)
{
	if (argc == 3 && std::wstring(argv[1]) == L"--echo") { return echo(argv[2]); }
	int failures = 0;
	auto check = [&](bool ok, const char* label) { if (!ok) { ++failures; std::cerr << label << '\n'; } };
	std::vector<std::uint8_t> storage(ControlChannel::storageBytes(Capacity));
	ControlChannel host(storage, ControlChannel::Side::Host, Capacity), helper(storage, ControlChannel::Side::Helper, Capacity);
	check(host.initialize(), "initialize");
	check(ControlChannel::storageBytes(3) == 0 && ControlChannel::storageBytes(UINT32_MAX) == 0, "alignment/overflow capacity");
	ControlChannel::Frame frame;
	check(helper.receive(frame) == Error::InvalidState, "empty receive");
	const std::array<std::uint8_t, 3> payload{0, 255, 42};
	Header request{MessageType::GetState, 7, 2, 3, 3};
	check(host.send(request, payload) == Error::None, "publish frame");
	check(host.send(request, payload) == Error::InvalidState, "backpressure without overwrite");
	check(helper.receive(frame) == Error::None && frame.payload == std::vector<std::uint8_t>(payload.begin(), payload.end()) && matches(frame.header, 7, 2, 3), "decode frame");
	check(helper.send(frame.header, frame.payload) == Error::None && host.receive(frame) == Error::None, "duplex reply");
	request.payloadBytes = 4;
	check(host.send(request, payload) == Error::InvalidMessage, "payload length mismatch");
	request.payloadBytes = 3;
	check(host.send(request, payload) == Error::None, "bad wire setup");
	put(storage, 8 + 32, UINT32_MAX, 4);
	check(helper.receive(frame) == Error::InvalidMessage, "untrusted length rejected before allocation");
	check(host.send(request, payload) == Error::None, "version setup");
	put(storage, 8 + 4, ProtocolVersion + 1, 2);
	check(helper.receive(frame) == Error::ProtocolMismatch, "wrong version rejected");
	check(helper.wait(frame, 10, [] { return true; }) == Error::Timeout, "bounded wait on living silent peer");
	check(helper.wait(frame, 1000, [] { return false; }) == Error::Disconnected, "disconnect returns promptly");
	std::array<wchar_t, 32768> executable{};
	GetModuleFileNameW(nullptr, executable.data(), static_cast<DWORD>(executable.size()));
	check(crossProcess(executable.data()), "same-ABI process echo");
	if (argc == 3 && std::wstring(argv[1]) == L"--peer") { check(crossProcess(argv[2]), "x64 host/x86 helper echo"); }
	std::cout << (failures ? "FAIL" : "PASS") << ": control channel, " << failures << " failures\n";
	return failures ? 1 : 0;
}
