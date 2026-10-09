#include "vsthost/HostSession.h"
#include "vsthost/LegacyMessageCodec.h"
#include "RemotePluginBase.h"
#include "communication.h"
#include <filesystem>
#include <iostream>

using namespace lmms;
using namespace lmms::vsthost;
namespace {
int failures = 0;
void check(bool result, const std::string& description)
{
	if (!result)
	{
		++failures;
		std::cerr << "FAIL: " << description << '\n';
	}
}
HostSession::Reply await(std::future<HostSession::Reply> future)
{
	if (future.wait_for(std::chrono::seconds(6)) != std::future_status::ready)
	{
		check(false, "bounded native fault future");
		return {Error::Timeout, {}};
	}
	return future.get();
}
std::string utf8(const std::filesystem::path& path)
{
	const auto value = path.u8string();
	return {reinterpret_cast<const char*>(value.data()), value.size()};
}
HostSession::Reply command(HostSession& session, std::vector<LegacyCommand> commands)
{
	std::vector<std::uint8_t> bytes;
	check(encodeLegacy(commands, bytes) == Error::None, "encode native fault request");
	return await(session.request(MessageType::Create, std::move(bytes), 150, true));
}
HostSession::Reply load(HostSession& session, const std::filesystem::path& path)
{
	return command(session,
		{{IdSampleRateInformation, {"44100"}}, {IdBufferSizeInformation, {"64"}}, {IdVstLoadPlugin, {utf8(path)}}});
}
}
int wmain(int argc, wchar_t** argv)
{
	if (argc != 4)
	{
		return 2;
	}
	const auto faultDirectory = std::filesystem::path(argv[2]);
	const auto baseline = std::filesystem::path(argv[3]);
	HostSession::Configuration configuration{argv[1], {L"none"}, 3000};
	const auto parentPid = GetCurrentProcessId();
	HostSession healthy;
	check(await(healthy.open(configuration)).error == Error::None && load(healthy, baseline).error == Error::None,
		"healthy native peer initializes");
	std::vector<float> input(128, 0.75f), output(128);
	check(healthy.process({64, 2, 2}, input, output), "healthy peer first audio");
	for (const std::wstring operation : {L"Entry", L"Audio", L"State", L"Editor", L"Close"})
	{
		for (const bool hang : {false, true})
		{
			const auto name = L"Vst2Fault" + operation + (hang ? L"Hang.dll" : L"Crash.dll");
			const auto description = utf8(name);
			HostSession failed;
			check(await(failed.open(configuration)).error == Error::None, description + " opens supervised helper");
			auto failure = load(failed, faultDirectory / name).error;
			if (operation != L"Entry" && operation != L"Editor")
			{
				check(failure == Error::None, description + " loads");
				if (operation == L"Audio")
				{
					const auto first = GetTickCount64();
					check(failed.process({64, 2, 2}, input, output), description + " first publication");
					check(GetTickCount64() - first < 10, description + " audio publication never waits");
					Sleep(40);
					const auto second = GetTickCount64();
					failed.process({64, 2, 2}, input, output);
					check(GetTickCount64() - second < 10, description + " late audio never waits");
					const auto deadline = GetTickCount64() + 1000;
					while (failed.state() != SessionState::Faulted && GetTickCount64() < deadline)
					{
						Sleep(1);
					}
					failure = failed.error();
				}
				else if (operation == L"State")
				{
					const auto temporary = std::filesystem::temp_directory_path()
						/ ("lmms-fault-" + std::to_string(parentPid) + ".chunk");
					failure = command(failed, {{IdSaveSettingsToFile, {utf8(temporary)}}}).error;
					std::filesystem::remove(temporary);
				}
				else
				{
					failure = await(failed.close()).error;
				}
			}
			const auto expected
				= hang ? (operation == L"Audio" ? Error::ProcessingFailed : Error::Timeout) : Error::ProcessCrashed;
			check(failure == expected,
				description + " structured crash/hang error " + std::to_string(static_cast<unsigned>(failure)));
			if (!hang)
			{
				check(failed.fault().nativeCode == 0xE0000042, description + " native exception code retained");
			}
			std::fill(output.begin(), output.end(), 9.0f);
			check(!failed.process({64, 2, 2}, input, output)
					&& std::all_of(output.begin(), output.end(), [](float value) { return value == 0; }),
				description + " failure is silence");
			const auto generation = failed.generation();
			check(await(failed.open(configuration)).error == Error::None && failed.generation() == generation + 1,
				description + " starts next generation");
			check(load(failed, baseline).error == Error::None, description + " reloads healthy DLL");
			check(await(failed.close()).error == Error::None, description + " clean shutdown after recovery");
			check(healthy.process({64, 2, 2}, input, output), "unaffected native instance continues");
			check(std::all_of(output.begin(), output.end(), [](float value) { return value == 0.375f; }),
				"unaffected native gain exact");
			check(GetCurrentProcessId() == parentPid, "parent survives native fault");
		}
	}
	check(await(healthy.close()).error == Error::None, "healthy peer clean shutdown");
	std::cout << "Native VST2 fault matrix: " << failures << " failures\n";
	return failures ? 1 : 0;
}
