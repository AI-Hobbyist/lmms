#ifndef SVS_COMPUTE_DEVICE_H
#define SVS_COMPUTE_DEVICE_H
#include <onnxruntime_cxx_api.h>

#include "ComputeCommon.h"
#ifdef SVSC_HAS_DML
#include <windows.h>

#include <dml_provider_factory.h>
#include <dxgi1_4.h>
#include <iomanip>
#include <sstream>
#include <wrl/client.h>
#endif

namespace svsc {
struct Device
{
#ifdef SVSC_HAS_DML
	Microsoft::WRL::ComPtr<ID3D12Device> d3d;
	Microsoft::WRL::ComPtr<IDMLDevice> dml;
	Microsoft::WRL::ComPtr<ID3D12CommandQueue> queue;
#endif
	void configure(Ort::SessionOptions& options, const std::string& backend, const std::string& id)
	{
		options.SetExecutionMode(ORT_SEQUENTIAL);
		options.SetIntraOpNumThreads(4);
		options.SetInterOpNumThreads(1);
		options.SetGraphOptimizationLevel(ORT_ENABLE_ALL);
		if (backend == "cpu")
		{
			return;
		}
#ifdef SVSC_HAS_DML
		options.DisableMemPattern();
		if (!dml)
		{
			Microsoft::WRL::ComPtr<IDXGIFactory1> factory;
			auto require = [](HRESULT value, const char* operation) {
				if (FAILED(value))
				{
					throw Error(SVSC_BACKEND_FAILURE,
								std::string(operation) + ": HRESULT " + std::to_string(uint32_t(value)));
				}
			};
			require(CreateDXGIFactory1(IID_PPV_ARGS(&factory)), "DXGI initialization");
			Microsoft::WRL::ComPtr<IDXGIAdapter1> selected;
			for (unsigned i = 0;; ++i)
			{
				Microsoft::WRL::ComPtr<IDXGIAdapter1> adapter;
				const auto status = factory->EnumAdapters1(i, &adapter);
				if (status == DXGI_ERROR_NOT_FOUND)
				{
					break;
				}
				require(status, "DXGI enumeration");
				DXGI_ADAPTER_DESC1 desc{};
				require(adapter->GetDesc1(&desc), "DXGI description");
				if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)
				{
					continue;
				}
				std::ostringstream key;
				key << "dxgi:" << std::hex << std::setfill('0') << std::setw(8) << uint32_t(desc.AdapterLuid.HighPart)
					<< ':' << std::setw(8) << desc.AdapterLuid.LowPart;
				if (key.str() == id)
				{
					selected = adapter;
					break;
				}
			}
			if (!selected)
			{
				throw Error(SVSC_BACKEND_FAILURE, "Requested DML LUID is absent");
			}
			require(D3D12CreateDevice(selected.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&d3d)),
					"D3D12 initialization");
			require(DMLCreateDevice(d3d.Get(), DML_CREATE_DEVICE_FLAG_NONE, IID_PPV_ARGS(&dml)), "DML initialization");
			D3D12_COMMAND_QUEUE_DESC desc{};
			desc.Type = D3D12_COMMAND_LIST_TYPE_COMPUTE;
			require(d3d->CreateCommandQueue(&desc, IID_PPV_ARGS(&queue)), "DML command queue");
		}
		const OrtDmlApi* api = nullptr;
		Ort::ThrowOnError(
			Ort::GetApi().GetExecutionProviderApi("DML", ORT_API_VERSION, reinterpret_cast<const void**>(&api)));
		Ort::ThrowOnError(api->SessionOptionsAppendExecutionProvider_DML1(options, dml.Get(), queue.Get()));
#else
		throw Error(SVSC_UNAVAILABLE, "DirectML is unavailable in this CPU runtime/platform");
#endif
	}
};
} // namespace svsc
#endif
