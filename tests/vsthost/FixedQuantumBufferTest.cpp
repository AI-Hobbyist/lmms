#include "vsthost/FixedQuantumBuffer.h"
#include <cstdlib>
#include <iostream>
#include <new>

using namespace lmms::vsthost;
static thread_local bool watch = false;
static thread_local unsigned allocations = 0;
void* operator new(std::size_t bytes) { if (watch) { ++allocations; } if (auto* p = std::malloc(bytes ? bytes : 1)) { return p; } throw std::bad_alloc(); }
void operator delete(void* pointer) noexcept { std::free(pointer); }
void operator delete(void* pointer, std::size_t) noexcept { std::free(pointer); }
#define CHECK(condition) do { if (!(condition)) { std::cerr << "line " << __LINE__ << ": " << #condition << '\n'; return 1; } } while (0)

int main()
{
	constexpr unsigned quantum = 64, channels = 3, total = 2048;
	FixedQuantumBuffer buffer;
	CHECK(buffer.configure({quantum, channels, channels}, 48000));
	CHECK(buffer.latency() == 128);
	CHECK(!buffer.configure({0, 3, 3}, 48000));
	std::array<float, quantum * channels> previous{};
	std::array<float, total * channels> source{}, actual{};
	for (unsigned i = 0; i < total; ++i)
	{
		for (unsigned c = 0; c < channels; ++c) { source[i * channels + c] = float(i * 4 + c); }
	}
	unsigned submitted = 0, eventsSeen = 0;
	bool valid = true;
	const auto submit = [&](auto layout, auto input, auto output, auto events, const auto& transport) {
		valid &= layout.frames == quantum && layout.inputs == channels && layout.outputs == channels;
		valid &= transport.continuous == 8000000000LL + submitted * quantum;
		valid &= transport.samples == 4000000000LL + submitted * quantum;
		for (const auto& event : events) { valid &= event.offset < quantum && event.id == submitted * quantum + event.offset; ++eventsSeen; }
		std::copy(previous.begin(), previous.end(), output.begin());
		std::copy(input.begin(), input.end(), previous.begin());
		++submitted; return true;
	};
	unsigned cursor = 0, callbacks = 0;
	constexpr std::array<unsigned, 7> sizes{1, 17, 65, 3, 127, 64, 19};
	while (cursor < total)
	{
		const auto frames = std::min(sizes[callbacks % sizes.size()], total - cursor);
		Vst3Transport transport; transport.flags = 1; transport.continuous = 8000000000LL + cursor;
		transport.samples = 4000000000LL + cursor; transport.music = cursor * 120.0 / (60 * 48000);
		const std::array<Vst3BlockEvent, 2> events{{{0, 0, 0, 0, cursor, 0.25}, {0, frames - 1, 0, 0, cursor + frames - 1, 0.75}}};
		watch = true;
		const bool ok = buffer.process(frames, std::span(source).subspan(cursor * channels, frames * channels),
			std::span(actual).subspan(cursor * channels, frames * channels), events, transport, submit);
		watch = false;
		CHECK(ok && allocations == 0 && valid); cursor += frames; ++callbacks;
	}
	CHECK(submitted == total / quantum && eventsSeen == 2 * callbacks);
	for (unsigned i = 0; i < total; ++i)
	{
		for (unsigned c = 0; c < channels; ++c)
		{ CHECK(actual[i * channels + c] == (i < 128 ? 0.0f : source[(i - 128) * channels + c])); }
	}
	FixedQuantumBuffer midiBuffer;
	CHECK(midiBuffer.configure({quantum, channels, channels}, 48000));
	previous.fill(0); submitted = 0; cursor = 0; callbacks = 0;
	std::array<Vst3OutputEvent, 512> generated{};
	std::array<Vst3Transport, total / quantum> positions{};
	std::array<std::array<unsigned, 512>, total / quantum> received{};
	unsigned delivered = 0;
	const auto submitMidi = [&](auto layout, auto input, auto output, auto, const auto& position, auto storage, auto& bytes) {
		valid &= layout.frames == quantum && submitted < positions.size();
		positions[submitted] = position;
		std::copy(previous.begin(), previous.end(), output.begin());
		std::copy(input.begin(), input.end(), previous.begin());
		if (submitted)
		{
			for (unsigned i = 0; i < generated.size(); ++i)
			{
				// Deliberately descending and repeated offsets, rather than sorted.
				generated[i] = {submitted - 1, quantum, {1, quantum - 1 - i % quantum, 0, i / 128, i % 128, 0.5}};
			}
			valid &= encodeVst3OutputEvents(generated, storage, bytes);
		}
		else { bytes = 0; }
		++submitted; return true;
	};
	const auto deliverMidi = [&](const Vst3OutputEvent& event, const Vst3Transport& position, unsigned offset) {
		const auto ordinal = event.event.channel * 128 + event.event.id;
		if (event.sequence >= received.size() || ordinal >= 512) { valid = false; return false; }
		const auto& original = positions[event.sequence];
		valid &= event.frames == quantum && event.event.offset == quantum - 1 - ordinal % quantum;
		valid &= position.continuous == original.continuous && position.samples == original.samples &&
			position.music == original.music && position.tempo == original.tempo && position.flags == original.flags;
		valid &= position.continuous == 8000000000LL + event.sequence * quantum;
		valid &= event.sequence * quantum + event.event.offset + midiBuffer.latency() == cursor + offset;
		valid &= actual[(cursor + offset) * channels] == source[(event.sequence * quantum + event.event.offset) * channels];
		++received[event.sequence][ordinal]; ++delivered; return true;
	};
	while (cursor < total)
	{
		const auto frames = std::min(sizes[callbacks % sizes.size()], total - cursor);
		Vst3Transport position; position.flags = 1; position.continuous = 8000000000LL + cursor;
		position.samples = 4000000000LL + cursor; position.tempo = 100 + callbacks % 7;
		position.music = cursor * 120.0 / (60 * 48000);
		watch = true;
		const bool ok = midiBuffer.processWithOutput(frames, std::span(source).subspan(cursor * channels, frames * channels),
			std::span(actual).subspan(cursor * channels, frames * channels), {}, position, submitMidi, deliverMidi);
		watch = false;
		CHECK(ok && allocations == 0 && valid); cursor += frames; ++callbacks;
	}
	CHECK(delivered == (total / quantum - 2) * 512);
	for (unsigned block = 0; block < received.size(); ++block)
	{
		for (const auto count : received[block]) { CHECK(count == (block < received.size() - 2 ? 1u : 0u)); }
	}
	// Reset discards pending audio and output events before a seek/reconfigure.
	midiBuffer.reset();
	unsigned afterReset = 0;
	std::array<float, quantum * channels> resetOutput{};
	Vst3Transport resetPosition; resetPosition.continuous = 9000000000LL;
	CHECK(midiBuffer.processWithOutput(quantum, std::span(source).first(quantum * channels), resetOutput, {}, resetPosition,
		[](auto, auto, auto output, auto, auto, auto, auto& bytes) {
			std::fill(output.begin(), output.end(), 0.0f); bytes = 0; return true;
		}, [&](const auto&, const auto&, auto) { ++afterReset; return true; }));
	CHECK(afterReset == 0 && std::all_of(resetOutput.begin(), resetOutput.end(), [](auto value) { return value == 0; }));
	// Reject an invalid output extent before any output callback is invoked.
	CHECK(!midiBuffer.processWithOutput(quantum, std::span(source).first(quantum * channels), resetOutput, {}, resetPosition,
		[](auto, auto, auto, auto, auto, auto storage, auto& bytes) {
			bytes = static_cast<unsigned>(storage.size() + 1); return true;
		}, [&](const auto&, const auto&, auto) { ++afterReset; return true; }));
	CHECK(afterReset == 0);
	buffer.reset(); previous.fill(0); submitted = 0;
	std::array<float, 192> output{}; Vst3Transport transport;
	CHECK(!buffer.process(64, std::span(source).first(192), output, {}, transport,
		[](auto, auto, auto, auto, auto) { return false; }));
	CHECK(std::all_of(output.begin(), output.end(), [](auto value) { return value == 0; }));
	CHECK(buffer.pendingEvents().empty());
	transport.continuous = INT64_MAX; output.fill(1);
	CHECK(!buffer.process(64, std::span(source).first(192), output, {}, transport,
		[](auto, auto, auto, auto, auto) { return true; }));
	CHECK(std::all_of(output.begin(), output.end(), [](auto value) { return value == 0; }));
	std::cout << "PASS fixed quantum: variable callbacks, exact latency, channels, events, 64-bit transport, no realtime allocation and failed submission silence\n";
}
