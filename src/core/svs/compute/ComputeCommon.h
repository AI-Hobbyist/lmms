#ifndef SVS_COMPUTE_COMMON_H
#define SVS_COMPUTE_COMMON_H
#include <cmath>
#include <cstring>
#include <filesystem>
#include <limits>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <string>
#include <vector>

#include "svs_compute.h"

namespace svsc {
using Json = nlohmann::ordered_json;
namespace fs = std::filesystem;
constexpr const char* RuntimeVersion = "svs-compute-1/ort-1.23.0/dml-1.15.4";
constexpr uint32_t ProtocolVersion = 1;
constexpr size_t ControlLimit = 1024 * 1024;
constexpr size_t BufferLimit = size_t(SVSC_MAX_BUFFER_BYTES);
constexpr size_t BufferHeader = 64;

struct Error : std::runtime_error
{
	svsc_status status;
	Error(svsc_status value, const std::string& text)
		: std::runtime_error(text)
		, status(value)
	{
	}
};

inline size_t width(uint32_t type)
{
	switch (type)
	{
	case SVSC_FLOAT32:
		return 4;
	case SVSC_INT64:
		return 8;
	case SVSC_BOOL:
		return 1;
	default:
		throw Error(SVSC_INVALID_ARGUMENT, "Unsupported tensor dtype");
	}
}

inline size_t tensorBytes(uint32_t type, const std::vector<int64_t>& dims)
{
	if (dims.size() > SVSC_MAX_RANK)
	{
		throw Error(SVSC_INVALID_ARGUMENT, "Tensor rank exceeds 8");
	}
	size_t bytes = width(type);
	for (auto dim : dims)
	{
		if (dim < 0 || uint64_t(dim) > BufferLimit || (dim && bytes > BufferLimit / uint64_t(dim)))
		{
			throw Error(SVSC_INVALID_ARGUMENT, "Tensor dimensions overflow or exceed budget");
		}
		bytes *= size_t(dim);
	}
	return bytes;
}

inline void validateValues(uint32_t type, const void* data, size_t bytes)
{
	if (bytes && !data)
	{
		throw Error(SVSC_INVALID_ARGUMENT, "Null tensor data");
	}
	for (size_t offset = 0; offset < bytes; offset += width(type))
	{
		if (type == SVSC_FLOAT32)
		{
			float value;
			std::memcpy(&value, static_cast<const uint8_t*>(data) + offset, sizeof(value));
			if (!std::isfinite(value))
			{
				throw Error(SVSC_INVALID_ARGUMENT, "Nonfinite float tensor");
			}
		}
		else if (type == SVSC_BOOL && static_cast<const uint8_t*>(data)[offset] > 1)
		{
			throw Error(SVSC_INVALID_ARGUMENT, "Invalid bool tensor");
		}
	}
}

inline fs::path authorizedPath(const fs::path& root, const fs::path& relative)
{
	if (relative.empty() || relative.is_absolute() || relative.has_root_name())
	{
		throw Error(SVSC_INVALID_ARGUMENT, "Model path must be relative to its authorized root");
	}
	const auto canonicalRoot = fs::canonical(root);
	const auto target = fs::canonical(canonicalRoot / relative);
	auto left = canonicalRoot.begin();
	auto right = target.begin();
	for (; left != canonicalRoot.end(); ++left, ++right)
	{
		if (right == target.end() || *left != *right)
		{
			throw Error(SVSC_INVALID_ARGUMENT, "Model or external tensor escapes authorized root");
		}
	}
	if (!fs::is_regular_file(target))
	{
		throw Error(SVSC_INVALID_ARGUMENT, "Model is not a regular file");
	}
	return target;
}
} // namespace svsc
#endif
