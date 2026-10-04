// Real DLL fault injection at the native VST2 ABI boundary. Keep the frozen
// baseline fixture unchanged so regression expectations retain their meaning.
#define VSTPluginMain BaselineFixtureEntry
#include "Vst2Baseline.cpp"
#undef VSTPluginMain

namespace
{
decltype(AEffect{}.dispatcher) nativeDispatch = nullptr;
decltype(AEffect{}.processReplacing) nativeProcess = nullptr;
void fault()
{
#if VST_FAULT_HANG
	Sleep(INFINITE);
#else
	RaiseException(0xE0000042, EXCEPTION_NONCONTINUABLE, 0, nullptr);
#endif
}
intptr_t VST_CALL_CONV faultDispatch(AEffect* effect, int32_t opcode, int32_t index, intptr_t value, void* pointer, float option)
{
	if ((VST_FAULT_OPERATION == 2 && (opcode == effGetChunk || opcode == effSetChunk)) ||
		(VST_FAULT_OPERATION == 3 && opcode == effEditOpen) ||
		(VST_FAULT_OPERATION == 4 && opcode == effClose)) { fault(); }
	return nativeDispatch(effect, opcode, index, value, pointer, option);
}
void VST_CALL_CONV faultProcess(AEffect* effect, float** input, float** output, int32_t frames)
{
	if (VST_FAULT_OPERATION == 1) { fault(); }
	nativeProcess(effect, input, output, frames);
}
}
extern "C" FIXTURE_EXPORT AEffect* VST_CALL_CONV VSTPluginMain(audioMasterCallback host)
{
	if (VST_FAULT_OPERATION == 0) { fault(); }
	auto* effect = BaselineFixtureEntry(host);
	if (effect)
	{
		nativeDispatch = effect->dispatcher; effect->dispatcher = faultDispatch;
		nativeProcess = effect->processReplacing; effect->process = effect->processReplacing = faultProcess;
	}
	return effect;
}
