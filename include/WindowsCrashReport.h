#ifndef LMMS_WINDOWS_CRASH_REPORT_H
#define LMMS_WINDOWS_CRASH_REPORT_H

#include "WindowsCrashRequest.h"

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cwchar>
#include <filesystem>
#include <string>
#include <string_view>
#include <utility>

namespace lmms::windowsCrashReport
{
namespace detail
{
inline void reportFailure(const wchar_t* operation, DWORD code) noexcept
{
	std::array<wchar_t, 2048> path{};
	const auto length = GetTempPathW(static_cast<DWORD>(path.size()), path.data());
	std::array<wchar_t, 256> line{};
	const auto count = std::swprintf(line.data(), line.size(), L"LMMS crash reporter: %ls, PID %lu, code %lu\r\n",
		operation, GetCurrentProcessId(), code);
	OutputDebugStringW(line.data());
	constexpr wchar_t filename[] = L"lmms-crash-reporter-errors.log";
	if (count <= 0 || !length || length >= path.size() - std::size(filename))
	{
		return;
	}
	std::copy_n(filename, std::size(filename), path.data() + length);
	const auto file = CreateFileW(path.data(), FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE,
		nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
	if (file != INVALID_HANDLE_VALUE)
	{
		std::array<char, 1024> utf8{};
		const auto bytes = WideCharToMultiByte(CP_UTF8, 0, line.data(), count, utf8.data(),
			static_cast<int>(utf8.size()), nullptr, nullptr);
		DWORD written = 0;
		if (bytes)
		{
			WriteFile(file, utf8.data(), static_cast<DWORD>(bytes), &written, nullptr);
		}
		CloseHandle(file);
	}
}

inline void waitForReporter(HANDLE reporter, DWORD timeout) noexcept
{
	const auto result = WaitForSingleObject(reporter, timeout);
	DWORD code = 0;
	if (result == WAIT_OBJECT_0)
	{
		if (!GetExitCodeProcess(reporter, &code))
		{
			reportFailure(L"reading reporter exit code", GetLastError());
		}
		else if (code)
		{
			reportFailure(L"reporter exited unsuccessfully", code);
		}
	}
	else
	{
		reportFailure(L"waiting for reporter", result == WAIT_FAILED ? GetLastError() : result);
	}
}

struct PluginIdentity
{
	std::wstring path;
	std::wstring name;
};

inline std::wstring directory;
inline std::wstring reporterPath;
inline std::atomic<const wchar_t*> processLabel{L"lmms_classic"};
inline LPTOP_LEVEL_EXCEPTION_FILTER previousFilter = nullptr;
inline SRWLOCK identityLock = SRWLOCK_INIT;
inline PluginIdentity plugin;
inline std::atomic_flag reporting = ATOMIC_FLAG_INIT;

inline LONG forward(EXCEPTION_POINTERS* exception) noexcept
{
	return previousFilter ? previousFilter(exception) : EXCEPTION_CONTINUE_SEARCH;
}

template<std::size_t Size>
void copyField(std::array<wchar_t, Size>& target, std::wstring_view source) noexcept
{
	std::copy_n(source.data(), (std::min)(source.size(), Size - 1), target.data());
}

inline LONG WINAPI report(EXCEPTION_POINTERS* exception) noexcept
{
	if (reporting.test_and_set(std::memory_order_relaxed))
	{
		return EXCEPTION_CONTINUE_SEARCH;
	}

	Request request;
	request.exceptionPointers = reinterpret_cast<std::uintptr_t>(exception);
	request.exceptionAddress = reinterpret_cast<std::uintptr_t>(exception->ExceptionRecord->ExceptionAddress);
	request.exceptionCode = exception->ExceptionRecord->ExceptionCode;
	request.threadId = GetCurrentThreadId();
	copyField(request.directory, directory);
	copyField(request.processLabel, processLabel.load(std::memory_order_relaxed));
	if (TryAcquireSRWLockShared(&identityLock))
	{
		copyField(request.pluginName, plugin.name);
		copyField(request.pluginPath, plugin.path);
		ReleaseSRWLockShared(&identityLock);
	}

	std::array<wchar_t, 2048> command{};
	const auto size = std::swprintf(command.data(), command.size(), L"\"%ls\" %lu %llu",
		reporterPath.c_str(), static_cast<unsigned long>(GetCurrentProcessId()),
		static_cast<unsigned long long>(reinterpret_cast<std::uintptr_t>(&request)));
	if (size > 0)
	{
		STARTUPINFOW startup{};
		startup.cb = sizeof(startup);
		PROCESS_INFORMATION reporter{};
		if (CreateProcessW(reporterPath.c_str(), command.data(), nullptr, nullptr, FALSE,
			CREATE_NO_WINDOW, nullptr, nullptr, &startup, &reporter))
		{
			// The reporter reads the exception pointers from this process.
			// Bound the wait if it cannot finish, for example on a stalled disk.
			waitForReporter(reporter.hProcess, 15000);
			CloseHandle(reporter.hThread);
			CloseHandle(reporter.hProcess);
		}
		else
		{
			reportFailure(L"starting exception reporter", GetLastError());
		}
	}
	else
	{
		reportFailure(L"formatting exception report request", ERROR_INSUFFICIENT_BUFFER);
	}
	return forward(exception);
}

inline void sanitizeName(std::wstring& name)
{
	for (auto& character : name)
	{
		if (character < 32 || std::wcschr(L"<>:\"/\\|?*", character))
		{
			character = L'_';
		}
	}
}
} // namespace detail

inline void install(const wchar_t* processName, bool sandbox)
{
	std::array<wchar_t, 32768> executable{};
	const auto size = GetModuleFileNameW(nullptr, executable.data(), static_cast<DWORD>(executable.size()));
	if (!size || size >= executable.size())
	{
		return;
	}
	auto root = std::filesystem::path(executable.data()).parent_path();
	std::error_code error;
	if (sandbox)
	{
		while (!std::filesystem::is_regular_file(root / L"lmms.exe", error))
		{
			const auto parent = root.parent_path();
			if (parent == root || parent.empty())
			{
				return;
			}
			root = parent;
		}
	}

	const auto directory = root / L"logs" / (sandbox ? L"VST" : L"lmms");
	const auto reporter = root / (sizeof(void*) == 8 ? L"LmmsCrashReporter64.exe" : L"LmmsCrashReporter32.exe");
	if (directory.native().size() >= Request{}.directory.size())
	{
		return;
	}
	detail::directory = directory.native();
	detail::reporterPath = reporter.native();
	detail::processLabel.store(processName, std::memory_order_relaxed);
	detail::previousFilter = SetUnhandledExceptionFilter(detail::report);
}

inline void setNodeWorkspace(bool node) noexcept
{
	detail::processLabel.store(node ? L"lmms_node" : L"lmms_classic", std::memory_order_relaxed);
}

inline void setPlugin(const std::wstring& path)
{
	auto name = std::filesystem::path(path).stem().wstring();
	detail::sanitizeName(name);
	detail::PluginIdentity identity{path, std::move(name)};
	AcquireSRWLockExclusive(&detail::identityLock);
	detail::plugin = std::move(identity);
	ReleaseSRWLockExclusive(&detail::identityLock);
}

inline void setPluginName(const char* name, UINT codePage)
{
	if (detail::directory.empty())
	{
		return;
	}
	const auto size = MultiByteToWideChar(codePage, MB_ERR_INVALID_CHARS, name, -1, nullptr, 0);
	if (size <= 1)
	{
		return;
	}
	std::wstring wideName(static_cast<std::size_t>(size), L'\0');
	MultiByteToWideChar(codePage, MB_ERR_INVALID_CHARS, name, -1, wideName.data(), size);
	wideName.pop_back();
	detail::sanitizeName(wideName);
	AcquireSRWLockExclusive(&detail::identityLock);
	detail::plugin.name = std::move(wideName);
	ReleaseSRWLockExclusive(&detail::identityLock);
}
} // namespace lmms::windowsCrashReport

#endif // LMMS_WINDOWS_CRASH_REPORT_H
