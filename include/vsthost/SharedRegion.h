#ifndef LMMS_VSTHOST_SHARED_REGION_H
#define LMMS_VSTHOST_SHARED_REGION_H
#include <cstdint>
#include <span>
#include <string>
#include <windows.h>

namespace lmms::vsthost
{
class SharedRegion
{
public:
	~SharedRegion() { close(); }
	SharedRegion() = default;
	SharedRegion(const SharedRegion&) = delete;
	SharedRegion& operator=(const SharedRegion&) = delete;
	bool create(const std::wstring& name, std::uint32_t bytes) noexcept
	{
		if (m_mapping || !bytes || bytes > MaxBytes) { return false; }
		const auto mapping = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, bytes, name.c_str());
		if (!mapping) { return false; }
		if (GetLastError() == ERROR_ALREADY_EXISTS) { CloseHandle(mapping); return false; }
		return bind(mapping, bytes);
	}
	bool attach(const std::wstring& name, std::uint32_t bytes) noexcept
	{
		if (m_mapping || !bytes || bytes > MaxBytes) { return false; }
		const auto mapping = OpenFileMappingW(FILE_MAP_ALL_ACCESS, FALSE, name.c_str());
		return mapping && bind(mapping, bytes);
	}
	std::span<std::uint8_t> bytes() noexcept { return {m_data, m_size}; }
	void close() noexcept
	{
		if (m_data) { UnmapViewOfFile(m_data); }
		if (m_mapping) { CloseHandle(m_mapping); }
		m_data = nullptr; m_mapping = nullptr; m_size = 0;
	}
private:
	static constexpr std::uint32_t MaxBytes = 64 * 1024 * 1024;
	bool bind(HANDLE mapping, std::uint32_t bytes) noexcept
	{
		const auto data = static_cast<std::uint8_t*>(MapViewOfFile(mapping, FILE_MAP_ALL_ACCESS, 0, 0, bytes));
		if (!data) { CloseHandle(mapping); return false; }
		m_mapping = mapping; m_data = data; m_size = bytes; return true;
	}
	HANDLE m_mapping = nullptr;
	std::uint8_t* m_data = nullptr;
	std::uint32_t m_size = 0;
};
} // namespace lmms::vsthost
#endif
