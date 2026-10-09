#ifndef LMMS_AUDIO_DELAY_LINE_H
#define LMMS_AUDIO_DELAY_LINE_H

#include <algorithm>
#include <cstddef>
#include <span>
#include <vector>

namespace lmms {
// One audio owner. Reserve storage while processing is quiescent; changing
// delay at a block boundary clears old history. An unsupported delay is rejected
// rather than silently truncated. Frame{} must represent silence.
template <class Frame> class AudioDelayLine
{
public:
	void prepare(std::size_t maximumDelay)
	{
		std::vector<Frame> replacement(maximumDelay);
		m_history.swap(replacement);
		m_delay = 0;
		m_position = 0;
	}
	bool setDelay(std::size_t frames) noexcept
	{
		if (frames > m_history.size())
		{
			return false;
		}
		if (frames != m_delay)
		{
			m_delay = frames;
			reset();
		}
		return true;
	}
	std::size_t delay() const noexcept { return m_delay; }
	std::size_t capacity() const noexcept { return m_history.size(); }
	void reset() noexcept
	{
		std::fill_n(m_history.begin(), m_delay, Frame{});
		m_position = 0;
	}
	// Supports disjoint buffers and exact in-place processing. Partial overlap
	// is not supported. A size error silences output without consuming history.
	bool process(std::span<const Frame> input, std::span<Frame> output) noexcept
	{
		if (input.size() != output.size())
		{
			std::fill(output.begin(), output.end(), Frame{});
			return false;
		}
		if (!m_delay)
		{
			if (input.data() != output.data())
			{
				std::copy(input.begin(), input.end(), output.begin());
			}
			return true;
		}
		for (std::size_t frame = 0; frame < input.size(); ++frame)
		{
			const auto incoming = input[frame];
			output[frame] = m_history[m_position];
			m_history[m_position] = incoming;
			if (++m_position == m_delay)
			{
				m_position = 0;
			}
		}
		return true;
	}

private:
	std::vector<Frame> m_history;
	std::size_t m_delay = 0;
	std::size_t m_position = 0;
};
}
#endif
