#include "WindowsCrashReport.h"

#include <dbghelp.h>
#include <psapi.h>

#include <cerrno>
#include <cstdio>
#include <cwchar>
#include <filesystem>
#include <limits>

namespace
{
using lmms::windowsCrashReport::Request;
using lmms::windowsCrashReport::detail::reportFailure;

std::wstring timestamp()
{
	FILETIME utc{};
	FILETIME local{};
	SYSTEMTIME time{};
	GetSystemTimePreciseAsFileTime(&utc);
	FileTimeToLocalFileTime(&utc, &local);
	FileTimeToSystemTime(&local, &time);
	ULARGE_INTEGER ticks{};
	ticks.LowPart = utc.dwLowDateTime;
	ticks.HighPart = utc.dwHighDateTime;
	std::array<wchar_t, 40> text{};
	std::swprintf(text.data(), text.size(), L"%04u%02u%02u-%02u%02u%02u-%07u",
		static_cast<unsigned>(time.wYear), static_cast<unsigned>(time.wMonth), static_cast<unsigned>(time.wDay),
		static_cast<unsigned>(time.wHour), static_cast<unsigned>(time.wMinute), static_cast<unsigned>(time.wSecond),
		static_cast<unsigned>(ticks.QuadPart % 10000000));
	return text.data();
}

DWORD writeDump(const std::filesystem::path& file, HANDLE process, DWORD pid, const Request& request)
{
	// Load the OS copy even when the reporter is next to third-party binaries.
	const auto library = LoadLibraryExW(L"dbghelp.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
	if (!library)
	{
		return GetLastError();
	}
	const auto write = reinterpret_cast<decltype(&MiniDumpWriteDump)>(GetProcAddress(library, "MiniDumpWriteDump"));
	if (!write)
	{
		const auto error = GetLastError();
		FreeLibrary(library);
		return error;
	}
	const auto dump = CreateFileW(file.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
	DWORD error = dump == INVALID_HANDLE_VALUE ? GetLastError() : ERROR_SUCCESS;
	if (dump != INVALID_HANDLE_VALUE)
	{
		MINIDUMP_EXCEPTION_INFORMATION exception{
			request.threadId, reinterpret_cast<EXCEPTION_POINTERS*>(request.exceptionPointers), TRUE};
		if (!write(process, pid, dump, MiniDumpWithThreadInfo, request.exceptionPointers ? &exception : nullptr,
				nullptr, nullptr))
		{
			error = GetLastError();
			// A live target can change while DbgHelp reads it. Retry the basic
			// thread and stack capture when an optional read was incomplete.
			if (error == HRESULT_FROM_WIN32(ERROR_PARTIAL_COPY) || error == ERROR_PARTIAL_COPY)
			{
				LARGE_INTEGER start{};
				if (SetFilePointerEx(dump, start, nullptr, FILE_BEGIN) && SetEndOfFile(dump)
					&& write(process, pid, dump, MiniDumpNormal, request.exceptionPointers ? &exception : nullptr,
						nullptr, nullptr))
				{
					error = ERROR_SUCCESS;
				}
				else
				{
					error = GetLastError();
				}
			}
		}
		CloseHandle(dump);
		if (error)
		{
			DeleteFileW(file.c_str());
		}
	}
	FreeLibrary(library);
	return error;
}

void writeThreadContext(std::FILE* log, HANDLE process, DWORD pid, DWORD threadId)
{
	const auto thread = OpenThread(THREAD_GET_CONTEXT | THREAD_SUSPEND_RESUME | THREAD_QUERY_LIMITED_INFORMATION,
		FALSE, threadId);
	if (!thread)
	{
		std::fwprintf(log, L"Audio thread context unavailable: TID %lu, open error %lu\n", threadId, GetLastError());
		return;
	}
	DWORD error = ERROR_INVALID_PARAMETER;
	CONTEXT context{};
	context.ContextFlags = CONTEXT_CONTROL;
	if (GetProcessIdOfThread(thread) == pid)
	{
		if (SuspendThread(thread) != static_cast<DWORD>(-1))
		{
			error = GetThreadContext(thread, &context) ? ERROR_SUCCESS : GetLastError();
			if (ResumeThread(thread) == static_cast<DWORD>(-1))
			{
				reportFailure(L"resuming audio thread after context capture", GetLastError());
			}
		}
		else
		{
			error = GetLastError();
		}
	}
	CloseHandle(thread);
	if (error)
	{
		std::fwprintf(log, L"Audio thread context unavailable: TID %lu, capture error %lu\n", threadId, error);
		return;
	}
#ifdef _WIN64
	const auto address = static_cast<std::uintptr_t>(context.Rip);
	const auto stack = static_cast<std::uintptr_t>(context.Rsp);
#else
	const auto address = static_cast<std::uintptr_t>(context.Eip);
	const auto stack = static_cast<std::uintptr_t>(context.Esp);
#endif
	std::fwprintf(log, L"Audio thread context at report capture: TID %lu, IP %p, SP %p\n",
		threadId, reinterpret_cast<void*>(address), reinterpret_cast<void*>(stack));
	MEMORY_BASIC_INFORMATION region{};
	wchar_t modulePath[32768];
	if (VirtualQueryEx(process, reinterpret_cast<void*>(address), &region, sizeof(region))
		&& region.Type == MEM_IMAGE
		&& K32GetModuleFileNameExW(process, static_cast<HMODULE>(region.AllocationBase), modulePath, ARRAYSIZE(modulePath)))
	{
		const auto offset = address - reinterpret_cast<std::uintptr_t>(region.AllocationBase);
		std::fwprintf(log, L"Audio thread instruction module: %ls + 0x%llX\n",
			modulePath, static_cast<unsigned long long>(offset));
	}
}

int writeReport(HANDLE process, DWORD pid, const Request& request, const wchar_t* reason = nullptr,
	const wchar_t* stage = nullptr, const wchar_t* audioDetail = nullptr, unsigned long long sequence = 0,
	const wchar_t* queueSnapshot = nullptr, DWORD audioThread = 0)
{
	const std::filesystem::path directory(request.directory.data());
	std::error_code error;
	std::filesystem::create_directories(directory, error);
	if (error)
	{
		reportFailure(L"creating report directory", static_cast<DWORD>(error.value()));
		return 5;
	}
	const auto* name = request.pluginName.front() ? request.pluginName.data() : request.processLabel.data();
	const auto stem = directory / (std::wstring(name) + L'_' + timestamp());
	const auto logPath = stem.native() + L".log";
	const auto dumpPath = stem.native() + L".dmp";
	std::FILE* log = nullptr;
	if (const auto openError = _wfopen_s(&log, logPath.c_str(), L"wx, ccs=UTF-8"))
	{
		reportFailure(L"opening report log", static_cast<DWORD>(openError));
		return 6;
	}
	std::fwprintf(log, L"Process: %ls\nPID: %lu\nReporter PID: %lu\nReporter bitness: %u\n",
		request.processLabel.data(), pid, GetCurrentProcessId(), static_cast<unsigned>(sizeof(void*) * 8));
	if (reason)
	{
		std::fwprintf(log, L"Reason: %ls\nStage: %ls\nNative code: 0x%08lX\n", reason, stage,
			request.exceptionCode);
		if (audioDetail)
		{
			std::fwprintf(log, L"Audio queue: %ls\nBlock sequence: %llu\n", audioDetail, sequence);
		}
	}
	else
	{
		std::fwprintf(log, L"Thread: %lu\nException: 0x%08lX\nAddress: %p\n", request.threadId,
			request.exceptionCode, reinterpret_cast<void*>(request.exceptionAddress));
	}
	if (queueSnapshot)
	{
		std::fwprintf(log, L"%ls", queueSnapshot);
	}
	if (request.pluginPath.front())
	{
		std::fwprintf(log, L"Plugin: %ls\n", request.pluginPath.data());
	}
	std::fflush(log);
	if (!process)
	{
		std::fwprintf(log, L"Minidump unavailable: sandbox already exited or could not be opened.\n");
		if (std::fclose(log) != 0)
		{
			reportFailure(L"closing report log", static_cast<DWORD>(errno));
			return 6;
		}
		return 0;
	}
	if (audioThread)
	{
		writeThreadContext(log, process, pid, audioThread);
		std::fflush(log);
	}
	const auto dumpError = writeDump(dumpPath, process, pid, request);
	std::fwprintf(log, L"Minidump write code: %lu\n", static_cast<unsigned long>(dumpError));
	if (std::fclose(log) != 0)
	{
		reportFailure(L"closing report log", static_cast<DWORD>(errno));
		return 6;
	}
	return dumpError ? 7 : 0;
}

unsigned long long parseNumber(const wchar_t* argument)
{
	wchar_t* end = nullptr;
	const auto value = std::wcstoull(argument, &end, 10);
	return end != argument && !*end ? value : 0;
}
}

int wmain(int argc, wchar_t** argv)
{
	if ((argc == 8 || (argc >= 10 && argc <= 12)) && std::wcscmp(argv[1], L"--supervisor") == 0)
	{
		using namespace lmms::windowsCrashReport;
		const auto pid = parseNumber(argv[2]);
		const auto code = parseNumber(argv[5]);
		const auto audioThread = argc == 12 ? parseNumber(argv[11]) : 0;
		const std::wstring_view reason(argv[3]);
		if (pid > MAXDWORD || code > MAXDWORD || audioThread > MAXDWORD
			|| (reason != L"timeout" && reason != L"unexpected exit" && reason != L"supervisor termination"))
		{
			return 2;
		}
		Request request;
		const std::wstring_view directory(argv[6]);
		if (directory.empty() || directory.size() >= request.directory.size())
		{
			return 2;
		}
		detail::copyField(request.directory, directory);
		detail::copyField(request.processLabel, L"VST");
		detail::copyField(request.pluginPath, argv[7]);
		auto name = std::filesystem::path(argv[7]).stem().wstring();
		detail::sanitizeName(name);
		detail::copyField(request.pluginName, name);
		request.exceptionCode = static_cast<DWORD>(code);
		const auto target = static_cast<DWORD>(pid);
		const auto process = reason != L"unexpected exit" && target
			? OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, target) : nullptr;
		if (!process && reason != L"unexpected exit" && target)
		{
			reportFailure(L"opening supervised sandbox for dump", GetLastError());
		}
		const auto result = writeReport(process, target, request, argv[3], argv[4],
			argc >= 10 ? argv[8] : nullptr, argc >= 10 ? parseNumber(argv[9]) : 0,
			argc >= 11 ? argv[10] : nullptr, static_cast<DWORD>(audioThread));
		if (process)
		{
			CloseHandle(process);
		}
		return result;
	}
	if (argc != 3)
	{
		return 2;
	}
	const auto pid = parseNumber(argv[1]);
	const auto address = parseNumber(argv[2]);
	if (!pid || pid > std::numeric_limits<DWORD>::max()
		|| !address || address > std::numeric_limits<std::uintptr_t>::max())
	{
		return 2;
	}
	const auto process = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, static_cast<DWORD>(pid));
	if (!process)
	{
		reportFailure(L"opening crashed process", GetLastError());
		return 3;
	}
	Request request;
	SIZE_T bytesRead = 0;
	const auto read = ReadProcessMemory(
		process, reinterpret_cast<const void*>(static_cast<std::uintptr_t>(address)), &request, sizeof(request), &bytesRead);
	const bool valid = read && bytesRead == sizeof(request) && request.directory.back() == 0
		&& request.processLabel.back() == 0 && request.pluginName.back() == 0 && request.pluginPath.back() == 0;
	if (!valid)
	{
		reportFailure(L"reading exception report request", read ? ERROR_INVALID_DATA : GetLastError());
	}
	const auto result = valid ? writeReport(process, static_cast<DWORD>(pid), request) : 4;
	CloseHandle(process);
	return result;
}
