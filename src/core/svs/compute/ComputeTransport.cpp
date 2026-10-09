#include "ComputeTransport.h"

namespace svsc {
void Worker::launch(const fs::path& directory)
{
#ifdef _WIN32
	const auto executable = directory / "SVSComputeWorker.exe";
	if (backend == "directml" && !fs::is_regular_file(directory / "DirectML.dll"))
	{
		throw Error(SVSC_UNAVAILABLE, "Optional DirectML runtime is missing");
	}
	if (!fs::is_regular_file(executable))
	{
		throw Error(SVSC_UNAVAILABLE, "Missing SVSComputeWorker.exe");
	}
	SECURITY_ATTRIBUTES security{sizeof(security), nullptr, TRUE};
	HANDLE childInput = nullptr, childOutput = nullptr;
	if (!CreatePipe(&childInput, &input, &security, 0) || !CreatePipe(&output, &childOutput, &security, 0))
	{
		if (childInput)
		{
			CloseHandle(childInput);
		}
		if (childOutput)
		{
			CloseHandle(childOutput);
		}
		stop();
		throw Error(SVSC_UNAVAILABLE, "Cannot create worker pipes");
	}
	SetHandleInformation(input, HANDLE_FLAG_INHERIT, 0);
	SetHandleInformation(output, HANDLE_FLAG_INHERIT, 0);
	// Keep worker diagnostics in an explicit native log; stdout is the framed IPC channel.
	const auto logPath = fs::temp_directory_path() / (epoch + ".log");
	HANDLE diagnostic = CreateFileW(logPath.c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, &security,
									CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
	if (diagnostic == INVALID_HANDLE_VALUE)
	{
		CloseHandle(childInput);
		CloseHandle(childOutput);
		stop();
		throw Error(SVSC_UNAVAILABLE, "Cannot open worker diagnostic log");
	}
	STARTUPINFOEXW startup{};
	startup.StartupInfo.cb = sizeof(startup);
	startup.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
	startup.StartupInfo.hStdInput = childInput;
	startup.StartupInfo.hStdOutput = childOutput;
	startup.StartupInfo.hStdError = diagnostic;
	SIZE_T attributeSize = 0;
	InitializeProcThreadAttributeList(nullptr, 1, 0, &attributeSize);
	std::vector<uint8_t> attributes(attributeSize);
	startup.lpAttributeList = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(attributes.data());
	const bool initialized = InitializeProcThreadAttributeList(startup.lpAttributeList, 1, 0, &attributeSize);
	HANDLE inherited[]{childInput, childOutput, diagnostic};
	const bool updated = initialized
		&& UpdateProcThreadAttribute(startup.lpAttributeList, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, inherited,
									 sizeof(inherited), nullptr, nullptr);
	std::wstring command = L"\"" + executable.wstring() + L"\" " + fs::u8path(backend).wstring() + L" "
		+ fs::u8path(device).wstring() + L" " + fs::u8path(epoch).wstring();
	PROCESS_INFORMATION info{};
	const bool created = updated
		&& CreateProcessW(executable.c_str(), command.data(), nullptr, nullptr, TRUE,
						  CREATE_NO_WINDOW | EXTENDED_STARTUPINFO_PRESENT, nullptr, directory.c_str(),
						  &startup.StartupInfo, &info);
	if (initialized)
	{
		DeleteProcThreadAttributeList(startup.lpAttributeList);
	}
	CloseHandle(childInput);
	CloseHandle(childOutput);
	CloseHandle(diagnostic);
	if (!created)
	{
		stop();
		throw Error(SVSC_UNAVAILABLE, "Cannot start isolated compute worker");
	}
	process = info.hProcess;
	CloseHandle(info.hThread);
#else
	const auto executable = directory / "SVSComputeWorker";
	if (!fs::is_regular_file(executable))
	{
		throw Error(SVSC_UNAVAILABLE, "Missing SVSComputeWorker");
	}
	int request[2]{-1, -1}, response[2]{-1, -1};
	if (pipe(request) || pipe(response))
	{
		for (const auto descriptor : {request[0], request[1], response[0], response[1]})
		{
			if (descriptor >= 0)
			{
				::close(descriptor);
			}
		}
		throw Error(SVSC_UNAVAILABLE, "Cannot create worker pipes");
	}
	process = fork();
	if (process == 0)
	{
		dup2(request[0], STDIN_FILENO);
		dup2(response[1], STDOUT_FILENO);
		::close(request[0]);
		::close(request[1]);
		::close(response[0]);
		::close(response[1]);
		execl(executable.c_str(), executable.c_str(), backend.c_str(), device.c_str(), epoch.c_str(), nullptr);
		_exit(127);
	}
	::close(request[0]);
	::close(response[1]);
	input = request[1];
	output = response[0];
	if (process < 0)
	{
		stop();
		throw Error(SVSC_UNAVAILABLE, "Cannot fork compute worker");
	}
#endif
}

void Worker::stop()
{
#ifdef _WIN32
	if (input)
	{
		CloseHandle(input);
		input = nullptr;
	}
	if (process)
	{
		if (WaitForSingleObject(process, 5000) != WAIT_OBJECT_0)
		{
			TerminateProcess(process, 1);
			WaitForSingleObject(process, 1000);
		}
		CloseHandle(process);
		process = nullptr;
	}
	if (output)
	{
		CloseHandle(output);
		output = nullptr;
	}
#else
	if (input >= 0)
	{
		::close(input);
		input = -1;
	}
	if (process > 0)
	{
		const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
		while (waitpid(process, nullptr, WNOHANG) == 0)
		{
			if (std::chrono::steady_clock::now() >= deadline)
			{
				kill(process, SIGKILL);
				waitpid(process, nullptr, 0);
				break;
			}
			std::this_thread::sleep_for(std::chrono::milliseconds(5));
		}
		process = -1;
	}
	if (output >= 0)
	{
		::close(output);
		output = -1;
	}
#endif
}

void Worker::write(const void* data, size_t size)
{
#ifndef _WIN32
	// Contain a dead worker's SIGPIPE in the caller thread; never change the host's signal handlers.
	struct PipeSignal
	{
		sigset_t mask{}, previous{}, pending{};
		PipeSignal()
		{
			sigemptyset(&mask);
			sigaddset(&mask, SIGPIPE);
			if (pthread_sigmask(SIG_BLOCK, &mask, &previous) != 0)
			{
				throw Error(SVSC_UNAVAILABLE, "Cannot isolate worker pipe signal");
			}
			sigpending(&pending);
		}
		~PipeSignal()
		{
			if (!sigismember(&pending, SIGPIPE) && !sigismember(&previous, SIGPIPE))
			{
				sigset_t current{};
				sigpending(&current);
				if (sigismember(&current, SIGPIPE))
				{
					int signal = 0;
					sigwait(&mask, &signal);
				}
			}
			pthread_sigmask(SIG_SETMASK, &previous, nullptr);
		}
	} pipeSignal;
#endif
	const auto* bytes = static_cast<const uint8_t*>(data);
	while (size)
	{
#ifdef _WIN32
		DWORD count = 0;
		if (!WriteFile(input, bytes, DWORD(size), &count, nullptr) || !count)
#else
		const auto count = ::write(input, bytes, size);
		if (count < 0 && errno == EINTR)
		{
			continue;
		}
		if (count <= 0)
#endif
		{
			throw Error(SVSC_WORKER_LOST, "Worker IPC write disconnected");
		}
		bytes += count;
		size -= size_t(count);
	}
}

void Worker::read(void* data, size_t size, std::chrono::steady_clock::time_point deadline)
{
	auto* bytes = static_cast<uint8_t*>(data);
	while (size)
	{
		if (std::chrono::steady_clock::now() >= deadline)
		{
			throw Error(SVSC_WORKER_LOST, "Worker IPC timeout; epoch invalidated");
		}
#ifdef _WIN32
		DWORD available = 0;
		if (!PeekNamedPipe(output, nullptr, 0, nullptr, &available, nullptr))
		{
			throw Error(SVSC_WORKER_LOST, "Worker IPC disconnected");
		}
		if (!available)
		{
			if (WaitForSingleObject(process, 0) == WAIT_OBJECT_0)
			{
				throw Error(SVSC_WORKER_LOST, "Worker process exited");
			}
			std::this_thread::sleep_for(std::chrono::milliseconds(5));
			continue;
		}
		DWORD count = 0;
		if (!ReadFile(output, bytes, DWORD(std::min(size, size_t(available))), &count, nullptr) || !count)
#else
		pollfd descriptor{output, POLLIN, 0};
		if (poll(&descriptor, 1, 5) == 0)
		{
			continue;
		}
		const auto count = ::read(output, bytes, size);
		if (count <= 0)
#endif
		{
			throw Error(SVSC_WORKER_LOST, "Worker IPC read disconnected");
		}
		bytes += count;
		size -= size_t(count);
	}
}
} // namespace svsc
