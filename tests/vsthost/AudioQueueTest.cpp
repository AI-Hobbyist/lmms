#include "vsthost/AudioQueue.h"
#include <array>
#include <iostream>
#include <vector>
#include <string>

using namespace lmms::vsthost;
int worker(const wchar_t* name)
{
	const auto mapping = OpenFileMappingW(FILE_MAP_ALL_ACCESS, FALSE, name);
	if (!mapping) { return 2; }
	auto* memory = static_cast<std::uint8_t*>(MapViewOfFile(mapping, FILE_MAP_ALL_ACCESS, 0, 0, AudioQueue::StorageBytes));
	if (!memory) { CloseHandle(mapping); return 3; }
	AudioQueue queue({memory, AudioQueue::StorageBytes});
	std::array<float, 8> audio{};
	std::array<std::uint8_t, 3> events{};
	AudioQueue::Claim claim{};
	const auto claimed = queue.claim(claim, audio, events);
	bool ok = claimed == AudioQueue::Result::Ok && matches(claim.header, 31, 4, 1) &&
		events == std::array<std::uint8_t, 3>{0x90, 60, 127};
	if (ok)
	{
		for (auto& value : audio) { value *= 0.25f; }
		ok = claim.collectOutput && queue.complete(claim, audio, events) == AudioQueue::Result::Ok;
	}
	UnmapViewOfFile(memory); CloseHandle(mapping);
	return ok ? 0 : 4;
}

bool crossProcess(const wchar_t* executable)
{
	const auto name = L"Local\\LMMS-VstQueue-Test-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64());
	const auto mapping = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, AudioQueue::StorageBytes, name.c_str());
	if (!mapping) { return false; }
	auto* memory = static_cast<std::uint8_t*>(MapViewOfFile(mapping, FILE_MAP_ALL_ACCESS, 0, 0, AudioQueue::StorageBytes));
	if (!memory) { CloseHandle(mapping); return false; }
	AudioQueue queue({memory, AudioQueue::StorageBytes});
	const std::array<float, 8> input{1, 2, 3, 4, 5, 6, 7, 8};
	const std::array<std::uint8_t, 3> events{0x90, 60, 127};
	std::array<float, 8> output{};
	bool ok = queue.initialize() && queue.submit({MessageType::Process, 31, 4, 1, 0}, {4, 2, 2}, input, events, true) == AudioQueue::Result::Ok;
	std::wstring command = L"\"" + std::wstring(executable) + L"\" --worker \"" + name + L"\"";
	STARTUPINFOW startup{}; startup.cb = sizeof(startup);
	PROCESS_INFORMATION process{};
	if (ok && CreateProcessW(executable, command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &startup, &process))
	{
		const auto waited = WaitForSingleObject(process.hProcess, 5000);
		DWORD exitCode = 1;
		if (waited != WAIT_OBJECT_0) { TerminateProcess(process.hProcess, 5); WaitForSingleObject(process.hProcess, 5000); }
		GetExitCodeProcess(process.hProcess, &exitCode);
		std::array<std::uint8_t, 3> feedback{}; std::uint32_t feedbackBytes = 0;
			ok = waited == WAIT_OBJECT_0 && exitCode == 0 &&
				queue.receive(31, 4, 1, output, feedback, &feedbackBytes) == AudioQueue::Result::Ok &&
				feedbackBytes == events.size() && feedback == events;
		for (std::size_t i = 0; i < output.size(); ++i) { ok = ok && output[i] == input[i] * 0.25f; }
		CloseHandle(process.hThread); CloseHandle(process.hProcess);
	}
	else { ok = false; }
	UnmapViewOfFile(memory); CloseHandle(mapping);
	return ok;
}

