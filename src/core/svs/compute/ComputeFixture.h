#ifndef SVS_COMPUTE_FIXTURE_H
#define SVS_COMPUTE_FIXTURE_H
#include <cstdint>
#include <string>
namespace svsc {
inline void encodeInteger(std::string& bytes, uint64_t value)
{
	while (value >= 128)
	{
		bytes.push_back(char((value & 127) | 128));
		value >>= 7;
	}
	bytes.push_back(char(value));
}
inline std::string integerField(unsigned field, uint64_t value)
{
	std::string bytes;
	encodeInteger(bytes, field * 8);
	encodeInteger(bytes, value);
	return bytes;
}
inline std::string messageField(unsigned field, const std::string& value)
{
	std::string bytes;
	encodeInteger(bytes, field * 8 + 2);
	encodeInteger(bytes, value.size());
	return bytes + value;
}
inline std::string fixtureModel()
{
	const auto shape = messageField(1, integerField(1, 1)) + messageField(1, integerField(1, 4));
	const auto type = messageField(1, integerField(1, 1) + messageField(2, shape));
	const auto port = [&](const char* name) { return messageField(1, name) + messageField(2, type); };
	const auto node = messageField(1, "x") + messageField(1, "y") + messageField(2, "z") + messageField(4, "Add");
	const auto graph = messageField(1, node) + messageField(2, "SVSCompute fixture") + messageField(11, port("x"))
		+ messageField(11, port("y")) + messageField(12, port("z"));
	return integerField(1, 8) + messageField(7, graph) + messageField(8, integerField(2, 17));
}
inline std::string identityModel(uint32_t dtype, bool scalar)
{
	const auto shape = scalar ? std::string{} : messageField(1, integerField(1, 4));
	const auto type = messageField(1, integerField(1, dtype) + messageField(2, shape));
	const auto port = [&](const char* name) { return messageField(1, name) + messageField(2, type); };
	const auto node = messageField(1, "x") + messageField(2, "z") + messageField(4, "Identity");
	const auto graph = messageField(1, node) + messageField(2, "identity-types") + messageField(11, port("x"))
		+ messageField(12, port("z"));
	return integerField(1, 8) + messageField(7, graph) + messageField(8, integerField(2, 17));
}
inline std::string cancellationModel()
{
	const auto port = [](const char* name, uint32_t dtype, bool scalar) {
		const auto shape = scalar ? std::string{} : messageField(1, integerField(1, 4));
		return messageField(1, name)
			+ messageField(2, messageField(1, integerField(1, dtype) + messageField(2, shape)));
	};
	const auto identity = [](const char* input, const char* output) {
		return messageField(1, input) + messageField(2, output) + messageField(4, "Identity");
	};
	const auto body = messageField(1, identity("condition", "next_condition"))
		+ messageField(1, identity("state", "next_state")) + messageField(2, "cancel-body")
		+ messageField(11, port("iteration", 7, true)) + messageField(11, port("condition", 9, true))
		+ messageField(11, port("state", 1, false)) + messageField(12, port("next_condition", 9, true))
		+ messageField(12, port("next_state", 1, false));
	const auto attribute = messageField(1, "body") + messageField(6, body) + integerField(20, 5);
	const auto loop = messageField(1, "trip_count") + messageField(1, "initial_condition") + messageField(1, "x")
		+ messageField(2, "z") + messageField(4, "Loop") + messageField(5, attribute);
	std::string count;
	const uint64_t iterations = 1000000000;
	for (unsigned i = 0; i < 8; ++i)
	{
		count.push_back(char(iterations >> (i * 8)));
	}
	const auto countTensor = integerField(2, 7) + messageField(8, "trip_count") + messageField(9, count);
	const auto condition
		= integerField(2, 9) + messageField(8, "initial_condition") + messageField(9, std::string(1, '\1'));
	const auto graph = messageField(1, loop) + messageField(2, "bounded-cancellation-test")
		+ messageField(5, countTensor) + messageField(5, condition) + messageField(11, port("x", 1, false))
		+ messageField(12, port("z", 1, false));
	return integerField(1, 8) + messageField(7, graph) + messageField(8, integerField(2, 17));
}
} // namespace svsc
#endif
