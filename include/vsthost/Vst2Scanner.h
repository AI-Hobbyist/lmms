#ifndef LMMS_VSTHOST_VST2_SCANNER_H
#define LMMS_VSTHOST_VST2_SCANNER_H

#include "HostSession.h"
#include "LegacyMessageCodec.h"
#include "Vst2Messages.h"
#include <charconv>
#include <unordered_set>

namespace lmms::vsthost {
struct Vst2ScanEntry
{
	std::uint32_t id;
	std::string name;
	std::uint32_t flags = 0;
};
struct Vst2ScanResult
{
	Error error = Error::None;
	bool cancelled = false;
	ProcessSupervisor::Fault fault{};
	bool shell = false;
	std::string vendor, version;
	std::vector<Vst2ScanEntry> entries;
};

// Control-worker API. Native entry, enumeration and close all run in a disposable
// helper Job; the DAW only parses a bounded, validated wire result.
inline Vst2ScanResult scanVst2(const HostSession::Configuration& config, const std::string& path,
	DWORD timeoutMs = 15000, std::stop_token stop = {})
{
	Vst2ScanResult result;
	if (stop.stop_requested())
	{
		result.cancelled = true;
		result.error = Error::InvalidState;
		return result;
	}
	HostSession session;
	std::stop_callback cancellation(stop, [&session] { session.cancel(); });
	result.error = session.open(config).get().error;
	if (result.error == Error::None)
	{
		std::vector<std::uint8_t> packet;
		const std::array<LegacyCommand, 1> commands{{{IdVstScanPlugin, {path}}}};
		result.error = encodeLegacy(commands, packet);
		if (result.error == Error::None)
		{
			const auto reply = session.request(MessageType::Scan, std::move(packet), timeoutMs).get();
			result.error = reply.error;
			std::vector<LegacyCommand> decoded;
			if (result.error == Error::None)
			{
				result.error = decodeLegacy(reply.payload, decoded);
			}
			const LegacyCommand* entries = nullptr;
			for (const auto& command : decoded)
			{
				if (command.id == IdVstShellEntries)
				{
					if (entries)
					{
						result.error = Error::InvalidMessage;
						break;
					}
					entries = &command;
				}
			}
			auto parse = [](const std::string& text, std::uint32_t& value) {
				const auto converted = std::from_chars(text.data(), text.data() + text.size(), value);
				return converted.ec == std::errc{} && converted.ptr == text.data() + text.size();
			};
			std::uint32_t count = 0;
			if (result.error == Error::None
				&& (!entries || entries->arguments.size() < 2 || !parse(entries->arguments[0], count) || !count
					|| count > 4096
					|| (entries->arguments.size() != 2 + 2 * count && entries->arguments.size() != 4 + 2 * count
						&& entries->arguments.size() != 4 + 3 * count)
					|| (entries->arguments[1] != "0" && entries->arguments[1] != "1")))
			{
				result.error = Error::InvalidMessage;
			}
			if (result.error == Error::None)
			{
				result.shell = entries->arguments[1] == "1";
				const auto stride = entries->arguments.size() == 4 + 3 * count ? 3u : 2u;
				if (entries->arguments.size() == 4 + stride * count)
				{
					const auto& vendor = entries->arguments[2 + stride * count];
					const auto& version = entries->arguments[3 + stride * count];
					std::int32_t number = 0;
					const auto parsed = std::from_chars(version.data(), version.data() + version.size(), number);
					if (vendor.size() > 63 || vendor.find('\0') != std::string::npos || parsed.ec != std::errc{}
						|| parsed.ptr != version.data() + version.size())
					{
						result.error = Error::InvalidMessage;
					}
					else
					{
						result.vendor = vendor;
						result.version = std::to_string(number);
					}
				}
				std::unordered_set<std::uint32_t> seen;
				if (!result.shell && count != 1)
				{
					result.error = Error::InvalidMessage;
				}
				for (std::uint32_t i = 0; result.error == Error::None && i < count; ++i)
				{
					std::uint32_t id = 0;
					const auto& name = entries->arguments[3 + stride * i];
					if (name.empty() || name.size() > 63 || name.find('\0') != std::string::npos)
					{
						result.error = Error::InvalidMessage;
						break;
					}
					if (!parse(entries->arguments[2 + stride * i], id) || (result.shell && !id)
						|| !seen.insert(id).second)
					{
						result.error = Error::InvalidMessage;
						break;
					}
					std::uint32_t flags = 0;
					if (stride == 3 && !parse(entries->arguments[4 + stride * i], flags))
					{
						result.error = Error::InvalidMessage;
						break;
					}
					result.entries.push_back({id, entries->arguments[3 + stride * i], flags});
				}
			}
		}
	}
	result.fault = session.fault();
	const auto closed = session.close().get();
	if (result.error == Error::None && closed.error != Error::None)
	{
		result.error = closed.error;
		result.fault = session.fault();
	}
	result.cancelled = stop.stop_requested();
	if (result.cancelled && result.error == Error::None)
	{
		result.error = Error::InvalidState;
	}
	if (result.error != Error::None)
	{
		result.entries.clear();
		result.vendor.clear();
		result.version.clear();
	}
	return result;
}
} // namespace lmms::vsthost
#endif
