#include "vsthost/HostSession.h"
#include "vsthost/LegacyMessageCodec.h"
#include "RemotePluginBase.h"
#include "communication.h"
#include <cmath>
#include <fstream>
#include <iostream>
#include <filesystem>
#include <tlhelp32.h>

using namespace lmms;
using namespace lmms::vsthost;
namespace {
int failures = 0;
void check(bool result, const char* description)
{
	if (!result)
	{
		++failures;
		std::cerr << "FAIL: " << description << '\n';
	}
}
HostSession::Reply await(std::future<HostSession::Reply> future)
{
	if (future.wait_for(std::chrono::seconds(7)) != std::future_status::ready)
	{
		check(false, "bounded future");
		return {Error::Timeout, {}};
	}
	return future.get();
}
std::vector<LegacyCommand> request(HostSession& session, std::vector<LegacyCommand> commands, bool pause = true)
{
	std::vector<std::uint8_t> bytes;
	check(encodeLegacy(commands, bytes) == Error::None, "encode control commands");
	auto reply = await(session.request(MessageType::Parameter, std::move(bytes), 1000, pause));
	check(reply.error == Error::None, "native control reply");
	std::vector<LegacyCommand> result;
	check(decodeLegacy(reply.payload, result) == Error::None, "decode native replies");
	return result;
}
const LegacyCommand* find(const std::vector<LegacyCommand>& replies, unsigned id)
{
	for (const auto& reply : replies)
	{
		if (reply.id == id)
		{
			return &reply;
		}
	}
	return nullptr;
}
bool hasModule(DWORD pid, const std::wstring& name)
{
	HANDLE snapshot = INVALID_HANDLE_VALUE;
	for (int attempt = 0; attempt < 5 && snapshot == INVALID_HANDLE_VALUE; ++attempt)
	{
		snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid);
	}
	if (snapshot == INVALID_HANDLE_VALUE)
	{
		return false;
	}
	MODULEENTRY32W entry{};
	entry.dwSize = sizeof(entry);
	bool found = false;
	if (Module32FirstW(snapshot, &entry))
	{
		do
		{
			if (_wcsicmp(entry.szModule, name.c_str()) == 0)
			{
				found = true;
			}
		} while (Module32NextW(snapshot, &entry));
	}
	CloseHandle(snapshot);
	return found;
}
void codec()
{
	std::vector<std::uint8_t> bytes;
	const std::vector<LegacyCommand> input{{64, {"path", "0.25"}}, {18, {}}};
	check(encodeLegacy(input, bytes) == Error::None && bytes[0] == 2 && bytes[4] == 64, "fixed wire widths");
	std::vector<LegacyCommand> output;
	check(decodeLegacy(bytes, output) == Error::None && output.size() == 2 && output[0].arguments == input[0].arguments,
		"control round trip");
	const auto valid = bytes;
	put(bytes, 8, UINT32_MAX, 4);
	check(decodeLegacy(bytes, output) == Error::InvalidMessage && output.size() == 2,
		"argument count rejected before allocation");
	bytes = valid;
	put(bytes, 12, UINT32_MAX, 4);
	check(decodeLegacy(bytes, output) == Error::InvalidMessage, "oversized string rejected");
	bytes = valid;
	bytes.push_back(0);
	check(decodeLegacy(bytes, output) == Error::InvalidMessage, "trailing bytes rejected");
	bytes = valid;
	bytes.pop_back();
	check(decodeLegacy(bytes, output) == Error::InvalidMessage, "truncated packet rejected");
}
}
int wmain(int argc, wchar_t** argv)
{
	if (argc != 3)
	{
		return 2;
	}
	codec();
	HostSession session;
	HostSession::Configuration configuration{argv[1], {L"none"}, 3000};
	check(await(session.open(configuration)).error == Error::None, "supervised native helper opens");
	const auto pluginPath = std::filesystem::path(argv[2]);
	const auto utf8 = pluginPath.u8string();
	const auto path = std::string(reinterpret_cast<const char*>(utf8.data()), utf8.size());
	auto replies = request(
		session, {{IdSampleRateInformation, {"44100"}}, {IdBufferSizeInformation, {"64"}}, {IdVstLoadPlugin, {path}}});
	check(find(replies, IdInitDone) != nullptr, "VST2 entry runs in helper");
	const auto* io = find(replies, IdChangeInputOutputCount);
	check(io && io->arguments == std::vector<std::string>{"2", "2"}, "stereo bus metadata");
	check(hasModule(session.pid(), pluginPath.filename().wstring()), "native DLL in child");
	check(!hasModule(GetCurrentProcessId(), pluginPath.filename().wstring()), "native DLL absent from parent");
	const auto* window = find(replies, IdVstPluginWindowID);
	HWND editor = window && !window->arguments.empty()
		? reinterpret_cast<HWND>(
			  static_cast<std::uintptr_t>(static_cast<std::uint32_t>(std::stoll(window->arguments[0]))))
		: nullptr;
	DWORD editorPid = 0;
	if (editor)
	{
		GetWindowThreadProcessId(editor, &editorPid);
	}
	check(editor && IsWindow(editor) && editorPid == session.pid(), "native editor belongs to helper");
	request(session, {{IdShowUI, {}}});
	check(IsWindowVisible(editor), "show native editor");
	request(session, {{IdHideUI, {}}});
	check(!IsWindowVisible(editor), "hide native editor");
	std::vector<float> input(128, 0.75f), output(128, -1);
	check(session.process({64, 2, 2}, input, output), "submit first block without waiting");
	check(std::all_of(output.begin(), output.end(), [](float v) { return v == 0; }),
		"first block is defined pipeline silence");
	Sleep(25);
	check(session.process({64, 2, 2}, input, output), "receive previous native block");
	check(std::all_of(output.begin(), output.end(), [](float v) { return std::abs(v - 0.375f) < 1e-6f; }),
		"native gain matches known samples");
	replies = request(session, {{IdVstSetParameter, {"0", "0.25"}}, {IdVstGetParameterDump, {}}});
	const auto* dump = find(replies, IdVstParameterDump);
	check(dump && dump->arguments.size() == 4 && std::abs(std::stof(dump->arguments[3]) - 0.25f) < 1e-6f,
		"parameter state survives pause barrier");
	request(session, {{IdVstSetProgram, {"1"}}, {IdVstIdleUpdate, {}}});
	replies = request(session, {{IdVstCurrentProgram, {}}});
	const auto* program = find(replies, IdVstCurrentProgram);
	check(program && program->arguments.size() == 1 && program->arguments[0] == "1", "program compatibility");
	request(session, {{IdVstSetParameter, {"0", "0.25"}}});
	std::array<std::uint8_t, 40> midi{};
	put(midi, 0, 0x90, 4);
	put(midi, 8, 60, 4);
	put(midi, 12, 127, 4);
	put(midi, 16, 11, 4);
	put(midi, 20, 0x80, 4);
	put(midi, 28, 60, 4);
	put(midi, 36, 37, 4);
	std::fill(input.begin(), input.end(), 0.0f);
	check(session.process({64, 2, 2}, input, output, midi), "enqueue timestamped MIDI");
	Sleep(25);
	check(session.process({64, 2, 2}, input, output), "receive native MIDI audio");
	for (int frame = 0; frame < 64; ++frame)
	{
		check(output[frame * 2] == (frame >= 11 && frame < 37 ? 1.0f : 0.0f), "MIDI exact sample offset");
	}
	const auto temp = std::filesystem::temp_directory_path()
		/ ("lmms-vst-session-" + std::to_string(GetCurrentProcessId()) + ".chunk");
	request(session, {{IdSaveSettingsToFile, {temp.string()}}});
	float gain = 0;
	std::ifstream file(temp, std::ios::binary);
	file.read(reinterpret_cast<char*>(&gain), sizeof(gain));
	file.close();
	check(std::abs(gain - 0.25f) < 1e-6f, "native chunk persisted");
	replies = request(session,
		{{IdVstSetParameter, {"0", "0.75"}}, {IdLoadSettingsFromFile, {temp.string(), "4"}},
			{IdVstGetParameterDump, {}}});
	dump = find(replies, IdVstParameterDump);
	check(dump && dump->arguments.size() == 4 && std::abs(std::stof(dump->arguments[3]) - 0.25f) < 1e-6f,
		"native chunk restored");
	std::filesystem::remove(temp);
	const auto generation = session.generation();
	const auto pid = session.pid();
	check(await(session.close()).error == Error::None, "native close acknowledges after destruction");
	check(!IsWindow(editor), "native editor destroyed");
	check(await(session.open(configuration)).error == Error::None && session.generation() == generation + 1
			&& session.pid() != pid,
		"native helper generation restarts");
	check(await(session.close()).error == Error::None, "empty helper closes");
	std::cout << "VST2 native session: " << failures << " failures\n";
	return failures ? 1 : 0;
}
