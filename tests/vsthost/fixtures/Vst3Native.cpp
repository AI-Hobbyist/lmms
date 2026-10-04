#include "public.sdk/source/vst/vstaudioeffect.h"
#include "public.sdk/source/vst/vsteditcontroller.h"
#include "pluginterfaces/vst/vstspeaker.h"
#include "pluginterfaces/base/ibstream.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"
#include <cmath>
#include <algorithm>
#include "pluginterfaces/vst/ivstevents.h"
#include "pluginterfaces/vst/ivstprocesscontext.h"
#include <cstring>
#include <windows.h>

using namespace Steinberg;
using namespace Steinberg::Vst;
namespace
{
uint32 requestedLatency = 0;
bool requestedMono = false;
const TUID ids[3] = {
	INLINE_UID(0xf1020304, 0xabcdef01, 0x13572468, 0x98765432),
	INLINE_UID(0x01000200, 0x76543210, 0xfedcba98, 0x24681357),
	INLINE_UID(0x12345678, 0x99887766, 0x55443322, 0x11223344)
};
class Processor final : public AudioEffect
{
public:
	explicit Processor(bool sample64) : m_sample64(sample64), m_gain(sample64 ? 0.75 : 0.25) {}
	tresult PLUGIN_API initialize(FUnknown* host) override
	{
		const auto result = AudioEffect::initialize(host);
		if (result != kResultOk) { return result; }
		addAudioInput(u"Main", SpeakerArr::kStereo);
		addAudioOutput(u"Main", SpeakerArr::kStereo);
		addEventInput(u"MIDI", 16);
		addEventOutput(u"MIDI", 16);
		return kResultOk;
	}
	tresult PLUGIN_API getControllerClassId(TUID cid) override { std::memcpy(cid, ids[2], 16); return kResultOk; }
	uint32 PLUGIN_API getLatencySamples() override { return m_active ? requestedLatency : 0xfffffffe; }
	tresult PLUGIN_API getBusInfo(MediaType media, BusDirection direction, int32 index, BusInfo& info) override
	{
		const auto result = AudioEffect::getBusInfo(media, direction, index, info);
		if (result == kResultOk && media == kAudio && requestedMono) { info.channelCount = 1; }
		return result;
	}
	tresult PLUGIN_API getBusArrangement(BusDirection direction, int32 index, SpeakerArrangement& arrangement) override
	{
		if (requestedMono && index == 0) { arrangement = SpeakerArr::kMono; return kResultOk; }
		return AudioEffect::getBusArrangement(direction, index, arrangement);
	}
	tresult PLUGIN_API setBusArrangements(SpeakerArrangement* inputs, int32 inputCount, SpeakerArrangement* outputs, int32 outputCount) override
	{
		if (requestedMono)
		{ return inputCount == 1 && outputCount == 1 && inputs[0] == SpeakerArr::kMono && outputs[0] == SpeakerArr::kMono ? kResultOk : kResultFalse; }
		return AudioEffect::setBusArrangements(inputs, inputCount, outputs, outputCount);
	}
	tresult PLUGIN_API canProcessSampleSize(int32 size) override
	{ return size == (m_sample64 ? kSample64 : kSample32) ? kResultOk : kResultFalse; }
	tresult PLUGIN_API setupProcessing(ProcessSetup& setup) override
	{
		if (m_active || canProcessSampleSize(setup.symbolicSampleSize) != kResultOk) { return kResultFalse; }
		const auto result = AudioEffect::setupProcessing(setup); m_setup = result == kResultOk; return result;
	}
	tresult PLUGIN_API setActive(TBool state) override
	{
		if ((state && !m_setup) || (!state && m_processing)) { return kResultFalse; }
		m_active = state; return AudioEffect::setActive(state);
	}
	tresult PLUGIN_API setProcessing(TBool state) override
	{
		if (!m_active) { return kResultFalse; } m_processing = state; return kResultOk;
	}
	tresult PLUGIN_API terminate() override
	{
		if (m_active || m_processing) { return kResultFalse; } return AudioEffect::terminate();
	}
	tresult PLUGIN_API getState(IBStream* stream) override
	{
		int32 written = 0;
		return stream && stream->write(&m_gain, sizeof(m_gain), &written) == kResultOk && written == sizeof(m_gain) ? kResultOk : kResultFalse;
	}
	tresult PLUGIN_API setState(IBStream* stream) override
	{
		double gain = 0; int32 read = 0;
		if (!stream || stream->read(&gain, sizeof(gain), &read) != kResultOk || read != sizeof(gain) || !std::isfinite(gain) || gain < 0 || gain > 1)
		{ return kResultFalse; } m_gain = gain; return kResultOk;
	}
	tresult PLUGIN_API process(ProcessData& data) override
	{
		if (!data.processContext || data.processContext->sampleRate != processSetup.sampleRate ||
			!(data.processContext->state & ProcessContext::kTempoValid)) { return kResultFalse; }
		if (data.numSamples && (data.processContext->state & ProcessContext::kRecording))
		{
			const auto& context = *data.processContext;
			if (context.projectTimeSamples != 123456 + m_contextChecks * 64 || context.continousTimeSamples != 234567 + m_contextChecks * 64 ||
				context.tempo != 137.5 || context.timeSigNumerator != 3 || context.timeSigDenominator != 8 ||
				context.cycleStartMusic != 16 || context.cycleEndMusic != 20 || context.barPositionMusic != 16.5 ||
				!(context.state & ProcessContext::kPlaying) || !(context.state & ProcessContext::kCycleActive) || context.systemTime <= 0)
			{ return kResultFalse; }
			++m_contextChecks;
		}
		if (!m_processing || data.symbolicSampleSize != (m_sample64 ? kSample64 : kSample32) ||
			data.numInputs != 1 || data.numOutputs != 1 || data.inputs[0].numChannels != (requestedMono ? 1 : 2) || data.outputs[0].numChannels != (requestedMono ? 1 : 2) ||
			data.numSamples < 0 || data.numSamples > processSetup.maxSamplesPerBlock) { return kResultFalse; }
		for (int32 frame = 0; frame < std::max(1, data.numSamples); ++frame)
		{
			if (data.inputParameterChanges)
			{
				for (int32 parameter = 0; parameter < data.inputParameterChanges->getParameterCount(); ++parameter)
				{
					auto* queue = data.inputParameterChanges->getParameterData(parameter);
					if (!queue || (queue->getParameterId() != 0xf0000101 && queue->getParameterId() != 42)) { return kResultFalse; }
					for (int32 point = 0; point < queue->getPointCount(); ++point)
					{
						int32 offset = 0; double value = 0;
						if (queue->getPoint(point, offset, value) != kResultOk) { return kResultFalse; }
						if (offset == frame && queue->getParameterId() == 0xf0000101) { m_gain = value; }
					}
				}
			}
			if (data.inputEvents)
			{
				for (int32 index = 0; index < data.inputEvents->getEventCount(); ++index)
				{
						Event event{}; if (data.inputEvents->getEvent(index, event) != kResultOk) { return kResultFalse; }
						const double expectedPpq = data.processContext->projectTimeMusic + event.sampleOffset *
							data.processContext->tempo / (60 * data.processContext->sampleRate);
						if (std::abs(event.ppqPosition - expectedPpq) > 1e-12) { return kResultFalse; }
					if (event.sampleOffset != frame) { continue; }
					if (event.type == Event::kNoteOnEvent) { m_note = event.noteOn.velocity; }
					else if (event.type == Event::kNoteOffEvent) { m_note = 0; }
					else if (event.type == Event::kPolyPressureEvent) { m_note = event.polyPressure.pressure; }
					if (data.outputEvents && data.outputEvents->addEvent(event) != kResultOk) { return kResultFalse; }
				}
			}
			if (frame >= data.numSamples) { continue; }
				for (int32 channel = 0; channel < data.outputs[0].numChannels; ++channel)
			{
				if (m_sample64) { data.outputs[0].channelBuffers64[channel][frame] = data.inputs[0].channelBuffers64[channel][frame] * m_gain + m_note; }
				else { data.outputs[0].channelBuffers32[channel][frame] = data.inputs[0].channelBuffers32[channel][frame] * static_cast<float>(m_gain) + static_cast<float>(m_note); }
			}
		}
			if (data.numSamples && data.inputEvents && data.inputEvents->getEventCount())
			{
				int32 index = -1, point = -1;
				auto* output = data.outputParameterChanges->addParameterData(0xf0000101, index);
				if (!output || output->addPoint(data.numSamples - 1, m_gain, point) != kResultOk) { return kResultFalse; }
			}
			data.outputs[0].silenceFlags = 0; return kResultOk;
	}
private:
	bool m_sample64, m_setup = false, m_active = false, m_processing = false;
	double m_gain, m_note = 0;
	int32 m_contextChecks = 0;
};
class View final : public EditorView
{
public:
	explicit View(EditController* controller) : EditorView(controller)
	{ setRect({0, 0, 240, 120}); }
	tresult PLUGIN_API isPlatformTypeSupported(FIDString type) override
	{ return type && std::strcmp(type, kPlatformTypeHWND) == 0 ? kResultOk : kResultFalse; }
	tresult PLUGIN_API attached(void* parent, FIDString type) override
	{
		if (isPlatformTypeSupported(type) != kResultOk || !parent || m_child) { return kResultFalse; }
		const auto result = EditorView::attached(parent, type);
		if (result != kResultOk) { return result; }
		m_child = CreateWindowExW(0, L"STATIC", L"LMMS VST3 fixture editor", WS_CHILD | WS_VISIBLE,
			0, 0, getRect().right, getRect().bottom, static_cast<HWND>(parent), nullptr, GetModuleHandleW(nullptr), nullptr);
		if (!m_child) { return kResultFalse; }
		SetWindowLongPtrW(m_child, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));
		m_original = reinterpret_cast<WNDPROC>(SetWindowLongPtrW(m_child, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(&procedure)));
		return kResultOk;
	}
	tresult PLUGIN_API removed() override
	{
		if (m_child)
		{
			SetWindowLongPtrW(m_child, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(m_original));
			DestroyWindow(m_child); m_child = nullptr;
		}
		return EditorView::removed();
	}
	tresult PLUGIN_API onSize(ViewRect* rectangle) override
	{
		if (!rectangle) { return kInvalidArgument; }
		if (m_child) { SetWindowPos(m_child, nullptr, 0, 0, rectangle->getWidth(), rectangle->getHeight(), SWP_NOZORDER); }
		return EditorView::onSize(rectangle);
	}
private:
	static LRESULT CALLBACK procedure(HWND window, UINT message, WPARAM wparam, LPARAM lparam)
	{
		auto* self = reinterpret_cast<View*>(GetWindowLongPtrW(window, GWLP_USERDATA));
		if (message == WM_APP + 38)
		{
			self->getController()->beginEdit(0xf0000101);
			self->getController()->EditController::setParamNormalized(0xf0000101, 0.625);
			self->getController()->performEdit(0xf0000101, 0.625);
			self->getController()->endEdit(0xf0000101);
			ViewRect resize{0, 0, 317, 173};
			return self->plugFrame && self->plugFrame->resizeView(self, &resize) == kResultOk ? 1 : 0;
		}
		return CallWindowProcW(self->m_original, window, message, wparam, lparam);
	}
	HWND m_child = nullptr;
	WNDPROC m_original = nullptr;
};
class Controller final : public EditController, public IMidiMapping
{
public:
	OBJ_METHODS(Controller, EditController)
	DEFINE_INTERFACES
		DEF_INTERFACE(IMidiMapping)
	END_DEFINE_INTERFACES(EditController)
	REFCOUNT_METHODS(EditController)
	tresult PLUGIN_API getMidiControllerAssignment(int32 bus, int16 channel, CtrlNumber number, ParamID& id) override
	{
		if (bus == 0 && channel == 3 && (number == (m_learned ? 10 : 7) || number == 128 || number == 129))
		{ id = 0xf0000101; return kResultOk; }
		return kResultFalse;
	}
	tresult PLUGIN_API initialize(FUnknown* host) override
	{
		const auto result = EditController::initialize(host);
		if (result != kResultOk) { return result; }
		parameters.addParameter(new Parameter(u"Gain", 0xf0000101, nullptr, 0.25));
		parameters.addParameter(new Parameter(u"Bypass", 42, nullptr, 0, 1, ParameterInfo::kCanAutomate | ParameterInfo::kIsBypass));
		return kResultOk;
	}
	tresult PLUGIN_API setComponentState(IBStream* stream) override
	{
		double gain = 0; int32 read = 0;
		if (!stream || stream->read(&gain, sizeof(gain), &read) != kResultOk || read != sizeof(gain)) { return kResultFalse; }
		return setParamNormalized(0xf0000101, gain);
	}
	tresult PLUGIN_API getState(IBStream* stream) override
	{
		double gain = getParamNormalized(0xf0000101); int32 written = 0;
		return stream && stream->write(&gain, sizeof(gain), &written) == kResultOk && written == sizeof(gain) ? kResultOk : kResultFalse;
	}
	tresult PLUGIN_API setState(IBStream* stream) override { return setComponentState(stream); }
	IPlugView* PLUGIN_API createView(FIDString type) override
	{ return type && std::strcmp(type, ViewType::kEditor) == 0 ? new View(this) : nullptr; }
	tresult PLUGIN_API getParameterInfo(int32 index, ParameterInfo& info) override
	{
		const auto result = EditController::getParameterInfo(m_changed ? 1 - index : index, info);
		if (result == kResultOk && m_changed && info.id == 0xf0000101)
		{
			std::fill_n(info.title, 128, char16{}); std::copy_n(u"Program Gain", 12, info.title);
			info.defaultNormalizedValue = 0.625; info.stepCount = 3;
		}
		return result;
	}
	tresult PLUGIN_API setParamNormalized(ParamID id, ParamValue value) override
	{
		const auto result = EditController::setParamNormalized(id, value);
		if (result == kResultOk && id == 42)
		{
			m_learned = value >= 0.5;
				if (value == 0.25)
			{
				m_changed = true; requestedLatency = 17;
				EditController::setParamNormalized(0xf0000101, 0.3125);
				if (getComponentHandler()) { getComponentHandler()->restartComponent(kParamValuesChanged | kParamTitlesChanged | kLatencyChanged); }
				}
				if (value == 0.125)
				{
					requestedMono = true;
					if (getComponentHandler()) { getComponentHandler()->restartComponent(kIoChanged); }
				}
				if (value == 0.0625 && getComponentHandler()) { getComponentHandler()->restartComponent(kReloadComponent); }
			if (getComponentHandler()) { getComponentHandler()->restartComponent(kMidiCCAssignmentChanged); }
		}
		if (result == kResultOk && getComponentHandler())
		{
			// Deliberate host-setter echo; the adapter must suppress this without
			// discarding genuine edits originating on another callback thread.
			getComponentHandler()->beginEdit(id); getComponentHandler()->performEdit(id, value); getComponentHandler()->endEdit(id);
		}
		return result;
	}
private:
	bool m_learned = false;
	bool m_changed = false;
};
}
FUnknown* createVst3NativeFixture(const TUID cid)
{
	if (std::memcmp(cid, ids[0], 16) == 0) { return static_cast<IComponent*>(new Processor(false)); }
	if (std::memcmp(cid, ids[1], 16) == 0) { return static_cast<IComponent*>(new Processor(true)); }
	if (std::memcmp(cid, ids[2], 16) == 0) { return static_cast<IEditController*>(new Controller); }
	return nullptr;
}
