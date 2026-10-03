#include "vsthost/Protocol.h"
#include <iostream>
#include <limits>

using namespace lmms::vsthost;
int main()
{
	int failed = 0;
	const auto check = [&](bool result, const char* name) {
		if (!result) { std::cerr << "FAIL: " << name << '\n'; ++failed; }
	};
	Header source{MessageType::Process, 0x0102030405060708, 9, 10, 1024}, decoded;
	auto bytes = encode(source);
	check(bytes[8] == 8 && bytes[15] == 1, "fixed little endian session");
	check(decode(bytes, decoded) == Error::None && matches(decoded, source.session, 9, 10), "round trip");
	check(!matches(decoded, source.session, 8, 10) && !matches(decoded, source.session, 9, 11), "stale generation/sequence");
	check(!matches(decoded, source.session + 1, 9, 10), "other session");
	check(decode(std::span(bytes).first(39), decoded) == Error::InvalidMessage, "truncated header");
	bytes[4] = 2;
	check(decode(bytes, decoded) == Error::ProtocolMismatch, "protocol mismatch");
	bytes = encode(source); bytes[36] = 1;
	check(decode(bytes, decoded) == Error::InvalidMessage, "reserved bytes");
	bytes = encode(source); bytes[6] = 255;
	check(decode(bytes, decoded) == Error::InvalidMessage, "unknown opcode");
	source.payloadBytes = MaxControlBytes + 1; bytes = encode(source);
	check(decode(bytes, decoded) == Error::InvalidMessage, "oversized blob");
	check(validRegion(16, 16, 32) && validRegion(32, 0, 32), "region boundaries");
	check(!validRegion(33, 0, 32) && !validRegion(16, 17, 32), "out of bounds region");
	check(!validRegion(1, std::numeric_limits<std::uint64_t>::max(), 32), "integer overflow");
	return failed == 0 ? 0 : 1;
}
