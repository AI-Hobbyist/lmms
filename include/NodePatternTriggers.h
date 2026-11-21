#ifndef LMMS_NODE_PATTERN_TRIGGERS_H
#define LMMS_NODE_PATTERN_TRIGGERS_H

#include <array>
#include <atomic>
#include <bitset>
#include <chrono>
#include <mutex>
#include <optional>
#include <vector>

#include "Note.h"

namespace lmms
{

class Track;

// The inspector observes one source at a time. The audio thread never waits
// for the inspector and transfers only numeric playback data.
class NodePatternTriggers
{
public:
	enum class Kind { Step, Note, Audio };
	using Clock = std::chrono::steady_clock;
	using Notes = std::bitset<NumKeys>;

	static int stepIndex(tick_t tick)
	{
		return (tick * TimePos::stepsPerBar() + TimePos::ticksPerBar() - 1) / TimePos::ticksPerBar();
	}

	struct Trigger
	{
		int pattern = -1;
		Kind kind{};
		tick_t tick = 0;
		Notes notes;
		std::optional<tick_t> playbackTick;
		Clock::time_point playbackTime;
	};

	static bool watches(const Track* track)
	{
		return s_track.load(std::memory_order_relaxed) == track;
	}

	static void watch(const Track* track)
	{
		const std::lock_guard lock(s_mutex);
		s_triggers.fill({});
		s_next = 0;
		s_track.store(track, std::memory_order_relaxed);
	}

	static void record(const Track* track, int pattern, Kind kind, tick_t tick, const Notes& notes = {})
	{
		if (!watches(track))
		{
			return;
		}
		const std::unique_lock lock(s_mutex, std::try_to_lock);
		if (!lock || !watches(track))
		{
			return;
		}
		// Retain the latest trigger per pattern and type. Simultaneous notes
		// from the same clip are already collected during scheduling.
		const Trigger trigger{pattern, kind, tick, notes};
		for (auto& entry : s_triggers)
		{
			if (entry.pattern == pattern && entry.kind == kind)
			{
				entry = trigger;
				return;
			}
		}
		if (kind == Kind::Step && notes.none())
		{
			return;
		}
		s_triggers[s_next] = trigger;
		s_next = (s_next + 1) % s_triggers.size();
	}

	static void advancePlayback(int pattern, tick_t tick)
	{
		if (!s_track.load(std::memory_order_relaxed))
		{
			return;
		}
		const std::unique_lock lock(s_mutex, std::try_to_lock);
		if (!lock)
		{
			return;
		}
		for (auto& entry : s_triggers)
		{
			if (entry.pattern == pattern && entry.kind == Kind::Note)
			{
				entry.playbackTick = tick;
				entry.playbackTime = Clock::now();
				break;
			}
		}
	}

	static std::vector<Trigger> recent(std::optional<Kind> kind)
	{
		std::array<Trigger, 128> snapshot;
		{
			const std::lock_guard lock(s_mutex);
			snapshot = s_triggers;
		}
		std::vector<Trigger> result;
		const auto now = Clock::now();
		for (auto& entry : snapshot)
		{
			if (entry.pattern < 0 || (kind && entry.kind != *kind))
			{
				continue;
			}
			if (now - entry.playbackTime >= std::chrono::milliseconds(250))
			{
				entry.playbackTick.reset();
			}
			result.push_back(entry);
		}
		return result;
	}

private:
	inline static std::atomic<const Track*> s_track{nullptr};
	inline static std::mutex s_mutex;
	static std::array<Trigger, 128> s_triggers;
	inline static std::size_t s_next = 0;
};

inline std::array<NodePatternTriggers::Trigger, 128> NodePatternTriggers::s_triggers{};

} // namespace lmms

#endif
