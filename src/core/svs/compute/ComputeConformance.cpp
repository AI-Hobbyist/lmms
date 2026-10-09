#include <fstream>
#include <iostream>
#include <limits>
#include <nlohmann/json.hpp>
#include <thread>

#include "ComputeFixture.h"
#include "svs_compute.hpp"

namespace fs = std::filesystem;
using Json = nlohmann::ordered_json;
void require(bool condition, const char* message)
{
	if (!condition)
	{
		throw std::runtime_error(message);
	}
}
template <class F> void rejects(F&& function, svsc_status expected)
{
	try
	{
		function();
	}
	catch (const svs_compute::Error& error)
	{
		require(error.status == expected, error.what());
		return;
	}
	throw std::runtime_error("Expected compute failure was not returned");
}
int main(int argc, char** argv)
{
	if (argc != 4)
	{
		std::cerr << "Usage: ComputeConformance library worker-directory fixture-directory\n";
		return 2;
	}
	try
	{
		svs_compute::Library library(fs::u8path(argv[1]));
		rejects([&] { svs_compute::Library missing(fs::u8path(argv[1]).parent_path() / "missing-SVSCompute.dll"); },
				SVSC_UNAVAILABLE);
		auto context = library.context(fs::u8path(argv[2]));
		rejects([&] { library.context(fs::u8path(argv[2]) / "missing-runtime"); }, SVSC_UNAVAILABLE);
		const auto root = fs::canonical(fs::u8path(argv[3]));
		const auto fixture = root / "B1-add-fixture.onnx";
		const auto modelBytes = svsc::fixtureModel();
		{
			std::ofstream file(fixture, std::ios::binary);
			file.write(modelBytes.data(), modelBytes.size());
		}
		struct Cleanup
		{
			fs::path path;
			~Cleanup()
			{
				std::error_code error;
				fs::remove(path, error);
			}
		} cleanup{fixture};
		const auto rootText = root.u8string();
		svsc_model_desc desc{sizeof(desc), 1, rootText.c_str(), "B1-add-fixture.onnx", "B1-add-v1"};
		auto model = context.model(desc);
		const auto signature = Json::parse(model.signature());
		require(signature["inputs"].size() == 2 && signature["outputs"].size() == 1, "Invalid model signature");
		float x[]{1, 2, 3, 4}, y[]{4, 3, 2, 1};
		int64_t shape[]{1, 4};
		std::vector<svsc_tensor> inputs{{sizeof(svsc_tensor), SVSC_FLOAT32, "x", 2, 0, shape, sizeof(x), x},
										{sizeof(svsc_tensor), SVSC_FLOAT32, "y", 2, 0, shape, sizeof(y), y}};
		svsc_session_desc options{sizeof(options), 0, "cpu", "cpu", "conformance", ""};
		Json report{{"cpu", Json::object()}};
		{
			auto session = model.session(options);
			auto completedRun = session.createRun();
			{
				auto result = completedRun.run(inputs);
				require(Json::parse(context.memoryStatus())["tensorAllocatedBytes"] == sizeof(x),
						"Completed run retained its shared transport buffer");
				require(static_cast<const float*>(result.value().tensors[0].data)[0] == 5,
						"Released transport invalidated owned output");
			}
			require(Json::parse(context.memoryStatus())["tensorAllocatedBytes"] == 0,
					"Released result retained inference tensors");
			for (int i = 0; i < 32; ++i)
			{
				session.createRun().run(inputs);
				require(Json::parse(context.memoryStatus())["tensorAllocatedBytes"] == 0,
						"Sequential inference accumulated tensor buffers");
			}
			std::cout
				<< "PASS: completed transport freed immediately; owned output freed on release; 32 runs return to zero."
				<< std::endl;
		}
		for (const auto type : {SVSC_INT64, SVSC_BOOL, SVSC_FLOAT32})
		{
			const bool scalar = type == SVSC_FLOAT32;
			const auto bytes = svsc::identityModel(type, scalar);
			const auto name = "B1-identity-" + std::to_string(type) + ".onnx";
			const auto path = root / name;
			{
				std::ofstream file(path, std::ios::binary);
				file.write(bytes.data(), bytes.size());
			}
			Cleanup identityCleanup{path};
			const svsc_model_desc identityDesc{sizeof(identityDesc), 1, rootText.c_str(), name.c_str(), "identity-v1"};
			auto identity = context.model(identityDesc);
			int64_t integerValues[]{1, -2, 3, 4};
			uint8_t boolValues[]{1, 0, 1, 0};
			float scalarValue = 0.125f;
			int64_t dimension = 4;
			const void* data = type == SVSC_INT64 ? static_cast<const void*>(integerValues)
				: type == SVSC_BOOL				  ? static_cast<const void*>(boolValues)
												  : static_cast<const void*>(&scalarValue);
			const auto count = type == SVSC_INT64 ? sizeof(integerValues)
				: type == SVSC_BOOL				  ? sizeof(boolValues)
												  : sizeof(scalarValue);
			const std::vector<svsc_tensor> tensor{{sizeof(svsc_tensor), uint32_t(type), "x", scalar ? 0u : 1u, 0,
												   scalar ? nullptr : &dimension, count, data}};
			auto output = identity.session(options).createRun().run(tensor);
			require(output.value().tensor_count == 1 && output.value().tensors[0].byte_count == count
						&& std::memcmp(data, output.value().tensors[0].data, count) == 0,
					"Typed/scalar tensor ownership failed");
		}
		{
			auto session = model.session(options);
			auto run = session.createRun();
			auto result = run.run(inputs);
			const auto& view = result.value();
			require(view.tensor_count == 1 && view.tensors[0].byte_count == 16, "Invalid result ownership/view");
			for (unsigned i = 0; i < 4; ++i)
			{
				require(static_cast<const float*>(view.tensors[0].data)[i] == 5, "CPU numerical mismatch");
			}
			report["cpu"] = Json::parse(view.execution_json);
			rejects([&] { run.run(inputs); }, SVSC_INVALID_ARGUMENT);
			auto invalid = inputs;
			invalid[0].byte_count = 4;
			rejects([&] { session.createRun().run(invalid); }, SVSC_INVALID_ARGUMENT);
			invalid = inputs;
			invalid[1].name = "x";
			rejects([&] { session.createRun().run(invalid); }, SVSC_INVALID_ARGUMENT);
			x[0] = std::numeric_limits<float>::infinity();
			rejects([&] { session.createRun().run(inputs); }, SVSC_INVALID_ARGUMENT);
			x[0] = 1;
			auto cancelled = session.createRun();
			cancelled.cancel();
			rejects([&] { cancelled.run(inputs); }, SVSC_CANCELLED);
		}
		options.backend = "directml";
		{
			const auto path = root / "B1-cancel-fixture.onnx";
			const auto bytes = svsc::cancellationModel();
			{
				std::ofstream file(path, std::ios::binary);
				file.write(bytes.data(), bytes.size());
			}
			Cleanup cancellationCleanup{path};
			const svsc_model_desc cancellationDesc{sizeof(cancellationDesc), 1, rootText.c_str(),
												   "B1-cancel-fixture.onnx", "loop-cancel-v1"};
			svsc_session_desc cpuOptions{sizeof(cpuOptions), 0, "cpu", "cpu", "cancel-loop", ""};
			auto loop = context.model(cancellationDesc).session(cpuOptions).createRun();
			int64_t dim = 4;
			const std::vector<svsc_tensor> data{{sizeof(svsc_tensor), SVSC_FLOAT32, "x", 1, 0, &dim, sizeof(x), x}};
			std::thread cancellation([&] {
				std::this_thread::sleep_for(std::chrono::milliseconds(50));
				loop.cancel();
			});
			try
			{
				rejects([&] { loop.run(data); }, SVSC_CANCELLED);
			}
			catch (...)
			{
				cancellation.join();
				throw;
			}
			cancellation.join();
		}
		options.device = "dxgi:ffffffff:ffffffff";
		{
			const svsc_session_desc cpuOptions{sizeof(cpuOptions), 0, "cpu", "cpu", "memory-cpu", ""};
			auto cpuSession = model.session(cpuOptions);
			library.setMemoryPolicy("resident", 1);
			cpuSession.createRun().run(inputs);
			std::this_thread::sleep_for(std::chrono::milliseconds(1300));
			require(Json::parse(context.memoryStatus())["cpu"]["residentSessions"].get<unsigned>() > 0,
					"CPU resident policy unexpectedly released models");
			auto lease = library.retainModels();
			library.setMemoryPolicy("idle", 1);
			std::this_thread::sleep_for(std::chrono::milliseconds(1300));
			require(Json::parse(context.memoryStatus())["cpu"]["residentSessions"].get<unsigned>() > 0,
					"CPU idle policy released an active render");
			lease.reset();
			std::this_thread::sleep_for(std::chrono::milliseconds(1500));
			require(Json::parse(context.memoryStatus())["cpu"]["residentSessions"] == 0, "CPU idle release failed");
			auto reloaded = cpuSession.createRun().run(inputs);
			require(static_cast<const float*>(reloaded.value().tensors[0].data)[0] == 5, "CPU model reload failed");
			library.setMemoryPolicy("immediate", 60);
			cpuSession.createRun().run(inputs);
			const auto released = Json::parse(context.memoryStatus());
			require(released["cpu"]["residentSessions"] == 0 && released["releaseError"] == "",
					"CPU immediate release failed");
			std::cout << "MEMORY CPU release " << released.dump() << std::endl;
			library.setMemoryPolicy("idle", 60);
		}
		options.cpu_only_reason = "force_on_cpu";
		{
			auto result = model.session(options).createRun().run(inputs);
			const auto route = Json::parse(result.value().execution_json);
			require(route["effectiveBackend"] == "cpu" && route["fallbackReason"] == "force_on_cpu",
					"CPU-only constraint ignored");
		}
		options.cpu_only_reason = "";
		options.allow_cpu_fallback = 1;
		{
			auto result = model.session(options).createRun().run(inputs);
			require(Json::parse(result.value().execution_json)["effectiveBackend"] == "cpu",
					"Missing-device fallback failed");
		}
		const auto devices = Json::parse(context.devices());
		report["devices"] = devices;
		for (const auto& device : devices["devices"])
		{
			if (device["backend"] != "directml" || !device.value("available", false))
			{
				continue;
			}
			const auto id = device["device"].get<std::string>();
			options.device = id.c_str();
			options.allow_cpu_fallback = 0;
			library.setMemoryPolicy("resident", 1);
			auto result = model.session(options).createRun().run(inputs);
			const auto route = Json::parse(result.value().execution_json);
			require(route["effectiveDevice"] == id && route["providerEvidence"]["dmlNodes"].get<unsigned>() > 0,
					"DML selection/profile mismatch");
			for (unsigned i = 0; i < 4; ++i)
			{
				require(static_cast<const float*>(result.value().tensors[0].data)[i] == 5, "DML numerical mismatch");
			}
			auto session = model.session(options);
			std::this_thread::sleep_for(std::chrono::milliseconds(1300));
			require(Json::parse(context.memoryStatus())["gpu"]["residentSessions"].get<unsigned>() > 0,
					"Resident policy unexpectedly released models");
			auto lease = library.retainModels();
			library.setMemoryPolicy("idle", 1);
			std::this_thread::sleep_for(std::chrono::milliseconds(1300));
			require(Json::parse(context.memoryStatus())["gpu"]["residentSessions"].get<unsigned>() > 0,
					"Idle policy released models during rendering");
			lease.reset();
			std::this_thread::sleep_for(std::chrono::milliseconds(1500));
			const auto released = Json::parse(context.memoryStatus());
			require(released["gpu"]["residentSessions"] == 0 && released["releaseError"] == "",
					"Idle policy failed to release resident models");
			std::cout << "MEMORY idle release " << released.dump() << std::endl;
			auto reloaded = session.createRun().run(inputs);
			require(Json::parse(reloaded.value().execution_json)["providerEvidence"]["dmlNodes"].get<unsigned>() > 0,
					"Released session did not reload on the selected GPU");
			lease = library.retainModels();
			library.setMemoryPolicy("immediate", 60);
			auto firstStage = session.createRun().run(inputs);
			auto secondStage = session.createRun().run(inputs);
			require(Json::parse(context.memoryStatus())["gpu"]["residentSessions"].get<unsigned>() > 0,
					"Immediate policy released models between render stages");
			lease.reset();
			const auto immediate = Json::parse(context.memoryStatus());
			require(immediate["gpu"]["residentSessions"] == 0 && immediate["releaseError"] == "",
					"Immediate policy failed after render completion");
			require(static_cast<const float*>(secondStage.value().tensors[0].data)[0] == 5,
					"GPU release invalidated retained result ownership");
			std::cout << "MEMORY immediate release " << immediate.dump() << std::endl;
			library.setMemoryPolicy("idle", 60);
		}
#ifdef _WIN32
		options.backend = "cpu";
		options.device = "cpu";
		auto oldSession = model.session(options);
		const auto before = Json::parse(context.probe("cpu", "cpu"));
		const auto process
			= OpenProcess(PROCESS_TERMINATE | SYNCHRONIZE, FALSE, before["workerProcessId"].get<DWORD>());
		require(process != nullptr, "Cannot open owned worker for crash isolation test");
		require(TerminateProcess(process, 93) != 0, "Cannot terminate owned test worker");
		WaitForSingleObject(process, 1000);
		CloseHandle(process);
		rejects([&] { oldSession.createRun().run(inputs); }, SVSC_WORKER_LOST);
		const auto after = Json::parse(context.probe("cpu", "cpu"));
		require(before["epoch"] != after["epoch"], "Restarted worker reused dead epoch");
		rejects([&] { oldSession.createRun().run(inputs); }, SVSC_WORKER_LOST);
		require(model.session(options).createRun().run(inputs).value().tensor_count == 1, "CPU worker restart failed");
		report["crashIsolation"] = "PASS";
#endif
		desc.relative_path = "../escape.onnx";
		rejects([&] { context.model(desc); }, SVSC_INVALID_ARGUMENT);
		std::cout << report.dump(2)
				  << "\nPASS: ownership, tensor validation, cancellation, CPU constraints, LUID and crash epoch\n";
		return 0;
	}
	catch (const std::exception& error)
	{
		std::cerr << "FAIL: " << error.what() << std::endl;
		return 1;
	}
}
