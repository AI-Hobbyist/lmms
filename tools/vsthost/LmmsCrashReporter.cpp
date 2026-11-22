#include "WindowsCrashRequest.h"

#include <dbghelp.h>

#include <cstdio>
#include <cwchar>
#include <filesystem>
#include <limits>

namespace
{
using lmms::windowsCrashReport::Request;

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
		if (!write(process, pid, dump, MiniDumpNormal, &exception, nullptr, nullptr))
		{
			error = GetLastError();
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

int writeReport(HANDLE process, DWORD pid, const Request& request)
{
	const std::filesystem::path directory(request.directory.data());
	std::error_code error;
	std::filesystem::create_directories(directory, error);
	if (error)
	{
		return 5;
	}
	const auto* name = request.pluginName.front() ? request.pluginName.data() : request.processLabel.data();
	const auto stem = directory / (std::wstring(name) + L'_' + timestamp());
	const auto logPath = stem.native() + L".log";
	const auto dumpPath = stem.native() + L".dmp";
	std::FILE* log = nullptr;
	if (_wfopen_s(&log, logPath.c_str(), L"wx, ccs=UTF-8") != 0)
	{
		return 6;
	}
	std::fwprintf(log, L"Process: %ls\nPID: %lu\nReporter PID: %lu\nThread: %lu\nException: 0x%08lX\nAddress: %p\n",
		request.processLabel.data(), static_cast<unsigned long>(pid), static_cast<unsigned long>(GetCurrentProcessId()),
		static_cast<unsigned long>(request.threadId),
		static_cast<unsigned long>(request.exceptionCode), reinterpret_cast<void*>(request.exceptionAddress));
	if (request.pluginPath.front())
	{
		std::fwprintf(log, L"Plugin: %ls\n", request.pluginPath.data());
	}
	std::fflush(log);
	const auto dumpError = writeDump(dumpPath, process, pid, request);
	std::fwprintf(log, L"Minidump write code: %lu\n", static_cast<unsigned long>(dumpError));
	std::fclose(log);
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
		return 3;
	}
	Request request;
	SIZE_T bytesRead = 0;
	const auto read = ReadProcessMemory(
		process, reinterpret_cast<const void*>(static_cast<std::uintptr_t>(address)), &request, sizeof(request), &bytesRead);
	const bool valid = read && bytesRead == sizeof(request) && request.directory.back() == 0
		&& request.processLabel.back() == 0 && request.pluginName.back() == 0 && request.pluginPath.back() == 0;
	const auto result = valid ? writeReport(process, static_cast<DWORD>(pid), request) : 4;
	CloseHandle(process);
	return result;
}
