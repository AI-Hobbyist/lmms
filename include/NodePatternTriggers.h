#ifndef LMMS_NODE_PATTERN_TRIGGERS_H
#define LMMS_NODE_PATTERN_TRIGGERS_H

#include <algorithm>
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

class PlayHandle;
class Track;

// The inspector observes one source or the whole Pattern graph. The audio
// thread never waits for the inspector or allocates storage for these events.
class NodePatternTriggers
{
public:
	enum class Kind { Step, Note, Audio };
	using Clock = std::chrono::steady_clock;
	using Notes = std::bitset<NumKeys>;
	static constexpr int DefaultHideDelayMs = 1000;

	static void setHideDelay(int milliseconds)
	{
		s_hideDelayMs.store(milliseconds, std::memory_order_relaxed);
	}

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
		const Track* track = nullptr;
	};

	static bool watches(const Track* track)
	{
		return track && (watchingAll() || s_track.load(std::memory_order_relaxed) == track);
	}

	static bool watchingAll() { return s_all.load(std::memory_order_relaxed); }

	static void watch(const Track* track, bool all = false)
	{
		const std::lock_guard lock(s_mutex);
		for (std::size_t i = 0; i < s_used.load(std::memory_order_relaxed); ++i)
		{
			s_active[i].handle.store(nullptr, std::memory_order_relaxed);
			s_active[i].finishedAt.store(0, std::memory_order_relaxed);
		}
		s_used.store(0, std::memory_order_release);
		s_track.store(track, std::memory_order_relaxed);
		s_all.store(all, std::memory_order_relaxed);
	}

	static void record(const Track* track, int pattern, Kind kind, tick_t tick, const PlayHandle* handle,
		int key = -1)
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
		const auto used = s_used.load(std::memory_order_relaxed);
		const auto now = clockMilliseconds();
		const auto delay = s_hideDelayMs.load(std::memory_order_relaxed);
		std::size_t index = 0;
		std::optional<std::size_t> oldestFinished;
		while (index < used)
		{
			const auto& slot = s_active[index];
			if (!slot.handle.load(std::memory_order_acquire))
			{
				const auto finished = slot.finishedAt.load(std::memory_order_relaxed);
				if (finished == 0 || now - finished >= delay)
				{
					break;
				}
				if (!oldestFinished || finished < s_active[*oldestFinished].finishedAt.load(std::memory_order_relaxed))
				{
					oldestFinished = index;
				}
			}
			++index;
		}
		if (index == s_active.size())
		{
			if (!oldestFinished)
			{
				return;
			}
			index = *oldestFinished;
		}
		auto& slot = s_active[index];
		slot.finishedAt.store(0, std::memory_order_relaxed);
		slot.trigger = {pattern, kind, tick, {}, {}, {}, track};
		if (kind == Kind::Step)
		{
			slot.trigger.notes.set(0);
		}
		else if (key >= 0 && key < NumKeys)
		{
			slot.trigger.notes.set(key);
		}
		slot.handle.store(handle, std::memory_order_release);
		if (index == used)
		{
			s_used.store(used + 1, std::memory_order_release);
		}
	}

	static void finish(const PlayHandle* handle)
	{
		const auto used = s_used.load(std::memory_order_acquire);
		for (std::size_t i = 0; i < used; ++i)
		{
			if (s_active[i].handle.load(std::memory_order_relaxed) != handle)
			{
				continue;
			}
			auto* expected = handle;
			// Publish the timestamp before releasing the slot to readers.
			s_active[i].finishedAt.store(clockMilliseconds(), std::memory_order_relaxed);
			if (s_active[i].handle.compare_exchange_strong(expected, nullptr, std::memory_order_release,
				std::memory_order_relaxed))
			{
				return;
			}
		}
	}

	static void advancePlayback(int pattern, tick_t tick)
	{
		if (s_used.load(std::memory_order_relaxed) == 0)
		{
			return;
		}
		const std::unique_lock lock(s_mutex, std::try_to_lock);
		if (!lock)
		{
			return;
		}
		const auto used = s_used.load(std::memory_order_relaxed);
		const auto now = Clock::now();
		for (std::size_t i = 0; i < used; ++i)
		{
			auto& slot = s_active[i];
			auto& entry = slot.trigger;
			if (entry.pattern == pattern && entry.kind != Kind::Audio
				&& slot.handle.load(std::memory_order_relaxed))
			{
				entry.playbackTick = tick;
				entry.playbackTime = now;
			}
		}
	}

	static std::vector<Trigger> recent(std::optional<Kind> kind)
	{
		struct ObservedTrigger
		{
			Trigger trigger;
			bool active;
			Clock::rep finished;
		};
		std::vector<ObservedTrigger> snapshot;
		snapshot.reserve(s_active.size());
		const auto delay = s_hideDelayMs.load(std::memory_order_relaxed);
		const auto nowMs = clockMilliseconds();
		{
			const std::lock_guard lock(s_mutex);
			const auto used = s_used.load(std::memory_order_relaxed);
			for (std::size_t i = 0; i < used; ++i)
			{
				const auto& slot = s_active[i];
				const bool active = slot.handle.load(std::memory_order_acquire) != nullptr;
				const auto finished = slot.finishedAt.load(std::memory_order_relaxed);
				if ((active || (finished > 0 && nowMs - finished < delay))
					&& (!kind || slot.trigger.kind == *kind))
				{
					snapshot.push_back({slot.trigger, active, finished});
				}
			}
		}
		std::stable_partition(snapshot.begin(), snapshot.end(), [](const auto& item) { return item.active; });
		std::vector<ObservedTrigger> grouped;
		const auto now = Clock::now();
		for (auto& item : snapshot)
		{
			auto& entry = item.trigger;
			if (entry.kind == Kind::Note && now - entry.playbackTime >= std::chrono::milliseconds(250))
			{
				entry.playbackTick.reset();
			}
			if (entry.kind == Kind::Step && entry.playbackTick)
			{
				const auto step = (*entry.playbackTick * TimePos::stepsPerBar()) / TimePos::ticksPerBar();
				entry.notes.set(0, stepIndex(entry.tick) == step);
				entry.tick = TimePos::stepPosition(step).getTicks();
			}
			const auto match = std::find_if(grouped.begin(), grouped.end(), [&entry](const ObservedTrigger& observed) {
				const auto& row = observed.trigger;
				return row.track == entry.track && row.pattern == entry.pattern && row.kind == entry.kind;
			});
			if (match == grouped.end())
			{
				grouped.push_back(item);
			}
			else if (match->active && item.active)
			{
				match->trigger.notes |= entry.notes;
			}
			else if (!match->active && !item.active)
			{
				if (item.finished > match->finished)
				{
					*match = item;
				}
				else if (item.finished == match->finished)
				{
					match->trigger.notes |= entry.notes;
				}
			}
		}
		std::vector<Trigger> result;
		result.reserve(grouped.size());
		for (auto& group : grouped)
		{
			result.push_back(std::move(group.trigger));
		}
		return result;
	}

private:
	struct ActiveTrigger
	{
		std::atomic<const PlayHandle*> handle{nullptr};
		std::atomic<Clock::rep> finishedAt{0};
		Trigger trigger;
	};

	static Clock::rep clockMilliseconds()
	{
		return std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now().time_since_epoch()).count();
	}

	inline static std::atomic<const Track*> s_track{nullptr};
	inline static std::atomic<bool> s_all{false};
	inline static std::atomic<int> s_hideDelayMs{DefaultHideDelayMs};
	inline static std::mutex s_mutex;
	inline static std::atomic<std::size_t> s_used{0};
	static std::array<ActiveTrigger, 1024> s_active;
};

inline std::array<NodePatternTriggers::ActiveTrigger, 1024> NodePatternTriggers::s_active{};

} // namespace lmms

#endif
