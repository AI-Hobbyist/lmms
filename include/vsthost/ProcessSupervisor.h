#ifndef LMMS_VSTHOST_PROCESS_SUPERVISOR_H
#define LMMS_VSTHOST_PROCESS_SUPERVISOR_H

#include "vsthost/AudioQueue.h"
#include "WindowsCrashReport.h"
#include <atomic>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include <windows.h>

namespace lmms::vsthost {
// Control-thread lifetime object. Restart creates a new supervisor/job so an old
// process tree can never share the new generation's resources.
class ProcessSupervisor
{
public:
	enum class Stage : std::uint32_t
	{
		Startup,
		Scan,
		Initialize,
		Control,
		Audio,
		Shutdown
	};
	struct Fault
	{
		Error error;
		Stage stage;
		DWORD nativeCode;
	};
	enum class AudioFailure
	{
		Unspecified,
		InvalidResult,
		InvalidSubmission,
		QueueFull
	};
	struct AudioFaultRequest
	{
		std::atomic_flag claimed = ATOMIC_FLAG_INIT;
		std::atomic<Error> error{Error::None};
		AudioFailure detail = AudioFailure::Unspecified;
		std::uint64_t sequence = 0;
		AudioQueue::FullDiagnostic full;
	};
	ProcessSupervisor()
	{
		m_job = CreateJobObjectW(nullptr, nullptr);
		if (!m_job)
		{
			fail(Error::InitializationFailed, GetLastError());
			return;
		}
		JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
		limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
		if (!SetInformationJobObject(m_job, JobObjectExtendedLimitInformation, &limits, sizeof(limits)))
		{
			fail(Error::InitializationFailed, GetLastError());
			return;
		}
		m_watcher = std::jthread([this](std::stop_token stop) {
			while (!stop.stop_requested())
			{
				const auto requested = m_requestedError.exchange(Error::None, std::memory_order_acq_rel);
				const auto source = m_audioFaultSource.load(std::memory_order_acquire);
				const auto audioRequested
					= source ? source->error.exchange(Error::None, std::memory_order_acq_rel) : Error::None;
				if (requested != Error::None || audioRequested != Error::None)
				{
					m_stage.store(Stage::Audio, std::memory_order_relaxed);
					if (audioRequested != Error::None)
					{
						terminate(audioRequested, 0, source->detail, source->sequence,
							source->detail == AudioFailure::QueueFull ? &source->full : nullptr);
					}
					else
					{
						terminate(requested);
					}
				}
				auto deadline = m_deadline.load(std::memory_order_acquire);
				if (deadline && GetTickCount64() >= deadline)
				{
					// A cancelled/replaced deadline must not kill the next operation.
					if (m_deadline.compare_exchange_strong(deadline, 0, std::memory_order_acq_rel))
					{
						terminate(Error::Timeout, WAIT_TIMEOUT);
					}
				}
				const auto process = m_process.load(std::memory_order_acquire);
				if (process && WaitForSingleObject(process, 0) == WAIT_OBJECT_0)
				{
					DWORD code = 0;
					GetExitCodeProcess(process, &code);
					if (!m_expectedExit.load(std::memory_order_acquire) && fault().error == Error::None)
					{
						terminate(code ? Error::ProcessCrashed : Error::Disconnected, code);
					}
					m_deadline.store(0, std::memory_order_release);
				}
				Sleep(2);
			}
		});
	}
	~ProcessSupervisor()
	{
		close(0);
		m_watcher.request_stop();
		if (m_watcher.joinable())
		{
			m_watcher.join();
		}
		if (const auto process = m_process.load())
		{
			CloseHandle(process);
		}
		if (m_job)
		{
			CloseHandle(m_job);
		}
	}
	ProcessSupervisor(const ProcessSupervisor&) = delete;
	ProcessSupervisor& operator=(const ProcessSupervisor&) = delete;

