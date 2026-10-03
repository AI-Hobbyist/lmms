#include "aeffectx.h"
#include <algorithm>
#include <cstring>
#include <array>
#ifdef _WIN32
#include <windows.h>
#endif

namespace
{
struct Instance
{
	AEffect effect{};
	float gain = 0.5f;
	int program = 0;
	std::array<VstMidiEvent, 32> events{};
	int eventCount = 0;
	float note = 0;
#ifdef _WIN32
	HWND editor = nullptr;
#endif
};
Instance& instance(AEffect* effect) { return *static_cast<Instance*>(effect->user); }
template<std::size_t Size> intptr_t text(void* pointer, const char (&value)[Size])
{
	std::memcpy(pointer, value, Size);
	return 1;
}
void VST_CALL_CONV setParameter(AEffect* effect, int32_t index, float value)
{
	if (index == 0) { instance(effect).gain = std::clamp(value, 0.0f, 1.0f); }
}
float VST_CALL_CONV getParameter(AEffect* effect, int32_t index)
{
	return index == 0 ? instance(effect).gain : 0.0f;
}
void VST_CALL_CONV process(AEffect* effect, float** inputs, float** outputs, int32_t frames)
{
	auto& plugin = instance(effect);
	int event = 0;
	for (int frame = 0; frame < frames; ++frame)
	{
		while (event < plugin.eventCount && plugin.events[event].deltaFrames <= frame)
		{
			const auto& midi = plugin.events[event++];
			const auto status = static_cast<unsigned char>(midi.midiData[0]) & 0xf0;
			if (status == 0x90) { plugin.note = static_cast<unsigned char>(midi.midiData[2]) / 127.0f; }
			if (status == 0x80) { plugin.note = 0; }
		}
		for (int channel = 0; channel < 2; ++channel)
		{ outputs[channel][frame] = inputs[channel][frame] * plugin.gain + plugin.note; }
	}
	plugin.eventCount = 0;
}
intptr_t VST_CALL_CONV dispatch(AEffect* effect, int32_t opcode, int32_t, intptr_t value, void* pointer, float)
{
	auto& plugin = instance(effect);
	switch (opcode)
	{
	case effProcessEvents:
	{
		const auto* events = static_cast<VstEvents*>(pointer);
		plugin.eventCount = std::clamp(events->numEvents, 0, 32);
		for (int i = 0; i < plugin.eventCount; ++i)
		{ plugin.events[i] = *reinterpret_cast<const VstMidiEvent*>(events->events[i]); }
		return 1;
	}
#ifdef _WIN32
	case effEditGetRect:
	{
		static const short rectangle[] = {0, 0, 120, 240};
		*static_cast<const void**>(pointer) = rectangle;
		return 1;
	}
	case effEditOpen:
		plugin.editor = CreateWindowW(L"STATIC", L"LMMS VST2 fixture", WS_CHILD | WS_VISIBLE,
			0, 0, 240, 120, static_cast<HWND>(pointer), nullptr, GetModuleHandleW(nullptr), nullptr);
		return plugin.editor != nullptr;
	case effEditClose:
		if (plugin.editor) { DestroyWindow(plugin.editor); plugin.editor = nullptr; }
		return 1;
#endif
	case effClose: delete &plugin; return 1;
	case effGetProgram: return plugin.program;
	case effSetProgram: plugin.program = static_cast<int>(value); return 1;
	case effGetChunk: *static_cast<void**>(pointer) = &plugin.gain; return sizeof(float);
	case effSetChunk:
		if (pointer && value == sizeof(float)) { std::memcpy(&plugin.gain, pointer, sizeof(float)); return 1; }
		return 0;
	case effGetEffectName: case effGetProductString: return text(pointer, "LMMS baseline");
	case effGetVendorString: return text(pointer, "LMMS tests");
	case effGetParamName: return text(pointer, "Gain");
	case effGetParamLabel: return text(pointer, "gain");
	case effGetParamDisplay: return text(pointer, "0.5");
	case effGetProgramName: case effGetProgramNameIndexed: return text(pointer, "Default");
	case effGetVstVersion: return 2400;
	case effGetVendorVersion: return 1;
	default: return 0;
	}
}
}
#ifdef _WIN32
#define FIXTURE_EXPORT __declspec(dllexport)
#else
#define FIXTURE_EXPORT __attribute__((visibility("default")))
#endif
extern "C" FIXTURE_EXPORT AEffect* VST_CALL_CONV VSTPluginMain(audioMasterCallback host)
{
	if (!host || host(nullptr, audioMasterVersion, 0, 0, nullptr, 0) == 0) { return nullptr; }
	auto* plugin = new Instance;
	auto& effect = plugin->effect;
	effect.magic = kEffectMagic;
	effect.dispatcher = dispatch;
	effect.process = effect.processReplacing = process;
	effect.setParameter = setParameter;
	effect.getParameter = getParameter;
	effect.numPrograms = 2;
	effect.numParams = 1;
	effect.numInputs = effect.numOutputs = 2;
	effect.flags = effFlagsCanReplacing | effFlagsHasEditor | 32; // program chunks
	effect.uniqueID = CCONST('L', 'M', 'B', '0');
	effect.user = plugin;
	return &effect;
}
