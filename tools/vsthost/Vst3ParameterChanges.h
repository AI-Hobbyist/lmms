#ifndef LMMS_VSTHOST_VST3_PARAMETER_CHANGES_H
#define LMMS_VSTHOST_VST3_PARAMETER_CHANGES_H
#include "pluginterfaces/base/funknownimpl.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"
#include <array>
#include <cmath>
#include <memory>

namespace lmms::vsthost
{
// Bounded, helper-owned queues. Plug-in addPoint calls never allocate and cannot
// silently drop edits: all invalid/overflow calls set the block's failure flag.
class Vst3ParameterChanges final : public Steinberg::U::ImplementsNonDestroyable<Steinberg::U::Directly<Steinberg::Vst::IParameterChanges>>
{
public:
	static constexpr Steinberg::int32 Capacity = 512;
	Vst3ParameterChanges() : m_queues(std::make_unique<Queue[]>(Capacity)) {}
	void reset(Steinberg::int32 frames) noexcept { m_count = 0; m_points = 0; m_failed = false; m_frames = frames; }
	bool failed() const noexcept { return m_failed; }
	Steinberg::int32 PLUGIN_API getParameterCount() override { return m_count; }
	Steinberg::Vst::IParamValueQueue* PLUGIN_API getParameterData(Steinberg::int32 index) override
	{ return index >= 0 && index < m_count ? &m_queues[index] : nullptr; }
	Steinberg::Vst::IParamValueQueue* PLUGIN_API addParameterData(const Steinberg::Vst::ParamID& id, Steinberg::int32& index) override
	{
		for (index = 0; index < m_count; ++index) { if (m_queues[index].id == id) { return &m_queues[index]; } }
		if (m_count == Capacity) { m_failed = true; index = -1; return nullptr; }
		auto& queue = m_queues[m_count]; queue.id = id; queue.count = 0; queue.owner = this;
		index = m_count++; return &queue;
	}
private:
	struct Point { Steinberg::int32 offset; Steinberg::Vst::ParamValue value; };
	class Queue final : public Steinberg::U::ImplementsNonDestroyable<Steinberg::U::Directly<Steinberg::Vst::IParamValueQueue>>
	{
	public:
		Steinberg::Vst::ParamID id = 0;
		Steinberg::int32 count = 0;
		Vst3ParameterChanges* owner = nullptr;
		Steinberg::Vst::ParamID PLUGIN_API getParameterId() override { return id; }
		Steinberg::int32 PLUGIN_API getPointCount() override { return count; }
		Steinberg::tresult PLUGIN_API getPoint(Steinberg::int32 index, Steinberg::int32& offset, Steinberg::Vst::ParamValue& value) override
		{
			if (index < 0 || index >= count) { return Steinberg::kInvalidArgument; }
			offset = points[index].offset; value = points[index].value; return Steinberg::kResultOk;
		}
		Steinberg::tresult PLUGIN_API addPoint(Steinberg::int32 offset, Steinberg::Vst::ParamValue value, Steinberg::int32& index) override
		{
			index = -1;
			if (!owner || offset < 0 || offset >= owner->m_frames || !std::isfinite(value) || value < 0 || value > 1)
			{ if (owner) { owner->m_failed = true; } return Steinberg::kInvalidArgument; }
			for (Steinberg::int32 position = 0; position < count; ++position)
			{
				if (points[position].offset == offset) { points[position].value = value; index = position; return Steinberg::kResultOk; }
			}
			if (owner->m_points == Capacity || count == Capacity) { owner->m_failed = true; return Steinberg::kResultFalse; }
			index = count;
			while (index > 0 && points[index - 1].offset > offset) { points[index] = points[index - 1]; --index; }
			points[index] = {offset, value}; ++count; ++owner->m_points; return Steinberg::kResultOk;
		}
	private:
		std::array<Point, Capacity> points;
	};
	std::unique_ptr<Queue[]> m_queues;
	Steinberg::int32 m_count = 0, m_points = 0, m_frames = 0;
	bool m_failed = false;
};
} // namespace lmms::vsthost
#endif
