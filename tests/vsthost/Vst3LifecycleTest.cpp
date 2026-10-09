#include "vsthost/Vst3Scanner.h"
#include "vsthost/Vst3Commands.h"
#include <filesystem>
#include <iostream>

using namespace lmms::vsthost;
#define CHECK(condition) \
	do \
	{ \
		if (!(condition)) \
		{ \
			std::cerr << "FAIL " << __LINE__ << ": " << #condition << '\n'; \
			return 1; \
		} \
	} while (false)

int wmain(int argc, wchar_t** argv)
{
	if (argc != 3)
	{
		return 2;
	}
	const std::wstring helper = argv[1];
	const std::filesystem::path plugin = argv[2];
	const auto scan = scanVst3(helper, plugin.wstring());
	CHECK(scan.error == Error::None && scan.classes.size() == 3);
	const auto utf8 = plugin.u8string();
	const std::string path(reinterpret_cast<const char*>(utf8.data()), utf8.size());
	DWORD before = 0;
	CHECK(GetProcessHandleCount(GetCurrentProcess(), &before));
	for (unsigned cycle = 0; cycle < 50; ++cycle)
	{
		HostSession session;
		CHECK(session.open({helper, {}, 5000, 100}).get().error == Error::None);
		std::vector<std::uint8_t> command;
		CHECK(encodeVst3Create(
			{scan.classes[cycle % 2].cid, cycle % 2 ? 44100.0 : 48000.0, cycle % 2 ? 64u : 512u, true, path}, command));
		CHECK(session.request(MessageType::Create, command, 5000).get().error == Error::None);
		const auto shown = session.request(MessageType::ShowEditor, {}, 3000).get();
		CHECK(shown.error == Error::None && shown.payload.size() == 8);
		const auto window = reinterpret_cast<HWND>(static_cast<std::uintptr_t>(get(shown.payload, 0, 8)));
		DWORD pid = 0;
		GetWindowThreadProcessId(window, &pid);
		CHECK(IsWindowVisible(window) && pid == session.pid());
		const auto child = FindWindowExW(window, nullptr, L"STATIC", L"LMMS VST3 fixture editor");
		CHECK(child != nullptr);
		DWORD_PTR resized = 0;
		CHECK(SendMessageTimeoutW(child, WM_APP + 38, 0, 0, SMTO_ABORTIFHUNG, 1500, &resized) && resized == 1);
		RECT rectangle{};
		CHECK(GetClientRect(window, &rectangle));
		CHECK(rectangle.right == 317 && rectangle.bottom == 173);
		CHECK(
			session.request(MessageType::HideEditor, {}, 3000).get().error == Error::None && !IsWindowVisible(window));
		CHECK(session.request(MessageType::ShowEditor, {}, 3000).get().error == Error::None && IsWindowVisible(window));
		const auto state = session.request(MessageType::GetState, {}, 3000, true).get();
		CHECK(state.error == Error::None && state.payload.size() == 32);
		double processor = 0, controller = 0;
		std::memcpy(&processor, state.payload.data() + 16, 8);
		std::memcpy(&controller, state.payload.data() + 24, 8);
		CHECK(processor == 0.625 && controller == processor);
		const auto audio
			= session
				  .renderOffline(
					  {32, 2, 2}, std::vector<float>(64, 1), [] { return std::span<const std::uint8_t>{}; }, 3000)
				  .get();
		CHECK(audio.error == Error::None && audio.payload.size() == 256);
		float sample = 0;
		std::memcpy(&sample, audio.payload.data(), 4);
		CHECK(sample == 0.625f);
		if (cycle == 0)
		{
			// A slow native GUI handler must not consume the realtime DSP deadline.
			CHECK(PostMessageW(child, WM_APP + 40, 0, 0));
			const auto readyDeadline = GetTickCount64() + 1000;
			while (!GetPropW(child, L"LMMSFixtureGuiBusy") && GetTickCount64() < readyDeadline)
			{
				Sleep(1);
			}
			CHECK(GetPropW(child, L"LMMSFixtureGuiBusy"));
			std::vector<float> input(64, 1), output(64);
			const auto start = GetTickCount64();
			CHECK(session.process({32, 2, 2}, input, output));
			CHECK(session.process({32, 2, 2}, input, output));
			CHECK(output.front() == 0.625f);
			CHECK(session.process({32, 2, 2}, input, output));
			CHECK(output.front() == 0.625f && GetTickCount64() - start < 250);
			CHECK(GetPropW(child, L"LMMSFixtureGuiBusy"));
			CHECK(session.error() == Error::None);
		}
		CHECK(session.close().get().error == Error::None);
		CHECK(!IsWindow(window) && !IsWindow(child));
	}
	DWORD after = 0;
	CHECK(GetProcessHandleCount(GetCurrentProcess(), &after));
	CHECK(after <= before + 4);
	CHECK(GetModuleHandleW(plugin.filename().c_str()) == nullptr);
	std::cout << "PASS 50 native editor/resize/state/audio/close lifetimes; handles " << before << " -> " << after
			  << '\n';
	return 0;
}
