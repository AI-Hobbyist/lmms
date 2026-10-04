#ifndef LMMS_VSTHOST_HOST_SESSION_H
#define LMMS_VSTHOST_HOST_SESSION_H

#include "vsthost/AudioQueue.h"
#include "vsthost/ControlChannel.h"
#include "vsthost/ProcessSupervisor.h"
#include "vsthost/SharedRegion.h"
#include <condition_variable>
#include <deque>
#include <future>
#include <functional>
#include <optional>
#include <memory>
#include <mutex>
#include <bcrypt.h>

namespace lmms::vsthost
{
// All lifecycle/control operations execute on one private dispatcher. The caller
// receives a future; process() touches only preallocated audio bytes and atomics.
class HostSession
{
public:
	struct Configuration
	{
		std::wstring helper;
		std::vector<std::wstring> arguments;
		DWORD startupMs = 30000;
	};
	struct Reply { Error error = Error::None; std::vector<std::uint8_t> payload; std::vector<std::uint8_t> followupPayload; };
	struct Followup { MessageType type; std::function<std::vector<std::uint8_t>()> prepare; };
	explicit HostSession(std::uint32_t controlCapacity = ControlChannel::DefaultCapacity)
		: m_capacity(controlCapacity)
	{
		if (BCryptGenRandom(nullptr, reinterpret_cast<PUCHAR>(&m_session), sizeof(m_session), BCRYPT_USE_SYSTEM_PREFERRED_RNG) < 0 || !m_session)
		{ m_error.store(Error::InitializationFailed); m_session = 0; }
		m_baseName = L"Local\\LMMS-VstHost-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(m_session);
		m_dispatcher = std::jthread([this](std::stop_token stop)
		{
			while (!stop.stop_requested())
			{
				std::packaged_task<Reply()> task;
				{
					std::unique_lock lock(m_tasksMutex);
					m_tasksAvailable.wait_for(lock, std::chrono::milliseconds(1), [&] { return !m_tasks.empty() || stop.stop_requested(); });
					if (!m_tasks.empty()) { task = std::move(m_tasks.front()); m_tasks.pop_front(); }
				}
				if (task.valid()) { task(); }
				observeFaults();
				if (m_cancelled.load(std::memory_order_acquire)) { closeImpl(); }
			}
			closeImpl();
		});
	}
	~HostSession()
	{
		m_cancelled.store(true, std::memory_order_release);
		m_audioAllowed.store(false, std::memory_order_release);
		m_dispatcher.request_stop(); m_tasksAvailable.notify_all();
		if (m_dispatcher.joinable()) { m_dispatcher.join(); }
		m_supervisor.reset();
	}
	HostSession(const HostSession&) = delete;
	HostSession& operator=(const HostSession&) = delete;
	// Permanently cancel this disposable session. The dispatcher breaks its
	// bounded receive wait and closes the Job; callers never touch native handles.
	void cancel() noexcept
	{
		m_cancelled.store(true, std::memory_order_release);
		m_audioAllowed.store(false, std::memory_order_release);
		m_tasksAvailable.notify_all();
	}
	std::future<Reply> open(Configuration configuration)
	{ return enqueue([this, configuration = std::move(configuration)] { return openImpl(configuration); }); }
	std::future<Reply> close()
	{ return enqueue([this] { return closeImpl(); }); }
	std::future<Reply> request(MessageType type, std::vector<std::uint8_t> payload,
		DWORD timeoutMs = 15000, bool pauseAudio = false)
	{
		return requestPrepared(type, [payload = std::move(payload)]() mutable { return std::move(payload); }, timeoutMs, pauseAudio);
	}
	template<class Prepare> std::future<Reply> requestPrepared(MessageType type, Prepare prepare,
		DWORD timeoutMs = 15000, bool pauseAudio = false)
	{ return requestPreparedCompleted(type, std::move(prepare), [](Reply&) {}, timeoutMs, pauseAudio); }
	template<class Prepare, class Complete> std::future<Reply> requestPreparedCompleted(MessageType type, Prepare prepare,
		Complete complete, DWORD timeoutMs = 15000, bool pauseAudio = false)
	{
		return enqueue([this, type, prepare = std::move(prepare), complete = std::move(complete), timeoutMs, pauseAudio]() mutable
		{
			if (state() != SessionState::Ready) { return Reply{Error::InvalidState, {}}; }
			if (pauseAudio)
			{
				if (!quiesce()) { return faultReply(Error::Timeout); }
				const auto paused = exchange(MessageType::Pause, {}, timeoutMs, ProcessSupervisor::Stage::Control);
				if (paused.error != Error::None) { return paused; }
					if (!retirePausedAudio()) { return faultReply(Error::InvalidMessage); }
			}
			const auto payload = prepare();
			const auto stage = type == MessageType::Scan ? ProcessSupervisor::Stage::Scan :
				type == MessageType::Create ? ProcessSupervisor::Stage::Initialize : ProcessSupervisor::Stage::Control;
			auto result = exchange(type, payload, timeoutMs, stage);
			if (result.error == Error::None) { complete(result); }
			if (pauseAudio && result.error == Error::None)
			{
				const auto resumed = exchange(MessageType::Resume, {}, timeoutMs, ProcessSupervisor::Stage::Control);
				if (resumed.error != Error::None) { return resumed; }
				m_havePrevious = false;
				m_audioAllowed.store(true, std::memory_order_release);
			}
			return result;
		});
	}
	// Synchronous control-owner publication while the dispatcher keeps audio
	// quiescent. The callback may update the caller's model under its existing
	// lock; it must not issue another session request or wait for audio.
	template<class Prepare, class Publish> Reply requestPreparedPublished(MessageType type, Prepare prepare,
		Publish publish, DWORD timeoutMs = 15000)
	{
		return requestPreparedFollowedPublished(type, std::move(prepare),
			[](Reply&) -> std::optional<Followup> { return {}; }, std::move(publish), timeoutMs);
	}
	// A reply may require one follow-up request (for example refreshed metadata).
	// Both exchanges share one pause/resume boundary. A read-only first request
	// may defer quiescence until its reply actually requests a follow-up.
	template<class Prepare, class Follow, class Publish> Reply requestPreparedFollowedPublished(MessageType type,
		Prepare prepare, Follow follow, Publish publish, DWORD timeoutMs = 15000, bool pauseAudio = true)
	{
		std::promise<Reply*> prepared;
		auto publication = prepared.get_future();
		std::promise<void> published;
		auto acknowledgement = published.get_future();
		auto result = enqueue([&, type, timeoutMs, pauseAudio]() mutable
		{
			if (state() != SessionState::Ready) { return Reply{Error::InvalidState, {}}; }
			bool paused = false;
			const auto pause = [&]() -> Reply {
				if (!quiesce()) { return faultReply(Error::Timeout); }
				auto reply = exchange(MessageType::Pause, {}, timeoutMs, ProcessSupervisor::Stage::Control);
				paused = reply.error == Error::None;
				if (paused && !retirePausedAudio()) { return faultReply(Error::InvalidMessage); }
				return reply;
			};
			if (pauseAudio)
			{
				const auto reply = pause(); if (reply.error != Error::None) { return reply; }
			}
			const auto payload = prepare();
			const auto stage = type == MessageType::Scan ? ProcessSupervisor::Stage::Scan :
				type == MessageType::Create ? ProcessSupervisor::Stage::Initialize : ProcessSupervisor::Stage::Control;
			auto reply = exchange(type, payload, timeoutMs, stage);
			if (reply.error != Error::None) { return reply; }
			if (auto next = follow(reply); next && reply.error == Error::None)
			{
				if (!paused)
				{
					const auto barrier = pause(); if (barrier.error != Error::None) { return barrier; }
				}
				const auto nextPayload = next->prepare();
				reply = exchange(next->type, nextPayload, timeoutMs, ProcessSupervisor::Stage::Control);
			}
			if (reply.error != Error::None) { return reply; }
			prepared.set_value(&reply); acknowledgement.get();
			if (paused && reply.error == Error::None)
			{
				const auto resumed = exchange(MessageType::Resume, {}, timeoutMs, ProcessSupervisor::Stage::Control);
				if (resumed.error != Error::None) { return resumed; }
				m_havePrevious = false; m_audioAllowed.store(true, std::memory_order_release);
			}
			return reply;
		});
		while (publication.wait_for(std::chrono::milliseconds(1)) != std::future_status::ready)
		{
			// Requests that fail before completion never enter publication.
			if (result.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready) { return result.get(); }
		}
		try { publish(*publication.get()); published.set_value(); }
		catch (...) { published.set_exception(std::current_exception()); }
		return result.get();
	}
	// Include caller-side assembly in the session's audio admission barrier.
	template<class Process> bool withAudio(Process process) noexcept
	{
		if (!m_audioAllowed.load(std::memory_order_acquire)) { return false; }
		m_audioUsers.fetch_add(1, std::memory_order_acq_rel);
		struct Exit { std::atomic<unsigned>& users; ~Exit() { users.fetch_sub(1, std::memory_order_release); } } exit{m_audioUsers};
		if (!m_audioAllowed.load(std::memory_order_acquire)) { return false; }
		return process();
	}
	// Exactly one audio producer. First block returns silence; later blocks consume
	// only the preceding sequence. A missed deadline faults instead of using late audio.
	bool process(AudioQueue::Layout layout, std::span<const float> input, std::span<float> output,
		std::span<const std::uint8_t> events = {}) noexcept
	{
		return processPrepared(layout, input, output, [events] { return events; });
	}
	template<class Prepare> bool processPrepared(AudioQueue::Layout layout, std::span<const float> input,
		std::span<float> output, Prepare prepare, std::span<std::uint8_t> outputEvents = {},
		std::uint32_t* outputEventBytes = nullptr) noexcept
	{
		// The audio owner supplies reusable storage; this path never allocates
		// or waits. Feedback belongs to the same preceding sequence as audio.
		if (outputEventBytes) { *outputEventBytes = 0; }
		std::fill(output.begin(), output.end(), 0.0f);
		if (!m_audioAllowed.load(std::memory_order_acquire)) { return false; }
		m_audioUsers.fetch_add(1, std::memory_order_acq_rel);
		struct Exit { std::atomic<unsigned>& users; ~Exit() { users.fetch_sub(1, std::memory_order_release); } } exit{m_audioUsers};
		if (!m_audioAllowed.load(std::memory_order_acquire)) { return false; }
		if (!m_audio || output.size() != std::uint64_t(layout.frames) * layout.outputs || m_audioSequence == UINT64_MAX)
		{ audioFault(Error::InvalidMessage); return false; }
		if (m_havePrevious)
		{
			if (m_audio->receive(m_session, generation(), m_audioSequence - 1, output, outputEvents, outputEventBytes) != AudioQueue::Result::Ok)
			{ audioFault(Error::ProcessingFailed); return false; }
		}
		const Header header{MessageType::Process, m_session, generation(), m_audioSequence, 0};
		if (m_audio->submit(header, layout, input, prepare(), outputEventBytes != nullptr) != AudioQueue::Result::Ok)
		{
			std::fill(output.begin(), output.end(), 0.0f);
			audioFault(Error::ProcessingFailed); return false;
		}
		++m_audioSequence; m_havePrevious = true;
		return true;
	}
	// Renderer-only request. Audio callbacks use processPrepared and never wait.
	template<class Prepare> std::future<Reply> renderOffline(AudioQueue::Layout layout,
		std::vector<float> input, Prepare prepare, DWORD timeoutMs = 5000, std::optional<MessageType> followup = {})
	{
		return enqueue([this, layout, input = std::move(input), prepare = std::move(prepare), timeoutMs, followup]() mutable
		{
			if (state() != SessionState::Ready || !quiesce()) { return Reply{Error::InvalidState, {}}; }
			auto barrier = exchange(MessageType::Pause, {}, timeoutMs, ProcessSupervisor::Stage::Control);
			if (barrier.error != Error::None) { return barrier; }
			if (!retirePausedAudio()) { return faultReply(Error::InvalidMessage); }
				barrier = exchange(MessageType::Resume, {}, timeoutMs, ProcessSupervisor::Stage::Control);
			if (barrier.error != Error::None) { return barrier; }
			if (!layout.frames || layout.frames > AudioQueue::MaxFrames || layout.outputs > AudioQueue::MaxChannels ||
				m_audioSequence == UINT64_MAX) { return faultReply(Error::InvalidMessage); }
			const auto sequence = m_audioSequence++;
			const Header header{MessageType::Process, m_session, generation(), sequence, 0};
			std::vector<float> output(layout.frames * layout.outputs);
			const bool collectOutput = followup == MessageType::Midi;
			std::array<std::uint8_t, AudioQueue::MaxOutputEventBytes> outputEvents{};
			std::uint32_t outputEventBytes = 0;
			m_havePrevious = false;
			m_supervisor->arm(ProcessSupervisor::Stage::Audio, timeoutMs);
			if (m_audio->submit(header, layout, input, prepare(), collectOutput) != AudioQueue::Result::Ok) { return faultReply(Error::InvalidMessage); }
			const auto deadline = GetTickCount64() + timeoutMs;
			while (true)
			{
				const auto result = m_audio->receive(m_session, generation(), sequence, output, outputEvents, &outputEventBytes);
				if (result == AudioQueue::Result::Ok) { break; }
				if (result == AudioQueue::Result::Invalid) { return faultReply(Error::InvalidMessage); }
				if (const auto error = m_supervisor->fault().error; error != Error::None) { return faultReply(error); }
				if (m_cancelled.load() || GetTickCount64() >= deadline) { return faultReply(Error::Timeout); }
				Sleep(1);
			}
			m_supervisor->disarm();
			Reply reply; reply.payload.resize(output.size() * sizeof(float));
			std::memcpy(reply.payload.data(), output.data(), reply.payload.size());
			// Harvest renderer feedback in this same dispatcher transaction while
			// realtime admission is still closed. A queued UI poll cannot steal it.
			if (collectOutput)
			{ reply.followupPayload.assign(outputEvents.begin(), outputEvents.begin() + outputEventBytes); }
			else if (followup)
			{
				auto harvested = exchange(*followup, {}, timeoutMs, ProcessSupervisor::Stage::Control);
				if (harvested.error != Error::None) { return harvested; }
				reply.followupPayload = std::move(harvested.payload);
			}
			m_audioAllowed.store(true, std::memory_order_release); return reply;
		});
	}
	void failRealtime(Error error) noexcept { audioFault(error); }
	SessionState state() const noexcept { return m_state.load(std::memory_order_acquire); }
	Error error() const noexcept { return m_error.load(std::memory_order_acquire); }
	ProcessSupervisor::Fault fault() const noexcept
	{ return {error(), m_faultStage.load(std::memory_order_acquire), m_nativeCode.load(std::memory_order_acquire)}; }
	std::uint64_t id() const noexcept { return m_session; }
	std::uint64_t generation() const noexcept { return m_generation.load(std::memory_order_acquire); }
	DWORD pid() const noexcept { return m_pid.load(std::memory_order_acquire); }
private:
	template<class Function> std::future<Reply> enqueue(Function function)
	{
		std::packaged_task<Reply()> task([this, function = std::move(function)]() mutable
		{
			if (m_cancelled.load(std::memory_order_acquire)) { return Reply{Error::InvalidState, {}}; }
			try { return function(); }
			catch (...) { return faultReply(Error::InitializationFailed); }
		});
		auto future = task.get_future();
		{ std::lock_guard lock(m_tasksMutex); m_tasks.push_back(std::move(task)); }
		m_tasksAvailable.notify_one(); return future;
	}
	bool retirePausedAudio() noexcept
	{
		if (!m_audio || !m_audio->discardCompleted(m_session, generation(), m_audioSequence)) { return false; }
		m_havePrevious = false; return true;
	}
	bool quiesce() noexcept
	{
		m_audioAllowed.store(false, std::memory_order_release);
		const auto deadline = GetTickCount64() + 2000;
		while (m_audioUsers.load(std::memory_order_acquire))
		{ if (GetTickCount64() >= deadline) { return false; } Sleep(1); }
		return true;
	}
	void audioFault(Error error) noexcept
	{
		m_audioFault.store(error, std::memory_order_release);
		m_audioAllowed.store(false, std::memory_order_release);
	}
	void observeFaults() noexcept
	{
		if (!m_supervisor) { return; }
		const auto fault = m_supervisor->fault();
		if (fault.error != Error::None && state() != SessionState::Stopped && state() != SessionState::Stopping)
		{
			m_audioAllowed.store(false, std::memory_order_release);
			m_nativeCode.store(fault.nativeCode); m_faultStage.store(fault.stage);
			m_error.store(fault.error, std::memory_order_release);
			m_state.store(SessionState::Faulted, std::memory_order_release);
		}
	}
	Reply faultReply(Error error, DWORD nativeCode = 0) noexcept
	{
		m_audioAllowed.store(false, std::memory_order_release);
		if (error == Error::Timeout && !nativeCode) { nativeCode = WAIT_TIMEOUT; }
		if (m_supervisor)
		{
			m_supervisor->terminate(error, nativeCode);
			const auto first = m_supervisor->fault();
			error = first.error; m_nativeCode.store(first.nativeCode); m_faultStage.store(first.stage);
		}
		m_error.store(error, std::memory_order_release);
		m_state.store(SessionState::Faulted, std::memory_order_release);
		return {error, {}};
	}
	Reply exchange(MessageType type, std::span<const std::uint8_t> payload, DWORD timeoutMs, ProcessSupervisor::Stage stage)
	{
		if (!m_control || !m_supervisor || payload.size() > m_capacity || m_controlSequence == UINT64_MAX)
		{ return faultReply(Error::InvalidMessage); }
		const Header header{type, m_session, generation(), ++m_controlSequence, static_cast<std::uint32_t>(payload.size())};
		m_supervisor->arm(stage, timeoutMs);
		if (const auto error = m_control->send(header, payload); error != Error::None) { return faultReply(error); }
		const auto deadline = GetTickCount64() + timeoutMs;
		ControlChannel::Frame frame;
		while (true)
		{
			const auto now = GetTickCount64();
			const auto remaining = now < deadline ? static_cast<DWORD>(deadline - now) : 0;
			const auto error = m_control->wait(frame, remaining, [&]
			{
				return !m_cancelled.load(std::memory_order_acquire) && m_supervisor->running() && m_supervisor->fault().error == Error::None;
			});
			if (error != Error::None)
			{
				const auto supervised = m_supervisor->fault().error;
				if (supervised == Error::None && error == Error::Disconnected && !m_supervisor->running())
				{
					// The process handle can signal before the watchdog's next poll.
					// Preserve the native crash distinction in that interval.
					DWORD exitCode = 0;
					if (GetExitCodeProcess(m_supervisor->processHandle(), &exitCode) && exitCode)
						{ return faultReply(Error::ProcessCrashed, exitCode); }
				}
				return faultReply(supervised != Error::None ? supervised : error);
			}
			if (frame.header.session != m_session || frame.header.generation > generation()) { return faultReply(Error::StaleSession); }
			if (frame.header.generation < generation())
			{ if (GetTickCount64() >= deadline) { return faultReply(Error::Timeout); } continue; }
			if (frame.header.sequence != header.sequence) { return faultReply(Error::InvalidMessage); }
			if (frame.header.type == MessageType::Fault)
			{
				if (frame.payload.size() != 4) { return faultReply(Error::InvalidMessage); }
				const auto code = get(frame.payload, 0, 4);
				if (!code || code > static_cast<unsigned>(Error::ProcessingFailed)) { return faultReply(Error::InvalidMessage); }
				return faultReply(static_cast<Error>(code));
			}
			const auto expectedType = type == MessageType::Scan ? MessageType::ScanResult : type;
			if (frame.header.type != expectedType) { return faultReply(Error::InvalidMessage); }
			m_supervisor->disarm(); return {Error::None, std::move(frame.payload)};
		}
	}
	Reply openImpl(const Configuration& configuration)
	{
		if (!m_session) { return faultReply(Error::InitializationFailed); }
		if (m_supervisor) { closeImpl(); }
		if (!quiesce()) { return faultReply(Error::Timeout); }
		if (!m_control)
		{
			if (!ControlChannel::storageBytes(m_capacity) ||
				!m_controlRegion.create(m_baseName + L"-control", ControlChannel::storageBytes(m_capacity)) ||
				!m_audioRegion.create(m_baseName + L"-audio", AudioQueue::StorageBytes))
			{ return faultReply(Error::InitializationFailed); }
			m_control = std::make_unique<ControlChannel>(m_controlRegion.bytes(), ControlChannel::Side::Host, m_capacity);
			m_audio = std::make_unique<AudioQueue>(m_audioRegion.bytes());
		}
		if (generation() == UINT64_MAX) { return faultReply(Error::InvalidState); }
		m_generation.fetch_add(1, std::memory_order_release);
		m_error.store(Error::None); m_audioFault.store(Error::None);
		m_nativeCode.store(0); m_faultStage.store(ProcessSupervisor::Stage::Startup);
		m_state.store(SessionState::Starting, std::memory_order_release);
		m_audioSequence = 0; m_controlSequence = 0; m_havePrevious = false;
		if (!m_control->initialize() || !m_audio->initialize()) { return faultReply(Error::InitializationFailed); }
		m_supervisor = std::make_unique<ProcessSupervisor>();
		m_supervisor->setAudioFaultSource(&m_audioFault);
		auto arguments = configuration.arguments;
		arguments.insert(arguments.end(), {L"--host-session", m_baseName + L"-control", m_baseName + L"-audio",
			std::to_wstring(m_session), std::to_wstring(generation()), std::to_wstring(m_capacity)});
		if (!m_supervisor->start(configuration.helper, arguments, configuration.startupMs))
		{ return faultReply(m_supervisor->fault().error); }
		m_pid.store(m_supervisor->pid(), std::memory_order_release);
		auto reply = exchange(MessageType::Hello, {}, configuration.startupMs, ProcessSupervisor::Stage::Startup);
		if (reply.error != Error::None) { return reply; }
		m_state.store(SessionState::Ready, std::memory_order_release);
		m_audioAllowed.store(true, std::memory_order_release);
		return reply;
	}
	Reply closeImpl() noexcept
	{
		Reply result;
		if (!quiesce()) { return faultReply(Error::Timeout); }
		m_state.store(SessionState::Stopping, std::memory_order_release);
		if (m_supervisor)
		{
			if (!m_cancelled.load() && m_supervisor->running() && m_supervisor->fault().error == Error::None)
			{
				m_supervisor->expectExit();
				try { result = exchange(MessageType::Close, {}, 2000, ProcessSupervisor::Stage::Shutdown); }
				catch (...) { m_supervisor->terminate(Error::InvalidState); result.error = Error::InvalidState; }
			}
			m_supervisor->close(0); m_supervisor.reset();
		}
		m_pid.store(0, std::memory_order_release);
		m_state.store(SessionState::Stopped, std::memory_order_release);
		return result;
	}
	std::uint32_t m_capacity;
	std::uint64_t m_session = 0, m_audioSequence = 0, m_controlSequence = 0;
	bool m_havePrevious = false;
	std::wstring m_baseName;
	SharedRegion m_controlRegion, m_audioRegion;
	std::unique_ptr<ControlChannel> m_control;
	std::unique_ptr<AudioQueue> m_audio;
	std::unique_ptr<ProcessSupervisor> m_supervisor;
	std::atomic<std::uint64_t> m_generation{0};
	std::atomic<DWORD> m_pid{0};
	std::atomic<DWORD> m_nativeCode{0};
	std::atomic<ProcessSupervisor::Stage> m_faultStage{ProcessSupervisor::Stage::Startup};
	std::atomic<SessionState> m_state{SessionState::Stopped};
	std::atomic<Error> m_error{Error::None}, m_audioFault{Error::None};
	std::atomic<bool> m_audioAllowed{false}, m_cancelled{false};
	std::atomic<unsigned> m_audioUsers{0};
	std::mutex m_tasksMutex;
	std::condition_variable m_tasksAvailable;
	std::deque<std::packaged_task<Reply()>> m_tasks;
	std::jthread m_dispatcher;
};
} // namespace lmms::vsthost
#endif
