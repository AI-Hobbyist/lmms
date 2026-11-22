#ifndef LMMS_VSTHOST_HELPER_TRACE_H
#define LMMS_VSTHOST_HELPER_TRACE_H

#ifdef _WIN32
#include <windows.h>

#include <cstdio>
#include <cwchar>

namespace lmms::vsthost {

// Opt-in control and editor diagnostics. Never call on the audio processing path.
inline void traceHelper(const char* event) noexcept
{
	struct TraceFile
	{
		HANDLE handle = INVALID_HANDLE_VALUE;

		TraceFile()
		{
			wchar_t path[32768];
			const auto length = GetEnvironmentVariableW(L"LMMS_VST_HELPER_TRACE_DIR", path, ARRAYSIZE(path));
			if (!length || length >= ARRAYSIZE(path))
			{
				return;
			}
			const auto count = std::swprintf(path + length, ARRAYSIZE(path) - length,
				L"\\vst-helper-%lu.log", GetCurrentProcessId());
			if (count > 0 && static_cast<std::size_t>(count) < ARRAYSIZE(path) - length)
			{
				handle = CreateFileW(path, FILE_APPEND_DATA, FILE_SHARE_READ, nullptr, OPEN_ALWAYS,
					FILE_ATTRIBUTE_NORMAL, nullptr);
			}
		}

		~TraceFile()
		{
			if (handle != INVALID_HANDLE_VALUE)
			{
				CloseHandle(handle);
			}
		}
	};

	static const TraceFile file;
	if (file.handle == INVALID_HANDLE_VALUE)
	{
		return;
	}
	char line[256];
	const auto length = std::snprintf(line, sizeof(line), "%llu tid=%lu %s\n", GetTickCount64(),
		GetCurrentThreadId(), event);
	if (length > 0 && static_cast<std::size_t>(length) < sizeof(line))
	{
		DWORD written = 0;
		WriteFile(file.handle, line, static_cast<DWORD>(length), &written, nullptr);
	}
}

} // namespace lmms::vsthost

#else
namespace lmms::vsthost {
inline void traceHelper(const char*) noexcept {}
}
#endif

#endif