int wmain(int argc, wchar_t** argv)
{
	if (argc == 3 && std::wstring(argv[1]) == L"--worker") { return worker(argv[2]); }
	using R = AudioQueue::Result;
	int failures = 0;
	auto check = [&](bool result, const char* label) { if (!result) { ++failures; std::cerr << label << '\n'; } };
	std::vector<std::uint8_t> memory(AudioQueue::StorageBytes);
	AudioQueue queue(memory);
	check(queue.initialize(), "initialize");
	AudioQueue invalid(std::span(memory).subspan(1));
	check(!invalid.valid(), "unaligned/incomplete mapping");
	std::array<float, 8> input{1, 2, 3, 4, 5, 6, 7, 8}, worker{}, result{}, output{};
	std::array<std::uint8_t, 3> midi{0x90, 60, 127}, events{};
	Header request{MessageType::Process, 11, 1, 0, 0};
	check(queue.submit(request, {4, 2, 2}, input, midi) == R::Ok, "submit");
	check(queue.receive(11, 1, 0, result) == R::Empty, "nonblocking first block");
	AudioQueue::Claim claim{};
	check(queue.claim(claim, worker, events) == R::Ok && worker == input && events == midi, "claim audio/events");
	for (std::size_t i = 0; i < output.size(); ++i) { output[i] = worker[i] * 0.25f; }
	auto wrong = claim; ++wrong.header.generation;
	check(queue.complete(wrong, output) == R::Invalid, "wrong worker generation");
	check(queue.complete(claim, output) == R::Ok, "complete");
	check(queue.receive(11, 1, 0, result) == R::Ok && result == output, "one block result");
	check(queue.receive(11, 1, 0, result) == R::Empty, "consume once");
	for (std::uint64_t i = 1; i <= AudioQueue::Slots; ++i)
	{ request.sequence = i; check(queue.submit(request, {4, 2, 2}, input) == R::Ok, "fill queue"); }
	check(queue.submit(request, {4, 2, 2}, input) == R::Full, "bounded full queue");
	for (std::uint64_t i = 1; i <= AudioQueue::Slots; ++i)
	{
		check(queue.claim(claim, worker, events) == R::Ok && claim.header.sequence == i, "oldest claim order");
		check(queue.complete(claim, output) == R::Ok, "late worker complete");
	}
	check(queue.receive(11, 2, 3, result) == R::Empty, "reject prior generation");
	request.generation = 2; request.sequence = 4;
	check(queue.submit(request, {4, 2, 2}, input) == R::Ok, "restart request");
	check(queue.claim(claim, worker, events) == R::Ok && queue.complete(claim, output) == R::Ok, "restart worker");
	check(queue.receive(11, 2, 5, result) == R::Empty, "late block not reused");
	request.sequence = 6;
	check(queue.submit(request, {UINT32_MAX, 2, 2}, input) == R::Invalid, "overflow frames rejected");
	check(queue.submit(request, {4, 33, 2}, input) == R::Invalid, "channel bound");
	check(queue.submit(request, {4, 2, 2}, input) == R::Ok, "malformed setup");
	put(memory, 44, UINT32_MAX, 4);
	check(queue.claim(claim, worker, events) == R::Invalid, "untrusted shared layout rejected");
	for (unsigned cycle = 0; cycle < 50; ++cycle)
	{
		for (std::uint64_t i = 0; i < AudioQueue::Slots; ++i)
		{
			request.sequence = cycle * AudioQueue::Slots + i;
			check(queue.submit(request, {4, 2, 2}, input) == R::Ok, "paused completion cycle submit");
			check(!queue.discardCompleted(11, 2, request.sequence + 1), "submitted work cannot be reclaimed");
			check(queue.claim(claim, worker, events) == R::Ok, "paused completion cycle claim");
			check(!queue.discardCompleted(11, 2, request.sequence + 1), "processing work cannot be reclaimed");
			check(queue.complete(claim, output) == R::Ok, "paused completion cycle complete");
		}
		check(!queue.discardCompleted(12, 2, request.sequence + 1), "foreign session completion rejected");
		check(!queue.discardCompleted(11, 3, request.sequence + 1), "foreign generation completion rejected");
		check(!queue.discardCompleted(11, 2, 0), "future completion rejected");
		check(queue.discardCompleted(11, 2, request.sequence + 1), "completed paused queue reclaimed without consuming audio");
	}
	request.sequence += 1;
	check(queue.submit(request, {4, 2, 2}, input) == R::Ok && queue.claim(claim, worker, events) == R::Ok &&
		queue.complete(claim, output) == R::Ok, "malformed completion retirement setup");
	put(memory, 44, UINT32_MAX, 4);
	check(!queue.discardCompleted(11, 2, request.sequence + 1), "malformed completed layout rejected during retirement");
	check(queue.initialize(), "reset isolated malformed test queue");
	std::vector<std::uint8_t> feedback(AudioQueue::MaxOutputEventBytes, 0x5a), received(feedback.size());
	std::uint32_t receivedBytes = 0;
	check(queue.submit(request, {4, 2, 2}, input, midi, true) == R::Ok, "feedback request");
	check(queue.claim(claim, worker, events) == R::Ok && claim.collectOutput, "feedback claim");
	check(queue.complete(claim, output, feedback) == R::Ok, "maximum feedback completion");
	check(queue.receive(11, 2, request.sequence, result, received, &receivedBytes) == R::Ok &&
		receivedBytes == feedback.size() && received == feedback && result == output, "atomic audio and feedback result");
	check(queue.receive(11, 2, request.sequence, result, received, &receivedBytes) == R::Empty &&
		receivedBytes == 0, "feedback consumed once");
	check(queue.submit(request, {4, 2, 2}, input, {}, true) == R::Ok &&
		queue.claim(claim, worker, events) == R::Ok, "oversized feedback setup");
	feedback.push_back(0);
	check(queue.complete(claim, output, feedback) == R::Invalid, "oversized feedback rejected");
	feedback.pop_back();
	check(queue.complete(claim, output, feedback) == R::Ok, "valid feedback after rejection");
	check(queue.receive(11, 2, request.sequence, result, std::span(received).first(1), &receivedBytes) == R::Invalid,
		"short feedback destination rejected");
	check(queue.submit(request, {4, 2, 2}, input) == R::Ok && queue.claim(claim, worker, events) == R::Ok,
		"noncollecting feedback setup");
	check(queue.complete(claim, output, feedback) == R::Invalid && queue.complete(claim, output) == R::Ok,
		"unsolicited feedback rejected");
	check(queue.receive(11, 2, request.sequence, result) == R::Ok, "legacy audio consumer retained");
	check(queue.submit(request, {4, 2, 2}, input, {}, true) == R::Ok &&
		queue.claim(claim, worker, events) == R::Ok && queue.complete(claim, output, feedback) == R::Ok,
		"corrupt feedback length setup");
	put(memory, 60, AudioQueue::MaxOutputEventBytes + 1, 4); result.fill(73);
	check(queue.receive(11, 2, request.sequence, result, received, &receivedBytes) == R::Invalid &&
		std::all_of(result.begin(), result.end(), [](float value) { return value == 73; }),
		"untrusted feedback length rejected before audio mutation");
	check(queue.submit(request, {4, 2, 2}, input) == R::Ok, "corrupt request flags setup");
	put(memory, 60, 2, 4);
	check(queue.claim(claim, worker, events) == R::Invalid, "unknown feedback flags rejected");
	std::array<wchar_t, 32768> executable{};
	check(GetModuleFileNameW(nullptr, executable.data(), static_cast<DWORD>(executable.size())) != 0, "test executable path");
	check(crossProcess(executable.data()), "shared mapping between same-ABI processes");
	if (argc == 3 && std::wstring(argv[1]) == L"--peer")
	{ check(crossProcess(argv[2]), "shared mapping between x64 parent and x86 worker"); }
	std::cout << (failures ? "FAIL" : "PASS") << ": audio queue, " << failures << " failures\n";
	return failures ? 1 : 0;
}
