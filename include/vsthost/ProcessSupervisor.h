#ifndef LMMS_VSTHOST_PROCESS_SUPERVISOR_H
#define LMMS_VSTHOST_PROCESS_SUPERVISOR_H

#include "vsthost/Protocol.h"
#include <atomic>
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
					= source ? source->exchange(Error::None, std::memory_order_acq_rel) : Error::None;
				if (requested != Error::None || audioRequested != Error::None)
				{
					m_stage.store(Stage::Audio, std::memory_order_relaxed);
					terminate(requested != Error::None ? requested : audioRequested);
				}
				auto deadline = m_deadline.load(std::memory_order_acquire);
				if (deadline && GetTickCount64() >= deadline)
				{
					// A cancelled/replaced deadline must not kill the next operation.
					if (m_deadline.compare_exchange_strong(deadline, 0, std::memory_order_acq_rel))
					{
						fail(Error::Timeout, WAIT_TIMEOUT);
						TerminateJobObject(m_job, static_cast<UINT>(Error::Timeout));
					}
				}
				const auto process = m_process.load(std::memory_order_acquire);
				if (process && WaitForSingleObject(process, 0) == WAIT_OBJECT_0)
				{
					DWORD code = 0;
					GetExitCodeProcess(process, &code);
					if (!m_expectedExit.load(std::memory_order_acquire))
					{
						fail(code ? Error::ProcessCrashed : Error::Disconnected, code);
						// Child exit also tears down any surviving descendants.
						TerminateJobObject(m_job, code);
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
		const std::wstring& executable, const std::vector<std::wstring>& arguments, DWORD startupTimeoutMs = 30000)
	{
		if (!m_job || m_process.load() || fault().error != Error::None)
		{
			return false;
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
	void terminate(Error reason, DWORD nativeCode = 0) noexcept
	{
		fail(reason, nativeCode);
		if (m_job)
		{
			TerminateJobObject(m_job, static_cast<UINT>(reason));
		}
		disarm();
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
	void setAudioFaultSource(std::atomic<Error>* source) noexcept
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
		expectExit();
		disarm();
		const auto process = m_process.load(std::memory_order_acquire);
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
	std::atomic<HANDLE> m_process{nullptr};
	DWORD m_pid = 0;
	std::atomic<ULONGLONG> m_deadline{0};
	std::atomic<Stage> m_stage{Stage::Startup}, m_faultStage{Stage::Startup};
	std::atomic<Error> m_error{Error::None};
	std::atomic<Error> m_requestedError{Error::None};
	std::atomic<std::atomic<Error>*> m_audioFaultSource{nullptr};
	std::atomic_flag m_faultClaimed = ATOMIC_FLAG_INIT;
	std::atomic<DWORD> m_nativeCode{0};
	std::atomic<bool> m_expectedExit{false};
	std::jthread m_watcher;
};
} // namespace lmms::vsthost
#endif
