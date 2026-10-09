#ifndef SVS_COMPUTE_TRANSPORT_H
#define SVS_COMPUTE_TRANSPORT_H
#ifdef _WIN32
#include <windows.h>
#else
#include <cerrno>
#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <signal.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#endif
#include <atomic>
#include <chrono>
#include <mutex>
#include <random>
#include <thread>

#include "ComputeCommon.h"

namespace svsc {
inline std::string token()
{
	static std::atomic<uint64_t> sequence{1};
	std::random_device random;
	return "svsc-" + std::to_string(uint64_t(random()) << 32 | random()) + "-" + std::to_string(sequence.fetch_add(1));
}

class SharedBuffer
{
public:
	std::string name;
	uint8_t* data = nullptr;
	bool owner = false;
#ifdef _WIN32
	HANDLE handle = nullptr;
#else
	int handle = -1;
#endif

	explicit SharedBuffer(std::string existing = {})
		: name(existing.empty() ? token() : existing)
	{
		owner = existing.empty();
#ifdef _WIN32
		const auto wide = fs::u8path("Local\\" + name).wstring();
		handle = owner ? CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0,
											DWORD(BufferLimit + BufferHeader), wide.c_str())
					   : OpenFileMappingW(FILE_MAP_ALL_ACCESS, FALSE, wide.c_str());
		if (handle)
		{
			data = static_cast<uint8_t*>(MapViewOfFile(handle, FILE_MAP_ALL_ACCESS, 0, 0, BufferLimit + BufferHeader));
		}
#else
		const auto posixName = "/" + name;
		handle = shm_open(posixName.c_str(), owner ? O_RDWR | O_CREAT | O_EXCL : O_RDWR, 0600);
		if (handle >= 0 && (!owner || ftruncate(handle, BufferLimit + BufferHeader) == 0))
		{
			const auto mapped
				= mmap(nullptr, BufferLimit + BufferHeader, PROT_READ | PROT_WRITE, MAP_SHARED, handle, 0);
			if (mapped != MAP_FAILED)
			{
				data = static_cast<uint8_t*>(mapped);
			}
		}
#endif
		if (!data)
		{
			close();
			throw Error(SVSC_UNAVAILABLE, "Cannot allocate/open bounded shared tensor buffer");
		}
		if (owner)
		{
			std::memset(data, 0, BufferHeader);
		}
	}
	SharedBuffer(const SharedBuffer&) = delete;
	SharedBuffer& operator=(const SharedBuffer&) = delete;
	~SharedBuffer() { close(); }
	void cancel()
	{
#ifdef _WIN32
		InterlockedExchange(reinterpret_cast<volatile LONG*>(data), 1);
#else
		__atomic_store_n(reinterpret_cast<uint32_t*>(data), 1u, __ATOMIC_RELEASE);
#endif
	}
	bool cancelled() const
	{
#ifdef _WIN32
		return InterlockedCompareExchange(reinterpret_cast<volatile LONG*>(data), 0, 0) != 0;
#else
		return __atomic_load_n(reinterpret_cast<uint32_t*>(data), __ATOMIC_ACQUIRE) != 0;
#endif
	}
	uint8_t* payload() const { return data + BufferHeader; }

private:
	void close()
	{
#ifdef _WIN32
		if (data)
		{
			UnmapViewOfFile(data);
		}
		if (handle)
		{
			CloseHandle(handle);
		}
		handle = nullptr;
#else
		if (data)
		{
			munmap(data, BufferLimit + BufferHeader);
		}
		if (handle >= 0)
		{
			::close(handle);
		}
		if (owner)
		{
			shm_unlink(("/" + name).c_str());
		}
		handle = -1;
#endif
		data = nullptr;
	}
};

class Worker
{
public:
	std::mutex mutex;
	std::string backend, device, epoch = token();
	std::atomic<bool> lost{false};
	uint64_t sequence = 0;
#ifdef _WIN32
	HANDLE process = nullptr, input = nullptr, output = nullptr;
#else
	pid_t process = -1;
	int input = -1, output = -1;
#endif
	Worker(const fs::path& directory, std::string type, std::string id)
		: backend(std::move(type))
		, device(std::move(id))
	{
		launch(directory);
		try
		{
			const auto reply = rpc({{"op", "hello"}}, 120);
			if (reply.value("runtime", std::string{}) != RuntimeVersion)
			{
				throw Error(SVSC_VERSION_MISMATCH, "Worker runtime version mismatch");
			}
		}
		catch (...)
		{
			stop();
			throw;
		}
	}
	Worker(const Worker&) = delete;
	Worker& operator=(const Worker&) = delete;
	~Worker() { stop(); }
	void terminate()
	{
#ifdef _WIN32
		if (process)
		{
			TerminateProcess(process, 1);
		}
#else
		if (process > 0)
		{
			kill(process, SIGKILL);
		}
#endif
	}

	Json rpc(Json request, unsigned seconds = 300)
	{
		std::lock_guard<std::mutex> lock(mutex);
		if (lost.load())
		{
			throw Error(SVSC_WORKER_LOST, "Worker epoch is invalid");
		}
		request["version"] = ProtocolVersion;
		request["epoch"] = epoch;
		request["request"] = ++sequence;
		const auto bytes = request.dump();
		if (bytes.size() > ControlLimit)
		{
			throw Error(SVSC_LIMIT_EXCEEDED, "IPC control exceeds 1 MiB");
		}
		try
		{
			const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(seconds);
			const uint32_t size = uint32_t(bytes.size());
			write(&size, sizeof(size));
			write(bytes.data(), bytes.size());
			uint32_t replySize = 0;
			read(&replySize, sizeof(replySize), deadline);
			if (!replySize || replySize > ControlLimit)
			{
				throw Error(SVSC_WORKER_LOST, "Invalid IPC response size");
			}
			std::string replyBytes(replySize, '\0');
			read(replyBytes.data(), replyBytes.size(), deadline);
			auto reply = Json::parse(replyBytes);
			if (reply.value("version", 0u) != ProtocolVersion || reply.value("epoch", std::string{}) != epoch
				|| reply.value("request", uint64_t(0)) != sequence)
			{
				throw Error(SVSC_WORKER_LOST, "Invalid IPC version/request/epoch");
			}
			const auto status = reply.value("status", SVSC_INTERNAL_ERROR);
			if (status != SVSC_OK)
			{
				throw Error(status, reply.value("error", "Worker failure"));
			}
			return reply;
		}
		catch (const Error& error)
		{
			if (error.status == SVSC_WORKER_LOST)
			{
				lost.store(true);
				stop();
			}
			throw;
		}
		catch (const std::exception& error)
		{
			lost.store(true);
			stop();
			throw Error(SVSC_WORKER_LOST, std::string("Malformed/disconnected worker: ") + error.what());
		}
	}

private:
	void launch(const fs::path& directory);
	void stop();
	void write(const void* data, size_t size);
	void read(void* data, size_t size, std::chrono::steady_clock::time_point deadline);
};
} // namespace svsc
#endif
