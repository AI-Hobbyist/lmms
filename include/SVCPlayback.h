#pragma once

#include <atomic>
#include <memory>
#include <mutex>
#include <vector>

#include "SVCChunking.h"
#include "svc.h"

namespace lmms::svc {
struct SourceAudio
{
	uint32_t rate = 0;
	std::vector<float> stereo;
	uint64_t frames() const { return stereo.size() / 2; }
};

struct RenderedAudio
{
	struct Chunk
	{
		uint64_t offset;
		std::shared_ptr<const std::vector<float>> samples;
	};
	uint32_t rate = 0;
	uint64_t inputStart = 0; // Origin in source frames; PCM stays at the backend rate.
	uint64_t generation = 0;
	uint64_t segment = 0;
	uint64_t frames = 0;
	std::vector<Chunk> chunks;
	float sample(uint64_t frame) const;
};

struct PlaybackRegion
{
	uint64_t start = 0;
	uint64_t end = 0;
	uint64_t generation = 0;
	std::shared_ptr<const RenderedAudio> audio;
};

struct PlaybackSnapshot
{
	std::shared_ptr<const SourceAudio> source;
	std::vector<PlaybackRegion> rendered;
	float sourceSample(uint64_t frame, unsigned channel) const;
	const PlaybackRegion* renderedRegion(double frame) const;
	float renderedSample(double frame, bool& available) const;
	float trackSample(uint64_t frame, unsigned channel) const;
};

// Writer methods run outside the real-time callback. Readers retain immutable
// snapshots for an audio block; no network/file/decoder work occurs in readers.
class PlaybackState
{
public:
	explicit PlaybackState(std::shared_ptr<const SourceAudio> source);
	uint64_t invalidate();
	uint64_t begin(const std::vector<Segment>& segments);
	bool publish(const svc_event& event);
	bool complete(uint64_t generation, uint64_t segment, svc_status terminal);
	std::shared_ptr<const PlaybackSnapshot> snapshot() const;
	uint64_t generation() const { return m_generation.load(); }
	double progress() const;
	bool finished() const;
	bool segmentComplete(uint64_t segment) const;

private:
	struct RequestState
	{
		Segment segment;
		QString request;
		uint64_t chunks = 0;
		uint64_t received = 0;
		uint64_t published = 0;
		uint32_t rate = 0;
		std::shared_ptr<const RenderedAudio> audio;
		svc_status terminal = SVC_OK;
	};
	void install(uint64_t start, uint64_t end, std::shared_ptr<const RenderedAudio> audio);
	void publishCoverage(RequestState& request, bool final);
	mutable std::mutex m_mutex;
	std::atomic<uint64_t> m_generation{1};
	std::shared_ptr<const PlaybackSnapshot> m_snapshot;
	std::vector<RequestState> m_requests;
};
} // namespace lmms::svc
