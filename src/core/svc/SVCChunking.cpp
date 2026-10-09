#include "SVCChunking.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace lmms::svc {
QString ChunkConfig::validate() const
{
	if (!std::isfinite(silenceThresholdDbfs) || silenceThresholdDbfs < SilenceMinimum
		|| silenceThresholdDbfs > SilenceMaximum)
	{
		return "Silence threshold must be finite and between -120 and 0 dBFS";
	}
	if (!std::isfinite(lengthThresholdSeconds) || !std::isfinite(forcedChunkSeconds) || lengthThresholdSeconds <= 0
		|| forcedChunkSeconds <= 0)
	{
		return "Chunk lengths must be finite and positive";
	}
	if (forcedChunkSeconds > lengthThresholdSeconds)
	{
		return "Forced chunk length must not exceed the length threshold";
	}
	return {};
}

std::vector<Segment> segmentAudio(const float* source, uint64_t frames, uint32_t channels, uint32_t sampleRate,
	const ChunkConfig& config, const AudioLimits& limits)
{
	const auto invalid = config.validate();
	if (!invalid.isEmpty()) { throw std::invalid_argument(invalid.toStdString()); }
	if (!source || !frames || !channels || channels > 64 || sampleRate < 100 || sampleRate > 384000
		|| frames > static_cast<uint64_t>(std::numeric_limits<int64_t>::max()) / channels
		|| !std::isfinite(limits.minimumSeconds) || !std::isfinite(limits.maximumSeconds) || limits.minimumSeconds <= 0
		|| limits.maximumSeconds < limits.minimumSeconds || limits.maximumBytes < 44
		|| limits.maximumSeconds * sampleRate > std::numeric_limits<int64_t>::max())
	{
		throw std::invalid_argument("Invalid source format or backend limits");
	}
	const auto window = std::max<uint64_t>(1, std::llround(0.020 * sampleRate));
	const auto hop = std::max<uint64_t>(1, std::llround(0.010 * sampleRate));
	const auto minimumSilence = std::max<uint64_t>(1, std::llround(0.100 * sampleRate));
	const auto minimumInput = std::max<uint64_t>(1, std::ceil(limits.minimumSeconds * sampleRate));
	const auto context = std::max<uint64_t>(1, std::llround(0.010 * sampleRate));
	if (config.forcedChunkSeconds * sampleRate < 1)
	{
		throw std::invalid_argument("Forced chunk length is shorter than one source sample");
	}

	std::vector<uint64_t> boundaries{0};
	std::vector<double> squares(channels, 0);
	uint64_t currentStart = 0;
	uint64_t currentEnd = 0;
	uint64_t silenceStart = 0;
	uint64_t silenceEnd = 0;
	bool silent = false;
	auto finishSilence = [&]() {
		if (silent && silenceEnd - silenceStart >= minimumSilence && silenceStart > 0 && silenceEnd < frames)
		{
			boundaries.push_back(silenceStart + (silenceEnd - silenceStart) / 2);
		}
		silent = false;
	};
	for (uint64_t position = 0; position < frames; position += std::min(hop, frames - position))
	{
		const auto end = position + std::min(window, frames - position);
		for (; currentStart < position; ++currentStart)
		{
			for (uint32_t channel = 0; channel < channels; ++channel)
			{
				const double value = source[currentStart * channels + channel];
				squares[channel] -= value * value;
			}
		}
		for (; currentEnd < end; ++currentEnd)
		{
			for (uint32_t channel = 0; channel < channels; ++channel)
			{
				const double value = source[currentEnd * channels + channel];
				if (!std::isfinite(value)) { throw std::invalid_argument("Nonfinite source sample"); }
				squares[channel] += value * value;
			}
		}
		const double peakRms = std::sqrt(
			std::max(0.0, *std::max_element(squares.begin(), squares.end())) / static_cast<double>(end - position));
		const double level = 20 * std::log10(std::max(peakRms, 1e-12));
		if (level <= config.silenceThresholdDbfs)
		{
			if (!silent) { silenceStart = position; }
			silent = true;
			silenceEnd = end;
		}
		else
		{
			finishSilence();
		}
	}
	finishSilence();
	boundaries.push_back(frames);

	// Merge short silence splits without removing any source frames.
	for (size_t index = 1; index + 1 < boundaries.size();)
	{
		if (boundaries[index] - boundaries[index - 1] < minimumInput
			|| boundaries[index + 1] - boundaries[index] < minimumInput)
		{
			boundaries.erase(boundaries.begin() + static_cast<ptrdiff_t>(index));
		}
		else
		{
			++index;
		}
	}
	std::vector<Segment> result;
	auto append = [&](uint64_t start, uint64_t end) {
		Segment segment{
			start, end, start > context ? start - context : 0, end + std::min(context, frames - end), 0, sampleRate};
		const auto inputFrames = segment.inputEnd - segment.inputStart;
		segment.padding = minimumInput > inputFrames ? minimumInput - inputFrames : 0;
		if (static_cast<double>(segment.transmittedFrames()) / sampleRate > limits.maximumSeconds
			|| segment.transmittedFrames() > (limits.maximumBytes - 44) / (2 * channels))
		{
			throw std::invalid_argument("Configured segment exceeds discovered backend duration or byte limit");
		}
		result.push_back(segment);
	};
	for (size_t index = 1; index < boundaries.size(); ++index)
	{
		const auto first = boundaries[index - 1];
		const auto last = boundaries[index];
		if (static_cast<double>(last - first) / sampleRate <= config.lengthThresholdSeconds)
		{
			append(first, last);
			continue;
		}
		uint64_t position = first;
		uint64_t piece = 1;
		while (position < last)
		{
			const double distance = piece * config.forcedChunkSeconds * sampleRate;
			const auto next = distance >= last - first ? last : first + static_cast<uint64_t>(std::llround(distance));
			if (next <= position) { throw std::invalid_argument("Chunk rounding produced an empty segment"); }
			append(position, next);
			position = next;
			++piece;
		}
	}
	return result;
}
} // namespace lmms::svc
