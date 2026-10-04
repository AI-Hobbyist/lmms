#ifndef LMMS_VSTHOST_VST3_COMPONENT_HANDLER_H
#define LMMS_VSTHOST_VST3_COMPONENT_HANDLER_H
#include "pluginterfaces/base/funknownimpl.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"
#include "vsthost/RealtimeMidiQueue.h"
#include <bit>
#include <cmath>

namespace lmms::vsthost
{
class Vst3ComponentHandler final : public Steinberg::U::Implements<Steinberg::U::Directly<Steinberg::Vst::IComponentHandler>>
{
public:
	Steinberg::tresult PLUGIN_API beginEdit(Steinberg::Vst::ParamID id) override { return edit(0, id, 0); }
	Steinberg::tresult PLUGIN_API performEdit(Steinberg::Vst::ParamID id, Steinberg::Vst::ParamValue value) override
	{ return edit(1, id, value); }
	Steinberg::tresult PLUGIN_API endEdit(Steinberg::Vst::ParamID id) override { return edit(2, id, 0); }
	Steinberg::tresult PLUGIN_API restartComponent(Steinberg::int32 flags) override
	{ m_restart.fetch_or(static_cast<std::uint32_t>(flags), std::memory_order_release); return Steinberg::kResultOk; }
	bool pop(RealtimeMidiQueue::Event& event) noexcept { return m_edits.pop(event); }
	bool failed() const noexcept { return m_failed.load(std::memory_order_acquire); }
	std::uint32_t takeRestart() noexcept { return m_restart.exchange(0, std::memory_order_acq_rel); }
	struct HostSetter
	{
		explicit HostSetter(Vst3ComponentHandler* handler) noexcept : previous(s_suppressed) { s_suppressed = handler; }
		~HostSetter() { s_suppressed = previous; }
		HostSetter(const HostSetter&) = delete;
		HostSetter& operator=(const HostSetter&) = delete;
		Vst3ComponentHandler* previous;
	};
private:
	Steinberg::tresult edit(std::uint32_t phase, Steinberg::Vst::ParamID id, double value) noexcept
	{
		// Host setters use this on the helper owner thread; native GUI callbacks
		// on other threads retain their edits via the bounded MPSC queue.
		if (s_suppressed == this) { return Steinberg::kResultOk; }
		if (!std::isfinite(value) || value < 0 || value > 1) { m_failed.store(true); return Steinberg::kInvalidArgument; }
		const auto bits = std::bit_cast<std::uint64_t>(value);
		if (!m_edits.push({0, {phase, id, static_cast<std::uint32_t>(bits), static_cast<std::uint32_t>(bits >> 32), 0}}))
		{ m_failed.store(true, std::memory_order_release); return Steinberg::kResultFalse; }
		return Steinberg::kResultOk;
	}
	RealtimeMidiQueue m_edits;
	std::atomic<std::uint32_t> m_restart{0};
	std::atomic<bool> m_failed{false};
	inline static thread_local Vst3ComponentHandler* s_suppressed = nullptr;
};
} // namespace lmms::vsthost
#endif
