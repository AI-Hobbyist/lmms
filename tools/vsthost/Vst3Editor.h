#ifndef LMMS_VSTHOST_VST3_EDITOR_H
#define LMMS_VSTHOST_VST3_EDITOR_H
#include "pluginterfaces/base/funknownimpl.h"
#include "pluginterfaces/gui/iplugview.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"
#include <windows.h>

namespace lmms::vsthost
{
class Vst3Editor final : public Steinberg::U::Implements<Steinberg::U::Directly<Steinberg::IPlugFrame>>
{
public:
	~Vst3Editor() override;
	void open(Steinberg::Vst::IEditController& controller);
	void close();
	void show(bool visible);
	HWND window() const noexcept { return m_window; }
	Steinberg::tresult PLUGIN_API resizeView(Steinberg::IPlugView* view, Steinberg::ViewRect* rectangle) override;
private:
	static LRESULT CALLBACK windowProcedure(HWND window, UINT message, WPARAM wparam, LPARAM lparam);
	Steinberg::IPtr<Steinberg::IPlugView> m_view;
	HWND m_window = nullptr;
	bool m_attached = false, m_resizing = false;
};
} // namespace lmms::vsthost
#endif
