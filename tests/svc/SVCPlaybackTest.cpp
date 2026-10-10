#include "SVCPlayback.h"

#include <QtEndian>
#include <cmath>
#include <cstdio>
#include <cstdlib>

using namespace lmms::svc;
namespace {
void check(bool value, const char* text)
{
	if (!value)
	{
		std::fprintf(stderr, "FAIL: %s\n", text);
		std::exit(1);
	}
}

svc_event event(const QByteArray& bytes, uint64_t generation, uint64_t segment, uint64_t index, uint64_t offset,
	uint32_t rate = 32000)
{
	svc_event result{};
	result.size = sizeof(result);
	result.type = SVC_AUDIO;
	result.request_id = "reference-request";
	result.generation_id = generation;
	result.segment_id = segment;
	result.chunk_index = index;
	result.sample_offset = offset;
	result.sample_count = bytes.size() / 2;
	result.sample_rate = rate;
	result.channels = 1;
	result.bytes = reinterpret_cast<const uint8_t*>(bytes.constData());
	result.byte_count = bytes.size();
	return result;
}

QByteArray pcm(uint64_t offset, uint64_t count, uint64_t impulse = 16000, int16_t value = 16000)
{
	QByteArray bytes(static_cast<int>(count * 2), 0);
	for (uint64_t index = 0; index < count; ++index)
	{
		qToLittleEndian<int16_t>(offset + index == impulse ? value : 0, bytes.data() + index * 2);
	}
	return bytes;
}
} // namespace

int main()
{
	auto source = std::make_shared<SourceAudio>();
	source->rate = 44100;
	source->stereo.resize(44100 * 2, .25f);
	PlaybackState state(source);
	const Segment segment{0, 44100, 0, 44100, 0, 44100};
	const auto generation = state.begin({segment});
	const auto oldSnapshot = state.snapshot();
	const auto first = pcm(0, 16001);
	check(state.publish(event(first, generation, 0, 1, 0)), "first chunk accepted");
	check(!state.finished() && state.progress() > .49 && state.progress() < .51, "first chunk publishes before done");
	const auto partial = state.snapshot();
	check(partial->trackSample(100, 0) == 0 && partial->trackSample(40000, 0) == .25f,
		"B immediately replaces received region, A fallback in pending region");
	check(oldSnapshot->trackSample(100, 0) == .25f, "previous audio block snapshot immutable");
	check(std::abs(partial->trackSample(22050, 0) - 16000 / 32768.0f) < 1e-6, "resampled impulse uses absolute time");
	check(!state.publish(event(first, generation - 1, 0, 2, 16001)), "stale generation rejected");
	check(!state.publish(event(first, generation, 0, 1, 16001)), "duplicate chunk rejected");
	const auto second = pcm(16001, 15999);
	check(state.publish(event(second, generation, 0, 2, 16001)), "second output chunk");
	check(state.complete(generation, 0, SVC_COMPLETE) && state.finished() && state.progress() == 1,
		"validated length and full source coverage");
	const auto complete = state.snapshot();
	for (uint64_t frame = 0; frame < source->frames(); ++frame)
	{
		const double position = double(frame) * 32000 / 44100;
		const auto index = static_cast<uint64_t>(position);
		const auto fraction = position - index;
		const float expected
			= ((index == 16000 ? 1 - fraction : 0) + (index + 1 == 16000 ? fraction : 0)) * (16000 / 32768.0f);
		check(std::abs(complete->trackSample(frame, 0) - expected) < 1e-6, "no accumulated resampling drift");
	}
	const auto replacementGeneration = state.begin({segment});
	check(state.progress() == 0 && state.snapshot()->trackSample(22050, 0) == complete->trackSample(22050, 0),
		"re-render keeps old B, excludes old coverage from new progress");
	const QByteArray replacement(1000 * 2, '\x20');
	check(state.publish(event(replacement, replacementGeneration, 0, 1, 0)), "replacement starts incrementally");
	check(state.snapshot()->trackSample(100, 0) != complete->trackSample(100, 0)
			&& state.snapshot()->trackSample(22050, 0) == complete->trackSample(22050, 0),
		"replace only new interval");
	check(!state.complete(replacementGeneration, 0, SVC_COMPLETE) && !state.finished(),
		"short result fails total duration");
	state.invalidate();
	check(!state.publish(event(second, replacementGeneration, 0, 2, 1000)), "invalidate rejects delayed callbacks");

	PlaybackState fragmented(source);
	const auto fragmentedGeneration = fragmented.begin({segment});
	uint64_t offset = 0;
	uint64_t index = 1;
	while (offset < 32000)
	{
		const auto count = std::min<uint64_t>(17, 32000 - offset);
		const auto bytes = pcm(offset, count);
		check(fragmented.publish(event(bytes, fragmentedGeneration, 0, index++, offset)),
			"small fragmented output accepted");
		offset += count;
	}
	check(fragmented.complete(fragmentedGeneration, 0, SVC_COMPLETE), "fragmented total valid");
	for (uint64_t frame = 0; frame < 44100; ++frame)
	{
		check(std::abs(fragmented.snapshot()->trackSample(frame, 0) - complete->trackSample(frame, 0)) < 1e-6,
			"fragmentation does not change waveform or impulse position");
	}

	const Segment contextual{1000, 4000, 900, 4100, 0, 44100};
	PlaybackState context(source);
	const auto contextGeneration = context.begin({contextual});
	const QByteArray contextPcm(3200 * 2, '\x10');
	check(context.publish(event(contextPcm, contextGeneration, 0, 1, 0, 44100))
			&& context.complete(contextGeneration, 0, SVC_COMPLETE),
		"contextual output complete");
	const auto contextSnapshot = context.snapshot();
	check(contextSnapshot->trackSample(999, 0) == .25f && contextSnapshot->trackSample(4000, 0) == .25f
			&& contextSnapshot->trackSample(1000, 0) != .25f && context.progress() == 1,
		"context cropped while effective source interval stays aligned");
	for (const auto rate : {32000u, 44100u, 48000u, 96000u})
	{
		PlaybackState native(source);
		const auto nativeGeneration = native.begin({segment});
		const auto nativeFirst = pcm(0, rate / 2, rate / 3, 24576);
		const auto nativeSecond = pcm(rate / 2, rate - rate / 2, rate / 3, 24576);
		check(native.publish(event(nativeFirst, nativeGeneration, 0, 1, 0, rate)), "native first chunk accepted");
		const auto retained = native.snapshot()->rendered.front().audio;
		check(retained->rate == rate && retained->frames == rate / 2, "output rate and native frame count preserved");
		check(native.publish(event(nativeSecond, nativeGeneration, 0, 2, rate / 2, rate)),
			"native second chunk accepted");
		check(native.complete(nativeGeneration, 0, SVC_COMPLETE), "native rate duration completed");
		const auto audio = native.snapshot()->rendered.front().audio;
		check(audio->rate == rate && audio->frames == rate && audio->chunks.size() == 2,
			"no source-rate intermediate PCM or chunk merge");
		check(audio->chunks[0].samples == retained->chunks[0].samples && retained->frames == rate / 2,
			"native PCM chunks shared while snapshots remain immutable");
		for (uint64_t frame = 0; frame < rate; ++frame)
		{
			check(audio->sample(frame) == (frame == rate / 3 ? .75f : 0.f), "native PCM bit-exact after publication");
		}
	}
	std::puts("PASS SVC M2 playback: provisional replacement, immutable blocks, stale rejection, absolute-phase "
			  "impulses, context cropping and re-render");
	return 0;
}
