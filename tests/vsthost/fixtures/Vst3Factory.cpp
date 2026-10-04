#include "pluginterfaces/base/ipluginbase.h"
#include <atomic>
#include <cstring>
#include <windows.h>

#ifndef VST3_FACTORY_FAULT
#define VST3_FACTORY_FAULT 0
#endif
using namespace Steinberg;
#ifdef VST3_FACTORY_NATIVE
FUnknown* createVst3NativeFixture(const TUID cid);
#endif
namespace
{
const TUID ids[3] = {
	INLINE_UID(0xf1020304, 0xabcdef01, 0x13572468, 0x98765432),
	INLINE_UID(0x01000200, 0x76543210, 0xfedcba98, 0x24681357),
	INLINE_UID(0x12345678, 0x99887766, 0x55443322, 0x11223344)
};
class Factory final : public IPluginFactory3
{
public:
	tresult PLUGIN_API queryInterface(const TUID iid, void** object) override
	{
		if (!object) { return kInvalidArgument; }
		*object = nullptr;
		if (FUnknownPrivate::iidEqual(iid, IPluginFactory3::iid) ||
			FUnknownPrivate::iidEqual(iid, IPluginFactory2::iid) ||
			FUnknownPrivate::iidEqual(iid, IPluginFactory::iid) ||
			FUnknownPrivate::iidEqual(iid, FUnknown::iid))
		{ *object = static_cast<IPluginFactory3*>(this); addRef(); return kResultOk; }
		return kNoInterface;
	}
	uint32 PLUGIN_API addRef() override { return ++m_references; }
	uint32 PLUGIN_API release() override
	{
		const auto remaining = --m_references;
		if (!remaining) { delete this; } return remaining;
	}
	tresult PLUGIN_API getFactoryInfo(PFactoryInfo* info) override
	{
		if (!info) { return kInvalidArgument; }
		*info = PFactoryInfo("LMMS", "https://lmms.io", "", 0); return kResultOk;
	}
	int32 PLUGIN_API countClasses() override
	{
#if VST3_FACTORY_FAULT == 1
		return -1;
#elif VST3_FACTORY_FAULT == 2
		return 4097;
#elif VST3_FACTORY_FAULT == 5
		RaiseException(0xe0000063, 0, 0, nullptr); return 0;
#elif VST3_FACTORY_FAULT == 6
		Sleep(INFINITE); return 0;
#else
		return 3;
#endif
	}
	tresult PLUGIN_API getClassInfo(int32, PClassInfo*) override { return kResultFalse; }
	tresult PLUGIN_API getClassInfo2(int32, PClassInfo2*) override { return kResultFalse; }
	tresult PLUGIN_API getClassInfoUnicode(int32 index, PClassInfoW* info) override
	{
		if (!info || index < 0 || index >= 3) { return kInvalidArgument; }
#if VST3_FACTORY_FAULT == 3
		if (index == 1) { return kResultFalse; }
#endif
		const auto identity = VST3_FACTORY_FAULT == 4 ? 0 : index;
		*info = PClassInfoW(ids[identity], 0x7fffffff,
			index == 2 ? "Component Controller Class" : "Audio Module Class",
			index == 0 ? u"LMMS Alpha \u97f3\u9891" : index == 1 ? u"LMMS Beta" : u"LMMS Controller",
			0, index == 0 ? "Fx|Dynamics" : index == 1 ? "Instrument|Synth" : "",
			u"LMMS \u6d4b\u8bd5", u"1.2.3", u"VST 3.8.0"); return kResultOk;
	}
	tresult PLUGIN_API createInstance(FIDString cid, FIDString iid, void** object) override
	{
		if (!object) { return kInvalidArgument; } *object = nullptr;
#ifdef VST3_FACTORY_NATIVE
		if (!cid || !iid) { return kInvalidArgument; }
		if (auto instance = createVst3NativeFixture(*reinterpret_cast<const TUID*>(cid)))
		{
			const auto result = instance->queryInterface(*reinterpret_cast<const TUID*>(iid), object);
			instance->release(); return result;
		}
#endif
		return kNoInterface;
	}
	tresult PLUGIN_API setHostContext(FUnknown*) override { return kResultOk; }
private:
	std::atomic<uint32> m_references{1};
};
}
extern "C" __declspec(dllexport) IPluginFactory* PLUGIN_API GetPluginFactory() { return new Factory; }
extern "C" __declspec(dllexport) bool PLUGIN_API InitDll() { return true; }
extern "C" __declspec(dllexport) bool PLUGIN_API ExitDll()
{
#if VST3_FACTORY_FAULT == 7
	Sleep(INFINITE);
#elif VST3_FACTORY_FAULT == 8
	RaiseException(0xe0000064, 0, 0, nullptr);
#endif
	return true;
}
