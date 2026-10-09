#include <filesystem>
#include <iostream>

#include "svs_compute.hpp"

int main(int argc, char** argv)
{
	if (argc != 7)
	{
		std::cerr << "Usage: SVSComputeConsumer library runtime-directory model-root relative-model backend device\n";
		return 2;
	}
	try
	{
		svs_compute::Library library(std::filesystem::u8path(argv[1]));
		library.setMemoryPolicy("idle", 60);
		auto context = library.context(std::filesystem::u8path(argv[2]));
		auto lease = library.retainModels();
		const svsc_model_desc descriptor{sizeof(descriptor), 1, argv[3], argv[4], "public-consumer-add-v1"};
		auto model = context.model(descriptor);
		const svsc_session_desc options{sizeof(options), 0, argv[5], argv[6], "public-consumer", ""};
		auto session = model.session(options);
		float x[]{1, 2, 3, 4}, y[]{4, 3, 2, 1};
		int64_t shape[]{1, 4};
		const std::vector<svsc_tensor> inputs{{sizeof(svsc_tensor), SVSC_FLOAT32, "x", 2, 0, shape, sizeof(x), x},
											  {sizeof(svsc_tensor), SVSC_FLOAT32, "y", 2, 0, shape, sizeof(y), y}};
		auto result = session.createRun().run(inputs);
		const auto& output = result.value();
		if (output.tensor_count != 1 || output.tensors[0].dtype != SVSC_FLOAT32
			|| output.tensors[0].byte_count != sizeof(x))
		{
			throw std::runtime_error("Unexpected fixture output signature");
		}
		for (unsigned i = 0; i < 4; ++i)
		{
			if (static_cast<const float*>(output.tensors[0].data)[i] != 5)
			{
				throw std::runtime_error("Fixture output mismatch");
			}
		}
		std::cout << output.execution_json << "\nPASS public C++17 consumer, no LMMS/Qt/ORT linkage\n";
		return 0;
	}
	catch (const std::exception& error)
	{
		std::cerr << error.what() << std::endl;
		return 1;
	}
}
