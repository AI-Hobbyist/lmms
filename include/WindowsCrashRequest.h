#ifndef LMMS_WINDOWS_CRASH_REQUEST_H
#define LMMS_WINDOWS_CRASH_REQUEST_H

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <array>
#include <cstdint>

namespace lmms::windowsCrashReport
{
// The crashed process keeps this block and its exception context alive until
// the reporter exits. Both binaries are built for the same architecture.
struct Request
{
	std::uintptr_t exceptionPointers = 0;
	std::uintptr_t exceptionAddress = 0;
	DWORD exceptionCode = 0;
	DWORD threadId = 0;
	std::array<wchar_t, 1024> directory{};
	std::array<wchar_t, 32> processLabel{};
	std::array<wchar_t, 129> pluginName{};
	std::array<wchar_t, 1024> pluginPath{};
};
}

#endif // LMMS_WINDOWS_CRASH_REQUEST_H
