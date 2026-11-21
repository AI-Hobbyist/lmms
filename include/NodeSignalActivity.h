#ifndef LMMS_NODE_SIGNAL_ACTIVITY_H
#define LMMS_NODE_SIGNAL_ACTIVITY_H

#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>

namespace lmms
{

class NodeSignalActivity
{
public:
	static bool monitoring() { return s_monitoring.load(std::memory_order_relaxed); }
	static void setMonitoring(bool enabled) { s_monitoring.store(enabled, std::memory_order_relaxed); }

	void mark(unsigned mask)
	{
		if (mask == 0)
		{
			return;
		}
		const auto time = now();
		for (unsigned side = 0; side < m_lastSignal.size(); ++side)
		{
			if ((mask & (1u << side)) != 0)
			{
				m_lastSignal[side].store(time, std::memory_order_relaxed);
			}
		}
	}

	bool active(unsigned side) const
	{
		const auto last = m_lastSignal.at(side).load(std::memory_order_relaxed);
		return last > 0 && now() - last < 120;
	}

	void updateAudio(unsigned mask)
	{
		const auto time = mask != 0 ? now() : 0;
		for (unsigned side = 0; side < m_lastSignal.size(); ++side)
		{
			m_lastSignal[side].store((mask & (1u << side)) != 0 ? time : 0,
				std::memory_order_relaxed);
		}
	}

private:
	static std::int64_t now()
	{
		return std::chrono::duration_cast<std::chrono::milliseconds>(
			std::chrono::steady_clock::now().time_since_epoch()).count();
	}

	std::array<std::atomic<std::int64_t>, 2> m_lastSignal{};
	inline static std::atomic_bool s_monitoring{false};
};

} // namespace lmms

#endif
