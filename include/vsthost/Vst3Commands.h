#ifndef LMMS_VSTHOST_VST3_COMMANDS_H
#define LMMS_VSTHOST_VST3_COMMANDS_H
#include "vsthost/Protocol.h"
#include <algorithm>
#include <bit>
#include <cmath>
#include <string>
#include <vector>

namespace lmms::vsthost {
struct Vst3Setup
{
	double sampleRate = 44100;
	std::uint32_t maxFrames = 512;
	bool offline = false;
};
// Parameter-control selector 2: IEEE754 rate, maximum frames and process mode.
inline bool encodeVst3Setup(const Vst3Setup& setup, std::vector<std::uint8_t>& bytes)
{
	bytes.clear();
	if (!std::isfinite(setup.sampleRate) || setup.sampleRate < 8000 || setup.sampleRate > 768000 || !setup.maxFrames
		|| setup.maxFrames > 4096)
	{
		return false;
	}
	bytes.resize(20);
	put(bytes, 0, 2, 4);
	put(bytes, 4, std::bit_cast<std::uint64_t>(setup.sampleRate), 8);
	put(bytes, 12, setup.maxFrames, 4);
	put(bytes, 16, setup.offline ? 1 : 0, 4);
	return true;
}
inline bool decodeVst3Setup(std::span<const std::uint8_t> bytes, Vst3Setup& setup)
{
	if (bytes.size() != 20 || get(bytes, 0, 4) != 2 || get(bytes, 16, 4) > 1)
	{
		return false;
	}
	const Vst3Setup parsed{
		std::bit_cast<double>(get(bytes, 4, 8)), static_cast<std::uint32_t>(get(bytes, 12, 4)), get(bytes, 16, 4) != 0};
	std::vector<std::uint8_t> checked;
	if (!encodeVst3Setup(parsed, checked))
	{
		return false;
	}
	setup = parsed;
	return true;
}
enum class Vst3MainLayout : std::uint32_t
{
	Default,
	Mono,
	Stereo,
	MonoToStereo
};

struct Vst3Create
{
	std::array<std::uint8_t, 16> cid{};
	double sampleRate = 44100;
	std::uint32_t maxFrames = 512;
	bool offline = false;
	std::string path;
	Vst3MainLayout layout = Vst3MainLayout::Default;
};
// v1, raw CID, IEEE754 rate, maximum frames, mode, then non-NUL UTF-8 path.
inline bool encodeVst3Create(const Vst3Create& command, std::vector<std::uint8_t>& bytes)
{
	bytes.clear();
	if (command.path.empty() || command.path.size() > 4 * 32767 || command.path.find('\0') != std::string::npos
		|| !std::isfinite(command.sampleRate) || command.sampleRate < 8000 || command.sampleRate > 768000
		|| !command.maxFrames || command.maxFrames > 4096 || command.layout > Vst3MainLayout::MonoToStereo)
	{
		return false;
	}
	const bool explicitLayout = command.layout != Vst3MainLayout::Default;
	const auto headerSize = explicitLayout ? 40 : 36;
	bytes.resize(headerSize + command.path.size());
	put(bytes, 0, explicitLayout ? 2 : 1, 4);
	std::copy(command.cid.begin(), command.cid.end(), bytes.begin() + 4);
	put(bytes, 20, std::bit_cast<std::uint64_t>(command.sampleRate), 8);
	put(bytes, 28, command.maxFrames, 4);
	put(bytes, 32, command.offline ? 1 : 0, 4);
	if (explicitLayout)
	{
		put(bytes, 36, static_cast<std::uint32_t>(command.layout), 4);
	}
	std::copy(command.path.begin(), command.path.end(), bytes.begin() + headerSize);
	return true;
}
inline bool decodeVst3Create(std::span<const std::uint8_t> bytes, Vst3Create& command)
{
	if (bytes.size() <= 36 || get(bytes, 32, 4) > 1)
	{
		return false;
	}
	const auto version = get(bytes, 0, 4);
	const auto headerSize = version == 2 ? 40u : 36u;
	if ((version != 1 && version != 2) || bytes.size() <= headerSize || bytes.size() > headerSize + 4 * 32767)
	{
		return false;
	}
	Vst3Create parsed;
	std::copy_n(bytes.begin() + 4, 16, parsed.cid.begin());
	parsed.sampleRate = std::bit_cast<double>(get(bytes, 20, 8));
	parsed.maxFrames = static_cast<std::uint32_t>(get(bytes, 28, 4));
	parsed.offline = get(bytes, 32, 4) != 0;
	if (version == 2)
	{
		parsed.layout = static_cast<Vst3MainLayout>(get(bytes, 36, 4));
		if (parsed.layout == Vst3MainLayout::Default)
		{
			return false;
		}
	}
	parsed.path.assign(reinterpret_cast<const char*>(bytes.data() + headerSize), bytes.size() - headerSize);
	std::vector<std::uint8_t> checked;
	if (!encodeVst3Create(parsed, checked))
	{
		return false;
	}
	command = std::move(parsed);
	return true;
}
} // namespace lmms::vsthost
#endif
