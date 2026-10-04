#include "vsthost/ProcessSupervisor.h"
#include <array>
#include <iostream>

using namespace lmms::vsthost;
bool waitStopped(ProcessSupervisor& supervisor, DWORD timeout)
{
	const auto end = GetTickCount64() + timeout;
	while (supervisor.running() && GetTickCount64() < end) { Sleep(2); }
	return !supervisor.running();
}
int wmain(int argc, wchar_t** argv)
{
	SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
	std::array<wchar_t, 32768> path{};
	if (!GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()))) { return 2; }
	if (argc >= 2)
	{
		const std::wstring mode = argv[1];
		if (mode == L"--hang") { Sleep(INFINITE); return 0; }
		if (mode == L"--crash") { RaiseException(0xE0000042, EXCEPTION_NONCONTINUABLE, 0, nullptr); return 42; }
		if (mode == L"--exit") { return 0; }
		if (mode == L"--args")
	{ return argc == 5 && std::wstring(argv[2]) == L"\u7a7a \u683c\\" && std::wstring(argv[3]) == L"a\"b" && std::wstring(argv[4]).empty() ? 0 : 8; }
		if (mode == L"--tree" && argc == 3)
		{
			const auto mapping = OpenFileMappingW(FILE_MAP_ALL_ACCESS, FALSE, argv[2]);
			if (!mapping) { return 3; }
			auto* childPid = static_cast<volatile LONG*>(MapViewOfFile(mapping, FILE_MAP_ALL_ACCESS, 0, 0, 4));
			if (!childPid) { CloseHandle(mapping); return 4; }
			auto command = ProcessSupervisor::quote(path.data()) + L" --hang";
			STARTUPINFOW startup{}; startup.cb = sizeof(startup); PROCESS_INFORMATION child{};
			if (!CreateProcessW(path.data(), command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &startup, &child)) { return 5; }
			InterlockedExchange(childPid, static_cast<LONG>(child.dwProcessId));
			CloseHandle(child.hThread); CloseHandle(child.hProcess);
			UnmapViewOfFile(const_cast<LONG*>(childPid)); CloseHandle(mapping);
			Sleep(INFINITE); return 0;
		}
	}
	int failures = 0;
	auto check = [&](bool ok, const char* label) { if (!ok) { ++failures; std::cerr << label << '\n'; } };
	{
		ProcessSupervisor supervisor;
		check(!supervisor.start(L"Z:\\missing-lmms-helper.exe", {}), "missing helper rejected");
		check(supervisor.fault().error == Error::MissingHelper, "missing helper diagnostics");
	}
	{
		ProcessSupervisor supervisor;
		check(supervisor.start(path.data(), {L"--hang"}), "start suspended/job child");
		supervisor.arm(ProcessSupervisor::Stage::Control, 50);
		check(waitStopped(supervisor, 2000), "watchdog terminates hang without caller polling IPC");
		check(supervisor.fault().error == Error::Timeout && supervisor.fault().stage == ProcessSupervisor::Stage::Control, "timeout stage");
	}
	{
		ProcessSupervisor supervisor;
		check(supervisor.start(path.data(), {L"--crash"}), "crash child start");
		check(waitStopped(supervisor, 2000), "crash child exits");
		const auto end = GetTickCount64() + 1000;
		while (supervisor.fault().error == Error::None && GetTickCount64() < end) { Sleep(2); }
		check(supervisor.fault().error == Error::ProcessCrashed && supervisor.fault().nativeCode != 0, "crash diagnostics");
	}
	{
		ProcessSupervisor supervisor;
		check(supervisor.start(path.data(), {L"--hang"}), "audio fault child start");
		supervisor.disarm();
		supervisor.requestTermination(Error::ProcessingFailed);
		check(waitStopped(supervisor, 2000), "audio fault watcher termination");
		check(supervisor.fault().error == Error::ProcessingFailed && supervisor.fault().stage == ProcessSupervisor::Stage::Audio, "audio fault stage");
	}
	{
		ProcessSupervisor supervisor; supervisor.expectExit();
		check(supervisor.start(path.data(), {L"--args", L"\u7a7a \u683c\\", L"a\"b", L""}), "unicode/quote child start");
		check(waitStopped(supervisor, 2000), "normal child exits");
		DWORD code = 1; GetExitCodeProcess(supervisor.processHandle(), &code);
		check(code == 0 && supervisor.fault().error == Error::None, "argument quoting and expected exit");
	}
	{
		const auto name = L"Local\\LMMS-Supervisor-Test-" + std::to_wstring(GetCurrentProcessId());
		const auto mapping = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, 4, name.c_str());
		auto* childPid = mapping ? static_cast<volatile LONG*>(MapViewOfFile(mapping, FILE_MAP_ALL_ACCESS, 0, 0, 4)) : nullptr;
		check(childPid != nullptr, "tree PID mapping");
		if (childPid)
		{
			ProcessSupervisor supervisor;
			check(supervisor.start(path.data(), {L"--tree", name}), "process tree start"); supervisor.disarm();
			const auto end = GetTickCount64() + 2000;
			while (!InterlockedCompareExchange(childPid, 0, 0) && GetTickCount64() < end) { Sleep(2); }
			const auto pid = static_cast<DWORD>(InterlockedCompareExchange(childPid, 0, 0));
			const auto child = OpenProcess(SYNCHRONIZE, FALSE, pid);
			check(pid && child, "descendant observed");
			supervisor.close(0);
			check(!supervisor.running(), "root terminated");
			if (child) { check(WaitForSingleObject(child, 2000) == WAIT_OBJECT_0, "Job terminates descendant"); CloseHandle(child); }
			UnmapViewOfFile(const_cast<LONG*>(childPid));
		}
		if (mapping) { CloseHandle(mapping); }
	}
	DWORD before = 0, after = 0; GetProcessHandleCount(GetCurrentProcess(), &before);
	for (int i = 0; i < 50; ++i)
	{
		ProcessSupervisor supervisor;
		check(supervisor.start(path.data(), {L"--hang"}), "repeated start"); supervisor.close(0);
		check(!supervisor.running(), "repeated cleanup");
	}
	GetProcessHandleCount(GetCurrentProcess(), &after);
	check(after <= before + 2, "50 lifetimes retain no job/process/thread handles");
	std::cout << (failures ? "FAIL" : "PASS") << ": supervisor, " << failures << " failures\n";
	return failures ? 1 : 0;
}
