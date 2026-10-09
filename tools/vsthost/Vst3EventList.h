#ifndef LMMS_VSTHOST_VST3_EVENT_LIST_H
#define LMMS_VSTHOST_VST3_EVENT_LIST_H
#include "pluginterfaces/base/funknownimpl.h"
#include "pluginterfaces/vst/ivstevents.h"
#include <array>

namespace lmms::vsthost {
class Vst3EventList final
	: public Steinberg::U::ImplementsNonDestroyable<Steinberg::U::Directly<Steinberg::Vst::IEventList>>
{
public:
	void reset(Steinberg::int32 frames) noexcept
	{
		m_count = 0;
		m_frames = frames;
		m_failed = false;
	}
	bool failed() const noexcept { return m_failed; }
	Steinberg::int32 PLUGIN_API getEventCount() override { return m_count; }
	Steinberg::tresult PLUGIN_API getEvent(Steinberg::int32 index, Steinberg::Vst::Event& event) override
	{
		if (index < 0 || index >= m_count)
		{
			return Steinberg::kInvalidArgument;
		}
		event = m_events[index];
		return Steinberg::kResultOk;
	}
	Steinberg::tresult PLUGIN_API addEvent(Steinberg::Vst::Event& event) override
	{
		using Event = Steinberg::Vst::Event;
		if (m_count == 512 || event.sampleOffset < 0 || event.sampleOffset >= m_frames || event.busIndex < 0
			|| event.busIndex >= 32
			|| (event.type != Event::kNoteOnEvent && event.type != Event::kNoteOffEvent
				&& event.type != Event::kPolyPressureEvent))
		{
			m_failed = true;
			return Steinberg::kInvalidArgument;
		}
		m_events[m_count++] = event;
		return Steinberg::kResultOk;
	}

private:
	std::array<Steinberg::Vst::Event, 512> m_events;
	Steinberg::int32 m_count = 0, m_frames = 0;
	bool m_failed = false;
};
}
#endif
