#include "Vst3Editor.h"
#include <stdexcept>

namespace lmms::vsthost {
using namespace Steinberg;
namespace {
bool valid(const ViewRect& rectangle)
{
	return rectangle.left == 0 && rectangle.top == 0 && rectangle.right > 0 && rectangle.bottom > 0
		&& rectangle.right <= 16384 && rectangle.bottom <= 16384;
}
void require(tresult result, const char* operation)
{
	if (result != kResultOk)
	{
		throw std::runtime_error(operation);
	}
}
}
Vst3Editor::~Vst3Editor()
{
	try
	{
		close();
	}
	catch (...)
	{
	}
}
void Vst3Editor::open(Vst::IEditController& controller)
{
	if (m_view || m_window)
	{
		throw std::logic_error("VST3 editor already open");
	}
	m_view = owned(controller.createView(Vst::ViewType::kEditor));
	if (!m_view)
	{
		throw std::runtime_error("VST3 controller has no native editor");
	}
	require(m_view->isPlatformTypeSupported(kPlatformTypeHWND), "VST3 editor does not support HWND");
	// Some editors (including Kontakt) only establish their dimensions after
	// receiving a native parent. Use a temporary parent size until attachment.
	ViewRect rectangle{0, 0, 640, 480};
	ViewRect preferred{};
	if (m_view->getSize(&preferred) == kResultOk)
	{
		if (!valid(preferred))
		{
			throw std::runtime_error("VST3 editor has invalid dimensions");
		}
		rectangle = preferred;
	}
	WNDCLASSEXW windowClass{};
	windowClass.cbSize = sizeof(windowClass);
	windowClass.hInstance = GetModuleHandleW(nullptr);
	windowClass.lpfnWndProc = windowProcedure;
	windowClass.lpszClassName = L"LMMS-VST3-Editor";
	windowClass.hCursor = LoadCursor(nullptr, IDC_ARROW);
	if (!RegisterClassExW(&windowClass) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
	{
		throw std::runtime_error("Could not register VST3 editor window");
	}
	m_window = CreateWindowExW(0, windowClass.lpszClassName, L"LMMS VST3 Editor",
		WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX, CW_USEDEFAULT, CW_USEDEFAULT, rectangle.right, rectangle.bottom,
		nullptr, nullptr, windowClass.hInstance, this);
	if (!m_window)
	{
		throw std::runtime_error("Could not create VST3 editor window");
	}
	require(m_view->setFrame(this), "VST3 editor setFrame failed");
	RECT outer{0, 0, rectangle.right, rectangle.bottom};
	if (!AdjustWindowRectEx(&outer, static_cast<DWORD>(GetWindowLongPtrW(m_window, GWL_STYLE)), FALSE, 0)
		|| !SetWindowPos(m_window, nullptr, 0, 0, outer.right - outer.left, outer.bottom - outer.top,
			SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE))
	{
		throw std::runtime_error("VST3 editor initial window sizing failed");
	}
	require(m_view->attached(m_window, kPlatformTypeHWND), "VST3 editor attach failed");
	m_attached = true;
	require(m_view->getSize(&rectangle), "VST3 attached editor getSize failed");
	if (!valid(rectangle))
	{
		throw std::runtime_error("VST3 attached editor has invalid dimensions");
	}
	require(resizeView(m_view, &rectangle), "VST3 attached editor sizing failed");
	ShowWindow(m_window, SW_SHOW);
}
void Vst3Editor::close()
{
	tresult failure = kResultOk;
	if (m_view)
	{
		if (m_attached)
		{
			failure = m_view->removed();
			m_attached = false;
		}
		const auto detached = m_view->setFrame(nullptr);
		if (failure == kResultOk)
		{
			failure = detached;
		}
		m_view.reset();
	}
	if (m_window)
	{
		DestroyWindow(m_window);
		m_window = nullptr;
	}
	require(failure, "VST3 editor detach failed");
}
void Vst3Editor::show(bool visible)
{
	if (m_window)
	{
		ShowWindow(m_window, visible ? SW_SHOW : SW_HIDE);
	}
}
tresult PLUGIN_API Vst3Editor::resizeView(IPlugView* view, ViewRect* rectangle)
{
	if (!view || view != m_view.get() || !rectangle || !valid(*rectangle) || !m_window || m_resizing)
	{
		return kInvalidArgument;
	}
	m_resizing = true;
	struct Reset
	{
		bool& value;
		~Reset() { value = false; }
	} reset{m_resizing};
	RECT outer{0, 0, rectangle->right, rectangle->bottom};
	if (!AdjustWindowRectEx(&outer, static_cast<DWORD>(GetWindowLongPtrW(m_window, GWL_STYLE)), FALSE, 0)
		|| !SetWindowPos(m_window, nullptr, 0, 0, outer.right - outer.left, outer.bottom - outer.top,
			SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE))
	{
		return kResultFalse;
	}
	return view->onSize(rectangle);
}
LRESULT CALLBACK Vst3Editor::windowProcedure(HWND window, UINT message, WPARAM wparam, LPARAM lparam)
{
	if (message == WM_CLOSE)
	{
		ShowWindow(window, SW_HIDE);
		return 0;
	}
	return DefWindowProcW(window, message, wparam, lparam);
}
} // namespace lmms::vsthost
