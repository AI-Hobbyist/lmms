#ifndef LMMS_VSTHOST_VST3_PARAMETER_CHANGES_H
#define LMMS_VSTHOST_VST3_PARAMETER_CHANGES_H
#include "pluginterfaces/base/funknownimpl.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"
#include <algorithm>
#include <bit>
#include <cmath>
#include <memory>

namespace lmms::vsthost {
// Bounded, helper-owned queues. Plug-in addPoint calls never allocate and cannot
// silently drop edits: all invalid/overflow calls set the block's failure flag.
class Vst3ParameterChanges final
	: public Steinberg::U::ImplementsNonDestroyable<Steinberg::U::Directly<Steinberg::Vst::IParameterChanges>>
{
public:
	static constexpr Steinberg::int32 Capacity = 512;
	explicit Vst3ParameterChanges(Steinberg::int32 bulkParameters = 0)
		: m_capacity(Capacity + bulkParameters)
		, m_queues(std::make_unique<Queue[]>(m_capacity))
		, m_storage(std::make_unique<Point[]>(m_capacity))
		, m_lookupSize(std::bit_ceil(static_cast<unsigned>(m_capacity) * 2))
		, m_lookup(std::make_unique<Steinberg::int32[]>(m_lookupSize))
	{
		std::fill_n(m_lookup.get(), m_lookupSize, -1);
	}
	void reset(Steinberg::int32 frames) noexcept
	{
		for (Steinberg::int32 i = 0; i < m_count; ++i)
		{
			m_lookup[m_queues[i].lookupSlot] = -1;
		}
		m_count = 0;
		m_points = 0;
		m_failed = false;
		m_frames = frames;
	}
	bool failed() const noexcept { return m_failed; }
	Steinberg::int32 bulkCapacity() const noexcept { return m_capacity - Capacity; }
	Steinberg::int32 PLUGIN_API getParameterCount() override { return m_count; }
	Steinberg::Vst::IParamValueQueue* PLUGIN_API getParameterData(Steinberg::int32 index) override
	{
		return index >= 0 && index < m_count ? &m_queues[index] : nullptr;
	}
	Steinberg::Vst::IParamValueQueue* PLUGIN_API addParameterData(
		const Steinberg::Vst::ParamID& id, Steinberg::int32& index) override
	{
		const auto mask = m_lookupSize - 1;
		auto slot = (id ^ (id >> 16)) & mask;
		while (m_lookup[slot] >= 0)
		{
			index = m_lookup[slot];
			if (m_queues[index].id == id)
			{
				return &m_queues[index];
			}
			slot = (slot + 1) & mask;
		}
		if (m_count == m_capacity)
		{
			m_failed = true;
			index = -1;
			return nullptr;
		}
		auto& queue = m_queues[m_count];
		queue.id = id;
		queue.count = 0;
		queue.first = -1;
		queue.owner = this;
		queue.lookupSlot = slot;
		index = m_count++;
		m_lookup[slot] = index;
		return &queue;
	}

private:
	struct Point
	{
		Steinberg::int32 offset;
		Steinberg::Vst::ParamValue value;
		Steinberg::int32 next;
	};
	class Queue final
		: public Steinberg::U::ImplementsNonDestroyable<Steinberg::U::Directly<Steinberg::Vst::IParamValueQueue>>
	{
	public:
		Steinberg::Vst::ParamID id = 0;
		Steinberg::int32 count = 0;
		Steinberg::int32 first = -1;
		unsigned lookupSlot = 0;
		Vst3ParameterChanges* owner = nullptr;
		Steinberg::Vst::ParamID PLUGIN_API getParameterId() override { return id; }
		Steinberg::int32 PLUGIN_API getPointCount() override { return count; }
		Steinberg::tresult PLUGIN_API getPoint(
			Steinberg::int32 index, Steinberg::int32& offset, Steinberg::Vst::ParamValue& value) override
		{
			if (index < 0 || index >= count)
			{
				return Steinberg::kInvalidArgument;
			}
			auto position = first;
			for (Steinberg::int32 i = 0; i < index; ++i)
			{
				position = owner->m_storage[position].next;
			}
			const auto& point = owner->m_storage[position];
			offset = point.offset;
			value = point.value;
			return Steinberg::kResultOk;
		}
		Steinberg::tresult PLUGIN_API addPoint(
			Steinberg::int32 offset, Steinberg::Vst::ParamValue value, Steinberg::int32& index) override
		{
			index = -1;
			if (!owner || offset < 0 || offset >= owner->m_frames || !std::isfinite(value) || value < 0 || value > 1)
			{
				if (owner)
				{
					owner->m_failed = true;
				}
				return Steinberg::kInvalidArgument;
			}
			auto* link = &first;
			Steinberg::int32 position = 0;
			while (*link >= 0)
			{
				auto& point = owner->m_storage[*link];
				if (point.offset == offset)
				{
					point.value = value;
					index = position;
					return Steinberg::kResultOk;
				}
				if (point.offset > offset)
				{
					break;
				}
				link = &point.next;
				++position;
			}
			if (owner->m_points == owner->m_capacity)
			{
				owner->m_failed = true;
				return Steinberg::kResultFalse;
			}
			owner->m_storage[owner->m_points] = {offset, value, *link};
			*link = owner->m_points++;
			index = position;
			++count;
			return Steinberg::kResultOk;
		}
	};
	const Steinberg::int32 m_capacity;
	std::unique_ptr<Queue[]> m_queues;
	std::unique_ptr<Point[]> m_storage;
	const unsigned m_lookupSize;
	std::unique_ptr<Steinberg::int32[]> m_lookup;
	Steinberg::int32 m_count = 0, m_points = 0, m_frames = 0;
	bool m_failed = false;
};
} // namespace lmms::vsthost
#endif
