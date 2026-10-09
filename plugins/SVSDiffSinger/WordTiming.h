#ifndef DIFFSINGER_WORD_TIMING_H
#define DIFFSINGER_WORD_TIMING_H
#include <cstdint>
#include <stdexcept>
#include <utility>
#include <vector>
namespace diffsinger {
// OpenUtau PaddedWordDivAndDur semantics: boundaries precede real vowels;
// padding and inserted gaps are not real phones. Durations are model frames.
inline std::pair<std::vector<int64_t>, std::vector<int64_t>> wordTiming(
	const std::vector<int64_t>& durations, const std::vector<bool>& realVowels)
{
	if (durations.size() < 3 || durations.size() != realVowels.size())
	{
		throw std::runtime_error("Word timing requires matching padded phonemes and durations");
	}
	std::vector<size_t> boundaries;
	for (size_t i = 1; i + 1 < durations.size(); ++i)
	{
		if (realVowels[i])
			boundaries.push_back(i);
	}
	if (boundaries.empty())
		boundaries.push_back(durations.size() - 2);
	boundaries.push_back(durations.size());
	std::vector<int64_t> divisions, frames;
	size_t offset = 0;
	for (const auto boundary : boundaries)
	{
		divisions.push_back(int64_t(boundary - offset));
		int64_t sum = 0;
		for (; offset < boundary; ++offset)
		{
			if (durations[offset] < 0)
				throw std::runtime_error("Negative phoneme duration in word timing");
			sum += durations[offset];
		}
		frames.push_back(sum);
	}
	return {std::move(divisions), std::move(frames)};
}
}
#endif
