#include "SVSWaveform.h"
#include <algorithm>
namespace lmms::svs {
void Waveform::build(const std::vector<float>& stereo)
{
	m_levels.clear();
	if (stereo.empty())
		return;
	auto& base = m_levels.emplace_back();
	base.resize((stereo.size() / 2 + 63) / 64);
	for (std::size_t frame = 0; frame < stereo.size() / 2; ++frame)
	{
		auto& peak = base[frame / 64];
		const auto low = std::min(stereo[frame * 2], stereo[frame * 2 + 1]),
				   high = std::max(stereo[frame * 2], stereo[frame * 2 + 1]);
		if (frame % 64 == 0)
			peak = {low, high};
		else
		{
			peak.minimum = std::min(peak.minimum, low);
			peak.maximum = std::max(peak.maximum, high);
		}
	}
	while (m_levels.back().size() > 1)
	{
		const auto& previous = m_levels.back();
		std::vector<WaveformPeak> next((previous.size() + 1) / 2);
		for (std::size_t i = 0; i < next.size(); ++i)
		{
			next[i] = previous[i * 2];
			if (i * 2 + 1 < previous.size())
			{
				next[i].minimum = std::min(next[i].minimum, previous[i * 2 + 1].minimum);
				next[i].maximum = std::max(next[i].maximum, previous[i * 2 + 1].maximum);
			}
		}
		m_levels.push_back(std::move(next));
	}
}
WaveformPeak Waveform::peak(std::size_t first, std::size_t end) const
{
	if (m_levels.empty() || end <= first)
		return {};
	std::size_t level = 0, block = 64;
	while (level + 1 < m_levels.size() && block * 2 <= end - first)
	{
		++level;
		block *= 2;
	}
	const auto& bins = m_levels[level];
	auto from = first / block, to = std::min(bins.size(), (end + block - 1) / block);
	if (from >= to)
		return {};
	auto result = bins[from];
	for (auto i = from + 1; i < to; ++i)
	{
		result.minimum = std::min(result.minimum, bins[i].minimum);
		result.maximum = std::max(result.maximum, bins[i].maximum);
	}
	return result;
}
std::size_t Waveform::bytes() const
{
	std::size_t result = 0;
	for (const auto& level : m_levels)
		result += level.size() * sizeof(WaveformPeak);
	return result;
}
}
