#ifndef LMMS_VSTHOST_VST3_SCANNER_H
#define LMMS_VSTHOST_VST3_SCANNER_H
#include "vsthost/HostSession.h"
#include "vsthost/Vst3CatalogCodec.h"

namespace lmms::vsthost
{
struct Vst3ScanResult
{
	Error error = Error::None;
	ProcessSupervisor::Fault fault{};
	std::vector<Vst3Class> classes;
};

// Control-worker API. The DAW never links the SDK or loads a scanned module.
inline Vst3ScanResult scanVst3(const std::wstring& helper, const std::wstring& path, DWORD timeoutMs = 30000)
{
	Vst3ScanResult result;
	if (path.empty() || path.size() > 32767 || path.find(L'\0') != std::wstring::npos)
	{ result.error = Error::InvalidMessage; return result; }
	const auto size = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, path.data(), static_cast<int>(path.size()), nullptr, 0, nullptr, nullptr);
	if (size <= 0) { result.error = Error::InvalidMessage; return result; }
	std::vector<std::uint8_t> payload(size);
	if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, path.data(), static_cast<int>(path.size()),
		reinterpret_cast<char*>(payload.data()), size, nullptr, nullptr) != size)
	{ result.error = Error::InvalidMessage; return result; }
	HostSession session;
	auto reply = session.open({helper, {}, timeoutMs}).get();
	if (reply.error == Error::None) { reply = session.request(MessageType::Scan, std::move(payload), timeoutMs).get(); }
	result.error = reply.error;
	if (result.error == Error::None && !decodeVst3Catalog(reply.payload, result.classes))
	{ result.error = Error::InvalidMessage; }
	result.fault = session.fault();
	const auto closed = session.close().get();
	if (result.error == Error::None && closed.error != Error::None)
	{ result.error = closed.error; result.fault = session.fault(); }
	if (result.error != Error::None) { result.classes.clear(); }
	return result;
}
} // namespace lmms::vsthost
#endif
