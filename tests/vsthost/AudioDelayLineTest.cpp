#include "AudioDelayLine.h"
#include <array>
#include <cstdlib>
#include <iostream>
#include <new>

static thread_local bool watch = false;
static thread_local unsigned allocations = 0;
void* operator new(std::size_t bytes)
{
	if (watch)
	{
		++allocations;
	}
	if (auto* p = std::malloc(bytes ? bytes : 1))
	{
		return p;
	}
	throw std::bad_alloc();
}
void operator delete(void* pointer) noexcept
{
	std::free(pointer);
}
void operator delete(void* pointer, std::size_t) noexcept
{
	std::free(pointer);
}
#define CHECK(condition) \
	do \
	{ \
		if (!(condition)) \
		{ \
			std::cerr << "line " << __LINE__ << ": " << #condition << '\n'; \
			return 1; \
		} \
	} while (0)

int main()
{
	using Frame = std::array<float, 3>;
	lmms::AudioDelayLine<Frame> delay;
	delay.prepare(2049);
	constexpr unsigned total = 8192;
	std::array<Frame, total> input{}, output{};
	input[0] = {1, -2, 3};
	input[17] = {-4, 5, -6};
	input[4096] = {7, -8, 9};
	constexpr std::array<unsigned, 7> sizes{1, 17, 65, 3, 127, 256, 19};
	for (const auto frames : {0u, 1u, 17u, 256u, 2049u})
	{
		watch = true;
		const auto configured = delay.setDelay(frames);
		delay.reset();
		watch = false;
		CHECK(configured && delay.delay() == frames && allocations == 0);
		unsigned cursor = 0, callback = 0;
		while (cursor < total)
		{
			const auto count = std::min(sizes[callback++ % sizes.size()], total - cursor);
			watch = true;
			const auto processed
				= delay.process(std::span(input).subspan(cursor, count), std::span(output).subspan(cursor, count));
			watch = false;
			CHECK(processed && allocations == 0);
			cursor += count;
		}
		for (unsigned i = 0; i < total; ++i)
		{
			CHECK(output[i] == (i < frames ? Frame{} : input[i - frames]));
		}
		// Exact in-place processing must preserve input before replacing it.
		delay.reset();
		output = input;
		watch = true;
		const auto processed = delay.process(output, output);
		watch = false;
		CHECK(processed && allocations == 0);
		for (unsigned i = 0; i < total; ++i)
		{
			CHECK(output[i] == (i < frames ? Frame{} : input[i - frames]));
		}
	}
	// Repeated publication of an unchanged delay retains queued audio.
	CHECK(delay.setDelay(17));
	Frame impulse{1, 2, 3}, first{}, tail{};
	CHECK(delay.process(std::span(&impulse, 1), std::span(&first, 1)));
	CHECK(delay.setDelay(17));
	CHECK(!delay.setDelay(2050) && delay.delay() == 17);
	std::array<Frame, 16> silence{}, discarded{};
	CHECK(delay.process(silence, discarded));
	Frame zero{};
	CHECK(delay.process(std::span(&zero, 1), std::span(&tail, 1)) && tail == impulse);
	// A delay change discards old history; the last delayed impulse still drains
	// correctly when subsequent input is silent.
	CHECK(delay.setDelay(1));
	CHECK(delay.process(std::span(&zero, 1), std::span(&tail, 1)) && tail == Frame{});
	CHECK(delay.process(std::span(&impulse, 1), std::span(&tail, 1)) && tail == Frame{});
	CHECK(!delay.process({}, std::span(&tail, 1)) && tail == Frame{});
	CHECK(delay.process(std::span(&zero, 1), std::span(&tail, 1)) && tail == impulse);
	delay.prepare(0);
	CHECK(delay.capacity() == 0 && delay.delay() == 0 && !delay.setDelay(1));
	CHECK(delay.process(std::span(&impulse, 1), std::span(&tail, 1)) && tail == impulse);
	CHECK(delay.process({}, {}));
	std::cout
		<< "PASS sample delay: multichannel impulses, variable blocks, long delay, in-place, tail drain, boundary reset, capacity rejection and no audio allocation\n";
}
