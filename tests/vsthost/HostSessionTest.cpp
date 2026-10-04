#include "vsthost/HostSession.h"
#include <array>
#include <iostream>

using namespace lmms::vsthost;
int wmain(int argc, wchar_t** argv)
{
	if (argc != 3 || std::wstring(argv[1]) != L"--helper") { return 2; }
	int failures = 0;
	auto check = [&](bool ok, const char* label) { if (!ok) { ++failures; std::cerr << label << '\n'; } };
	auto configuration = [&](const std::wstring& mode = L"")
	{
		HostSession::Configuration config;
		config.helper = argv[2]; config.startupMs = 5000;
		if (!mode.empty()) { config.arguments = {L"--mode", mode}; }
		return config;
	};
	auto reply = [&](std::future<HostSession::Reply> future)
	{
		if (future.wait_for(std::chrono::seconds(7)) != std::future_status::ready)
		{ check(false, "future deadline"); return HostSession::Reply{Error::Timeout, {}}; }
		return future.get();
	};
	std::array<float, 8> input{1, 2, 3, 4, 5, 6, 7, 8}, output{};
	{
		HostSession session(1024);
		const auto begin = GetTickCount64();
		auto opening = session.open(configuration());
		check(GetTickCount64() - begin < 50, "open returns future without starting/waiting on caller");
		check(reply(std::move(opening)).error == Error::None && session.state() == SessionState::Ready, "handshake ready");
		const auto firstGeneration = session.generation(), identity = session.id();
		const auto firstPid = session.pid();
		check(session.process({4, 2, 2}, input, output) && output == std::array<float, 8>{}, "pipeline first block silence");
		Sleep(10);
		check(session.process({4, 2, 2}, input, output), "pipeline preceding result");
		for (unsigned i = 0; i < input.size(); ++i) { check(output[i] == input[i] * 0.25f, "known pipeline samples"); }
		const std::vector<std::uint8_t> state{0, 255, 127, 42};
		const auto restored = reply(session.request(MessageType::SetState, state, 1000, true));
		check(restored.error == Error::None && restored.payload == state, "state behind async pause/resume barrier");
		check(session.process({4, 2, 2}, input, output) && output == std::array<float, 8>{}, "barrier resumes new pipeline");
		const auto owner = std::this_thread::get_id();
		bool published = false;
		const auto publication = session.requestPreparedPublished(MessageType::SetState, [state] { return state; },
			[&](HostSession::Reply& result) {
				check(std::this_thread::get_id() == owner, "publication runs on control owner");
				check(result.payload == state, "publication receives completed native reply");
				check(!session.process({4, 2, 2}, input, output), "publication retains audio admission barrier");
				check(output == std::array<float, 8>{}, "quiescent publication silences callbacks");
				published = true;
			}, 1000);
		check(publication.error == Error::None && published, "publication acknowledged before resume");
		check(session.process({4, 2, 2}, input, output), "published state resumes audio");
		const std::vector<std::uint8_t> nextState{7, 8, 9};
		const auto followed = session.requestPreparedFollowedPublished(MessageType::SetState, [state] { return state; },
			[&](HostSession::Reply& first) -> std::optional<HostSession::Followup> {
				check(first.payload == state, "first exchange completed before follow-up");
				check(!session.process({4, 2, 2}, input, output), "no audio admission between paused exchanges");
				return HostSession::Followup{MessageType::SetState, [&] {
					check(!session.process({4, 2, 2}, input, output), "follow-up preparation retains admission barrier");
					return nextState;
				}};
			}, [&](HostSession::Reply& final) {
				check(final.payload == nextState, "control owner publishes final exchange");
				check(!session.process({4, 2, 2}, input, output), "final publication before resume");
			}, 1000);
		check(followed.error == Error::None && followed.payload == nextState, "single transaction follow-up successful");
		check(session.process({4, 2, 2}, input, output), "follow-up transaction resumes audio"); Sleep(10);
		const auto readOnly = session.requestPreparedFollowedPublished(MessageType::GetState,
			[] { return std::vector<std::uint8_t>{}; }, [](HostSession::Reply&) -> std::optional<HostSession::Followup> { return {}; },
			[](HostSession::Reply&) {}, 1000, false);
		check(readOnly.error == Error::None && session.process({4, 2, 2}, input, output), "read-only request preserves audio admission");
		for (unsigned i = 0; i < input.size(); ++i) { check(output[i] == input[i] * 0.25f, "read-only request preserves pending sequence"); }
		const auto conditional = session.requestPreparedFollowedPublished(MessageType::GetState,
			[] { return std::vector<std::uint8_t>{}; }, [&](HostSession::Reply&) -> std::optional<HostSession::Followup> {
				return HostSession::Followup{MessageType::SetState, [&] {
					check(!session.process({4, 2, 2}, input, output), "conditional follow-up quiesces before preparation");
					return nextState;
				}};
			}, [&](HostSession::Reply&) { check(!session.process({4, 2, 2}, input, output), "conditional publication before resume"); }, 1000, false);
		check(conditional.error == Error::None, "conditional pause transaction successful");
		check(reply(session.close()).error == Error::None && session.state() == SessionState::Stopped && !session.pid(), "close acknowledged/cleaned");
		check(reply(session.open(configuration())).error == Error::None && session.id() == identity &&
			session.generation() == firstGeneration + 1 && session.pid() != firstPid, "new process and generation on restart");
		check(reply(session.close()).error == Error::None, "restart cleanup");
	}
	{
		HostSession session(1024);
		check(reply(session.open(configuration(L"old-generation"))).error == Error::None, "old generation reply discarded");
		check(reply(session.close()).error == Error::None, "old reply cleanup");
	}
	{
		HostSession session(1024);
		check(reply(session.open(configuration(L"wrong-sequence"))).error == Error::InvalidMessage && session.state() == SessionState::Faulted, "out-of-order control reply faults");
	}
	for (const auto mode : {L"hang-state", L"crash-state"})
	{
		HostSession session(1024);
		check(reply(session.open(configuration(mode))).error == Error::None, "state fault fixture ready");
		const auto failed = reply(session.request(MessageType::GetState, {}, 100, true));
		std::wcout << mode << L" error=" << static_cast<unsigned>(failed.error) << L'\n';
		check(failed.error == (std::wstring(mode) == L"hang-state" ? Error::Timeout : Error::ProcessCrashed), "state crash/hang structured failure");
		check(session.state() == SessionState::Faulted && !session.process({4, 2, 2}, input, output), "faulted instrument silence");
		check(output == std::array<float, 8>{}, "fault output zero");
		check(reply(session.open(configuration(mode))).error == Error::None, "follow-up fault fixture ready");
		bool published = false;
		const auto followFailure = session.requestPreparedFollowedPublished(MessageType::SetState,
			[] { return std::vector<std::uint8_t>{}; }, [](HostSession::Reply&) -> std::optional<HostSession::Followup> {
				return HostSession::Followup{MessageType::GetState, [] { return std::vector<std::uint8_t>{}; }};
			}, [&](HostSession::Reply&) { published = true; }, 100);
		check(followFailure.error == (std::wstring(mode) == L"hang-state" ? Error::Timeout : Error::ProcessCrashed),
			"follow-up crash/hang structured failure");
		check(!published && !session.process({4, 2, 2}, input, output), "follow-up failure does not publish or resume audio");
		check(reply(session.open(configuration())).error == Error::None, "recover failed session");
		check(reply(session.close()).error == Error::None, "recovered cleanup");
	}
	{
		HostSession session(1024);
		check(reply(session.open(configuration(L"hang-audio"))).error == Error::None, "audio hang fixture ready");
		check(session.process({4, 2, 2}, input, output), "audio hang submit"); Sleep(10);
		const auto begin = GetTickCount64();
		check(!session.process({4, 2, 2}, input, output) && GetTickCount64() - begin < 10, "audio deadline never waits for hung helper");
		const auto deadline = GetTickCount64() + 2000;
		while (session.state() != SessionState::Faulted && GetTickCount64() < deadline) { Sleep(1); }
		check(session.state() == SessionState::Faulted && session.error() == Error::ProcessingFailed, "audio fault supervised");
		check(reply(session.open(configuration())).error == Error::None, "audio fault restart");
		check(reply(session.close()).error == Error::None, "audio recovery cleanup");
	}
	{
		HostSession session(1024);
		check(reply(session.open(configuration())).error == Error::None, "publication exception fixture ready");
		const auto failed = session.requestPreparedPublished(MessageType::SetState,
			[] { return std::vector<std::uint8_t>{}; }, [](HostSession::Reply&) { throw std::runtime_error("publication failed"); }, 1000);
		check(failed.error == Error::InitializationFailed && !session.process({4, 2, 2}, input, output),
			"publication exception faults without resuming audio");
		bool published = false;
		const auto rejected = session.requestPreparedPublished(MessageType::GetState,
			[] { return std::vector<std::uint8_t>{}; }, [&](HostSession::Reply&) { published = true; }, 1000);
		check(rejected.error == Error::InvalidState && !published, "failed request returns without publication handshake");
	}
	DWORD before = 0, after = 0; GetProcessHandleCount(GetCurrentProcess(), &before);
	for (unsigned i = 0; i < 50; ++i)
	{
		HostSession session(1024);
		check(reply(session.open(configuration())).error == Error::None, "50 session lifetimes open");
		check(reply(session.close()).error == Error::None, "50 session lifetimes close");
	}
	GetProcessHandleCount(GetCurrentProcess(), &after);
	check(after <= before + 2, "session mappings/jobs/process handles released");
	std::cout << (failures ? "FAIL" : "PASS") << ": HostSession, " << failures << " failures\n";
	return failures ? 1 : 0;
}
