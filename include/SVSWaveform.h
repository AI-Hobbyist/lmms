#ifndef LMMS_SVS_WAVEFORM_H
#define LMMS_SVS_WAVEFORM_H
#include <vector>
#include <cstddef>
namespace lmms::svs {
struct WaveformPeak
{
	float minimum = 0, maximum = 0;
};
class Waveform
{
public:
	void build(const std::vector<float>& stereo);
	WaveformPeak peak(std::size_t firstFrame, std::size_t endFrame) const;
	std::size_t bytes() const;

private:
	std::vector<std::vector<WaveformPeak>> m_levels;
};
}
#endif
