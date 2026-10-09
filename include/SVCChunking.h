#pragma once

#include <QString>
#include <cstdint>
#include <vector>

namespace lmms::svc {
struct ChunkConfig
{
	double silenceThresholdDbfs = -70;
	double lengthThresholdSeconds = 30;
	double forcedChunkSeconds = 10;
	static constexpr unsigned AlgorithmVersion = 1;
	static constexpr double SilenceMinimum = -120;
	static constexpr double SilenceMaximum = 0;
	static constexpr double SilenceStep = 1;
	static constexpr double LengthStep = 0.001;
	QString validate() const;
};

struct AudioLimits
{
	double minimumSeconds = 0.1;
	double maximumSeconds = 600;
	uint64_t maximumBytes = 100 * 1024 * 1024;
};

struct Segment
{
	uint64_t start = 0;
	uint64_t end = 0;
	uint64_t inputStart = 0;
	uint64_t inputEnd = 0;
	uint64_t padding = 0;
	uint32_t sampleRate = 0;
	unsigned algorithmVersion = ChunkConfig::AlgorithmVersion;
	uint64_t frames() const { return end - start; }
	uint64_t transmittedFrames() const { return inputEnd - inputStart + padding; }
};

// Analyze original interleaved source before any gain/effects. No silence is removed.
// Throws invalid_argument on invalid settings, samples or discovered limits.
std::vector<Segment> segmentAudio(const float* source, uint64_t frames, uint32_t channels, uint32_t sampleRate,
	const ChunkConfig& config = {}, const AudioLimits& limits = {});
} // namespace lmms::svc