	bool start(
		const std::wstring& executable, const std::vector<std::wstring>& arguments, DWORD startupTimeoutMs = 30000,
		const std::wstring& pluginPath = {})
	{
		if (!m_job || m_process.load() || fault().error != Error::None)
		{
			return false;
		}
		const std::filesystem::path helper(executable);
		const auto stem = helper.stem().wstring();
		if (stem == L"RemoteVstPlugin32" || stem == L"RemoteVstPlugin64"
			|| stem == L"RemoteVstHost32" || stem == L"RemoteVstHost64")
		{
			auto root = helper.parent_path();
			std::error_code error;
			while (!root.empty() && !std::filesystem::is_regular_file(root / L"lmms.exe", error))
			{
				const auto parent = root.parent_path();
				if (parent == root)
				{
					root.clear();
					break;
				}
				root = parent;
			}
			if (!root.empty())
			{
				m_reportDirectory = (root / L"logs" / L"VST").wstring();
				// DbgHelp must run at the sandbox's bitness to write x86 thread contexts.
				const bool x86 = stem == L"RemoteVstPlugin32" || stem == L"RemoteVstHost32";
				m_reporter = (root / (x86 ? L"LmmsCrashReporter32.exe" : L"LmmsCrashReporter64.exe")).wstring();
				m_pluginPath = pluginPath.empty() ? executable : pluginPath;
			}
		}
		arm(Stage::Startup, startupTimeoutMs);
		std::wstring command = quote(executable);
		for (const auto& argument : arguments)
		{
			command += L" " + quote(argument);
		}
		STARTUPINFOW startup{};
		startup.cb = sizeof(startup);
		PROCESS_INFORMATION process{};
		// Associate the child before any plugin code or descendant creation runs.
		if (!CreateProcessW(executable.c_str(), command.data(), nullptr, nullptr, FALSE,
				CREATE_SUSPENDED | CREATE_NO_WINDOW, nullptr, nullptr, &startup, &process))
		{
			fail(Error::MissingHelper, GetLastError());
			disarm();
			return false;
		}
		m_process.store(process.hProcess, std::memory_order_release);
		m_pid = process.dwProcessId;
		bool assigned = AssignProcessToJobObject(m_job, process.hProcess) != FALSE;
		if (!assigned)
		{
			fail(Error::InitializationFailed, GetLastError());
		}
		if (!assigned || fault().error != Error::None || ResumeThread(process.hThread) == DWORD(-1))
		{
			if (fault().error == Error::None)
			{
				fail(Error::InitializationFailed, GetLastError());
			}
			TerminateProcess(process.hProcess, static_cast<UINT>(Error::InitializationFailed));
			TerminateJobObject(m_job, static_cast<UINT>(Error::InitializationFailed));
			CloseHandle(process.hThread);
			disarm();
			return false;
		}
		CloseHandle(process.hThread);
		// Startup stays armed until the caller verifies a protocol handshake.
		return true;
	}
	void arm(Stage stage, DWORD timeoutMs) noexcept
	{
		disarm();
		m_stage.store(stage, std::memory_order_relaxed);
		m_deadline.store(GetTickCount64() + (timeoutMs ? timeoutMs : 1), std::memory_order_release);
	}
	void disarm() noexcept { m_deadline.store(0, std::memory_order_release); }
	void terminate(Error reason, DWORD nativeCode = 0, AudioFailure detail = AudioFailure::Unspecified,
		std::uint64_t sequence = 0, const AudioQueue::FullDiagnostic* full = nullptr) noexcept
	{
		std::lock_guard lock(m_terminationMutex);
		const auto process = m_process.load(std::memory_order_acquire);
		const bool exited = process && WaitForSingleObject(process, 0) == WAIT_OBJECT_0;
		if (exited && (reason == Error::Disconnected || reason == Error::Timeout))
		{
			DWORD exitCode = 0;
			if (GetExitCodeProcess(process, &exitCode))
			{
				nativeCode = exitCode;
				reason = exitCode ? Error::ProcessCrashed : Error::Disconnected;
			}
		}
		const bool first = fault().error == Error::None;
		fail(reason, nativeCode);
		disarm();
		if (first && process && !m_expectedExit.load(std::memory_order_acquire))
		{
			if (exited || reason == Error::ProcessCrashed)
			{
				reportSandboxFault(L"unexpected exit", GetProcessId(process), nativeCode);
			}
			else if (reason == Error::Timeout)
			{
				reportSandboxFault(L"timeout", GetProcessId(process), nativeCode);
			}
			else if (reason == Error::ProcessingFailed || reason == Error::InvalidMessage
				|| reason == Error::ProtocolMismatch || reason == Error::StaleSession)
			{
				reportSandboxFault(L"supervisor termination", GetProcessId(process), static_cast<DWORD>(reason),
					detail, sequence, full);
			}
		}
		if (m_job)
		{
			TerminateJobObject(m_job, static_cast<UINT>(reason));
		}
	}
	// Audio callbacks only publish a fault request. OS process termination and
	// diagnostics run on the independent watcher, never on the audio thread.
	void requestTermination(Error reason) noexcept
	{
		Error expected = Error::None;
		m_requestedError.compare_exchange_strong(expected, reason, std::memory_order_release);
	}
	// Source must outlive the supervisor. It is read even while the control
	// dispatcher is blocked awaiting a plugin's state or editor reply.
	void setAudioFaultSource(AudioFaultRequest* source) noexcept
	{
		m_audioFaultSource.store(source, std::memory_order_release);
	}
	void expectExit() noexcept
	{
		m_expectedExit.store(true, std::memory_order_release);
		m_stage.store(Stage::Shutdown, std::memory_order_relaxed);
	}
	void close(DWORD graceMs = 2000) noexcept
	{
		std::lock_guard lock(m_terminationMutex);
		const auto process = m_process.load(std::memory_order_acquire);
		if (process && !m_expectedExit.load(std::memory_order_acquire) && fault().error == Error::None
			&& WaitForSingleObject(process, 0) == WAIT_OBJECT_0)
		{
			DWORD code = 0;
			GetExitCodeProcess(process, &code);
			fail(code ? Error::ProcessCrashed : Error::Disconnected, code);
			reportSandboxFault(L"unexpected exit", GetProcessId(process), code);
		}
		expectExit();
		disarm();
		if (process && WaitForSingleObject(process, graceMs) != WAIT_OBJECT_0)
		{
			TerminateJobObject(m_job, 0);
			WaitForSingleObject(process, 2000);
		}
		// Even a normally exited root may have left descendants.
		if (m_job)
		{
			TerminateJobObject(m_job, 0);
		}
	}
	bool running() const noexcept
	{
		const auto process = m_process.load(std::memory_order_acquire);
		return process && WaitForSingleObject(process, 0) == WAIT_TIMEOUT;
	}
	DWORD pid() const noexcept { return m_pid; }
	HANDLE processHandle() const noexcept { return m_process.load(std::memory_order_acquire); }
	Fault fault() const noexcept
	{
		return {m_error.load(std::memory_order_acquire), m_faultStage.load(), m_nativeCode.load()};
	}
	static std::wstring quote(const std::wstring& argument)
	{
		std::wstring result = L"\"";
		std::size_t backslashes = 0;
		for (const auto character : argument)
		{
			if (character == L'\\')
			{
				++backslashes;
				continue;
			}
			result.append(character == L'"' ? 2 * backslashes + 1 : backslashes, L'\\');
			backslashes = 0;
			result += character;
		}
		result.append(2 * backslashes, L'\\');
		result += L'"';
		return result;
	}

private:
	static std::wstring formatFullDiagnostic(const AudioQueue::FullDiagnostic& full)
	{
		constexpr const wchar_t* states[] = {L"free", L"writing", L"submitted", L"processing", L"ready"};
		constexpr const wchar_t* activities[] = {L"unavailable", L"idle", L"checking control", L"control",
			L"claiming audio", L"processing audio", L"completing audio", L"pumping window messages",
			L"dispatching window message", L"waiting for instance mutex", L"plug-in audio call", L"audio rejected"};
		const auto stateName = [&](LONG state) {
			return state >= 0 && state < ARRAYSIZE(states) ? states[state] : L"unknown";
		};
		std::wstring text = L"Queue snapshot QPC: " + std::to_wstring(full.ticks)
			+ L" (ticks per second: " + std::to_wstring(full.frequency) + L")\n";
		const auto appendActivity = [&](const wchar_t* label, std::uint64_t packed) {
			const auto activity = packed & 0xFFFF;
			text += std::wstring(label) + L": TID " + std::to_wstring(packed >> 32) + L", "
				+ (activity < ARRAYSIZE(activities) ? activities[activity] : L"unknown")
				+ L", operation code " + std::to_wstring((packed >> 16) & 0xFFFF) + L"\n";
		};
		appendActivity(L"Helper dispatcher", full.dispatcher);
		appendActivity(L"Dedicated helper audio thread", full.audioThread);
		text += L"Slot QPC values are sampled independently (0 = stage not reached).\n";
		for (std::size_t i = 0; i < full.slotDiagnostics.size(); ++i)
		{
			const auto& slot = full.slotDiagnostics[i];
			text += L"Slot " + std::to_wstring(i) + L": observed=" + stateName(slot.observed)
				+ L" sampled=" + stateName(slot.sampled) + L" sequence=" + std::to_wstring(slot.sequence)
				+ L" submit=" + std::to_wstring(slot.ticks[0]) + L" claim=" + std::to_wstring(slot.ticks[1])
				+ L" complete=" + std::to_wstring(slot.ticks[2]) + L" reclaim=" + std::to_wstring(slot.ticks[3])
				+ L" last_reclaimed_sequence=" + std::to_wstring(slot.lastReclaimedSequence)
				+ L" last_reclaimed_at=" + std::to_wstring(slot.lastReclaimedTicks) + L"\n";
		}
		return text;
	}
	void reportSandboxFault(const wchar_t* reason, DWORD pid, DWORD code,
		AudioFailure detail = AudioFailure::Unspecified, std::uint64_t sequence = 0,
		const AudioQueue::FullDiagnostic* full = nullptr) noexcept
	{
		if (m_reporter.empty())
		{
			return;
		}
		using namespace windowsCrashReport::detail;
		try
		{
			constexpr const wchar_t* stages[] = {L"startup", L"scan", L"initialize", L"control", L"audio", L"shutdown"};
			auto command = quote(m_reporter) + L" --supervisor " + std::to_wstring(pid)
				+ L" " + quote(reason) + L" " + stages[static_cast<unsigned>(m_faultStage.load())]
				+ L" " + std::to_wstring(code)
				+ L" " + quote(m_reportDirectory) + L" " + quote(m_pluginPath);
			if (detail != AudioFailure::Unspecified)
			{
				constexpr const wchar_t* details[] = {L"", L"invalid result", L"invalid submission", L"queue full"};
				command += L" " + quote(details[static_cast<unsigned>(detail)]) + L" " + std::to_wstring(sequence);
			}
			if (full)
			{
				command += L" " + quote(formatFullDiagnostic(*full));
				const auto audioThread = full->audioThread ? full->audioThread : full->dispatcher;
				command += L" " + std::to_wstring(audioThread >> 32);
			}
			STARTUPINFOW startup{};
			startup.cb = sizeof(startup);
			PROCESS_INFORMATION reporter{};
			if (!CreateProcessW(m_reporter.c_str(), command.data(), nullptr, nullptr, FALSE,
					CREATE_NO_WINDOW, nullptr, nullptr, &startup, &reporter))
			{
				reportFailure(L"starting sandbox reporter", GetLastError());
				return;
			}
			waitForReporter(reporter.hProcess, 2000);
			CloseHandle(reporter.hThread);
			CloseHandle(reporter.hProcess);
		}
		catch (...)
		{
			reportFailure(L"preparing sandbox report", ERROR_NOT_ENOUGH_MEMORY);
		}
	}

	void fail(Error error, DWORD nativeCode) noexcept
	{
		if (!m_faultClaimed.test_and_set(std::memory_order_acq_rel))
		{
			m_nativeCode.store(nativeCode);
			m_faultStage.store(m_stage.load());
			m_error.store(error, std::memory_order_release);
		}
	}
	HANDLE m_job = nullptr;
	std::mutex m_terminationMutex;
	std::wstring m_reporter, m_reportDirectory, m_pluginPath;
	std::atomic<HANDLE> m_process{nullptr};
	DWORD m_pid = 0;
	std::atomic<ULONGLONG> m_deadline{0};
	std::atomic<Stage> m_stage{Stage::Startup}, m_faultStage{Stage::Startup};
	std::atomic<Error> m_error{Error::None};
	std::atomic<Error> m_requestedError{Error::None};
	std::atomic<AudioFaultRequest*> m_audioFaultSource{nullptr};
	std::atomic_flag m_faultClaimed = ATOMIC_FLAG_INIT;
	std::atomic<DWORD> m_nativeCode{0};
	std::atomic<bool> m_expectedExit{false};
	std::jthread m_watcher;
};
} // namespace lmms::vsthost
#endif
