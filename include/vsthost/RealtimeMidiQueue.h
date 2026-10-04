#ifndef LMMS_VSTHOST_REALTIME_MIDI_QUEUE_H
#define LMMS_VSTHOST_REALTIME_MIDI_QUEUE_H

#include <array>
#include <atomic>
#include <cstdint>

namespace lmms::vsthost
{
// Bounded multiple-producer/single-consumer queue. Producers never allocate,
// acquire a mutex or wait for another producer to publish a reserved cell.
class RealtimeMidiQueue
{
public:
	static constexpr std::uint32_t Capacity = 512;
	struct Event { std::uint64_t generation; std::array<std::uint32_t, 5> values; };
	RealtimeMidiQueue()
	{ for (std::uint32_t i = 0; i < Capacity; ++i) { m_cells[i].sequence.store(i); } }
	bool push(const Event& event) noexcept
	{
		auto position = m_write.load(std::memory_order_relaxed);
		for (unsigned attempt = 0; attempt < 8; ++attempt)
		{
			auto& cell = m_cells[position & (Capacity - 1)];
			const auto sequence = cell.sequence.load(std::memory_order_acquire);
			if (sequence < position) { return false; }
			if (sequence == position && m_write.compare_exchange_weak(position, position + 1, std::memory_order_relaxed))
			{ cell.event = event; cell.sequence.store(position + 1, std::memory_order_release); return true; }
			position = m_write.load(std::memory_order_relaxed);
		}
		return false;
	}
	// Control barriers may snapshot the reservation boundary and wait for cells
	// below it to publish. readPosition requires exclusive consumer ownership;
	// producers after writePosition remain queued for the next audio block.
	std::uint64_t writePosition() const noexcept { return m_write.load(std::memory_order_acquire); }
	std::uint64_t readPosition() const noexcept { return m_read; }
	bool pop(Event& event) noexcept
	{
		auto& cell = m_cells[m_read & (Capacity - 1)];
		if (cell.sequence.load(std::memory_order_acquire) != m_read + 1) { return false; }
		event = cell.event;
		cell.sequence.store(m_read + Capacity, std::memory_order_release); ++m_read; return true;
	}
private:
	struct Cell { std::atomic<std::uint64_t> sequence{0}; Event event{}; };
	std::array<Cell, Capacity> m_cells;
	std::atomic<std::uint64_t> m_write{0};
	std::uint64_t m_read = 0;
};
} // namespace lmms::vsthost
#endif
