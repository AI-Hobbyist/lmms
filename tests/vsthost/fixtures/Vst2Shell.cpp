// The S0 baseline stays frozen. This separate shell wraps its ordinary VST2
// implementation and rejects enumeration outside the required lifecycle.
#define VSTPluginMain BaselineShellEntry
#include "Vst2Baseline.cpp"
#undef VSTPluginMain
#include <unordered_map>
#include <cstdint>

namespace {
constexpr std::uint32_t Alpha = 0xf1020304u;
constexpr std::uint32_t Beta = 0x01000200u;
struct ShellContext
{
	audioMasterCallback host;
	unsigned next = 0;
	bool open = false;
	bool root = false;
	WNDPROC originalEditor = nullptr;
};
std::unordered_map<AEffect*, ShellContext> contexts;
LRESULT CALLBACK shellEditor(HWND window, UINT message, WPARAM word, LPARAM parameter)
{
	auto* effect = reinterpret_cast<AEffect*>(GetWindowLongPtrW(window, GWLP_USERDATA));
	auto& context = contexts.at(effect);
	if (message == WM_APP + 37)
	{
		context.host(effect, audioMasterBeginEdit, 0, 0, nullptr, 0);
		setParameter(effect, 0, 0.375f);
		context.host(effect, audioMasterAutomate, 0, 0, nullptr, 0.375f);
		setParameter(effect, 0, 0.625f);
		context.host(effect, audioMasterAutomate, 0, 0, nullptr, 0.625f);
		context.host(effect, audioMasterEndEdit, 0, 0, nullptr, 0);
		return 1;
	}
	return CallWindowProcW(context.originalEditor, window, message, word, parameter);
}
void VST_CALL_CONV shellSetParameter(AEffect* effect, int32_t index, float value)
{
	setParameter(effect, index, value);
	// Deliberate plugin echo: host-originated setters must not be recorded as UI edits.
	contexts.at(effect).host(effect, audioMasterAutomate, index, 0, nullptr, value);
}
intptr_t VST_CALL_CONV shellDispatch(
	AEffect* effect, int32_t opcode, int32_t index, intptr_t value, void* pointer, float option)
{
	auto& context = contexts.at(effect);
	if (opcode == effOpen)
	{
		context.open = true;
	}
	if (opcode == 35)
	{
		return context.root ? 10 : 1;
	}
	if (opcode == 70 && context.root)
	{
		if (!context.open)
		{
			RaiseException(0xe0000052u, 0, 0, nullptr);
		}
#if defined(VST_SHELL_SCAN_HANG)
		Sleep(INFINITE);
#elif defined(VST_SHELL_SCAN_CRASH)
		RaiseException(0xe0000053u, 0, 0, nullptr);
#elif defined(VST_SHELL_SCAN_DUPLICATE)
		text(pointer, "Duplicate");
		return static_cast<int32_t>(Alpha);
#endif
		if (context.next == 0)
		{
			++context.next;
			text(pointer, "Shell Alpha");
			return static_cast<int32_t>(Alpha);
		}
		if (context.next == 1)
		{
			++context.next;
			text(pointer, "Shell Beta");
			return static_cast<int32_t>(Beta);
		}
		return 0;
	}
	if (context.root && ((opcode == effMainsChanged && value) || opcode == effEditOpen))
	{
		RaiseException(0xe0000054u, 0, 0, nullptr);
	}
	if (opcode == effGetEffectName || opcode == effGetProductString)
	{
		return context.root									  ? text(pointer, "LMMS test shell")
			: effect->uniqueID == static_cast<int32_t>(Alpha) ? text(pointer, "Shell Alpha")
															  : text(pointer, "Shell Beta");
	}
	if (opcode == effEditOpen)
	{
		const auto result = dispatch(effect, opcode, index, value, pointer, option);
		if (auto editor = instance(effect).editor)
		{
			SetWindowLongPtrW(editor, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(effect));
			context.originalEditor = reinterpret_cast<WNDPROC>(
				SetWindowLongPtrW(editor, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(shellEditor)));
		}
		return result;
	}
	// Destroy the subclassed window while its context is still alive.
	if (opcode == effClose)
	{
		dispatch(effect, effEditClose, 0, 0, nullptr, 0);
		contexts.erase(effect);
	}
	return dispatch(effect, opcode, index, value, pointer, option);
}
}
extern "C" FIXTURE_EXPORT AEffect* VST_CALL_CONV VSTPluginMain(audioMasterCallback host)
{
	if (!host)
	{
		return nullptr;
	}
	// Called before an AEffect exists: the ID must already be available here.
	const auto selected = static_cast<std::uint32_t>(host(nullptr, audioMasterCurrentId, 0, 0, nullptr, 0));
	if (selected && selected != Alpha && selected != Beta)
	{
		return nullptr;
	}
	auto* effect = BaselineShellEntry(host);
	if (!effect)
	{
		return nullptr;
	}
	contexts.emplace(effect, ShellContext{host, 0, false, selected == 0});
	effect->dispatcher = shellDispatch;
	effect->setParameter = shellSetParameter;
	effect->uniqueID = static_cast<int32_t>(selected ? selected : 0x53484c30u);
	if (!selected)
	{
		effect->numInputs = effect->numOutputs = effect->numParams = 0;
		effect->flags = 0;
	}
	else
	{
		instance(effect).gain = selected == Alpha ? 0.25f : 0.75f;
	}
	return effect;
}
