#include "SVCPlayback.h"

#include <QCoreApplication>
#include <QtEndian>
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace lmms::svc {
float PlaybackSnapshot::sourceSample(uint64_t frame, unsigned channel) const
{ return source && frame < source->frames() && channel < 2 ? source->stereo[frame * 2 + channel] : 0; }

float RenderedAudio::sample(uint64_t frame) const
{
	if (chunks.empty() || !frames) { return 0; }
	frame = std::min(frame, frames - 1);
	const auto next = std::upper_bound(chunks.begin(), chunks.end(), frame,
		[](uint64_t position, const Chunk& chunk) { return position < chunk.offset; });
	const auto& chunk = *std::prev(next);
	return (*chunk.samples)[frame - chunk.offset];
}

const PlaybackRegion* PlaybackSnapshot::renderedRegion(double frame) const
{
	const auto next = std::upper_bound(rendered.begin(), rendered.end(), frame,
		[](double position, const PlaybackRegion& region) { return position < region.start; });
	if (next != rendered.begin())
	{
		const auto& region = *std::prev(next);
		if (frame < region.end) { return &region; }
	}
	return nullptr;
}

float PlaybackSnapshot::renderedSample(double frame, bool& available) const
{
	const auto* region = renderedRegion(frame);
	available = region != nullptr;
	if (!region) { return 0; }
	const auto& audio = *region->audio;
	const auto position = (frame - audio.inputStart) * audio.rate / source->rate;
	const auto index = static_cast<uint64_t>(std::max(0.0, position));
	const auto fraction = position - index;
	// A display/audition lookup projects time into native PCM without storing
	// a source-rate copy. Track playback consumes the native PCM directly.
	return audio.sample(index) * (1 - fraction) + audio.sample(index + 1) * fraction;
}

float PlaybackSnapshot::trackSample(uint64_t frame, unsigned channel) const
{
	bool available;
	const auto rendered = renderedSample(frame, available);
	return available ? rendered : sourceSample(frame, channel);
}

PlaybackState::PlaybackState(std::shared_ptr<const SourceAudio> source)
{
	if (!source || !source->rate || source->stereo.size() % 2)
	{
		throw std::invalid_argument(QCoreApplication::translate("NativeRVC", "Invalid SVC source").toStdString());
	}
	auto snapshot = std::make_shared<PlaybackSnapshot>();
	snapshot->source = std::move(source);
	m_snapshot = snapshot;
}

uint64_t PlaybackState::invalidate()
{
	std::lock_guard lock(m_mutex);
	m_requests.clear();
	return ++m_generation;
}

uint64_t PlaybackState::begin(const std::vector<Segment>& segments)
{
	std::lock_guard lock(m_mutex);
	const auto source = m_snapshot->source;
	uint64_t end = 0;
	for (const auto& segment : segments)
	{
		if (segment.start < end || segment.start >= segment.end || segment.end > source->frames()
			|| segment.inputStart > segment.start || segment.inputEnd < segment.end
			|| segment.inputEnd > source->frames() || segment.sampleRate != source->rate)
		{
			throw std::invalid_argument(
				QCoreApplication::translate("NativeRVC", "Invalid SVC segment map").toStdString());
		}
		end = segment.end;
	}
	++m_generation;
	m_requests.clear();
	for (const auto& segment : segments)
	{
		RequestState request;
		request.segment = segment;
		m_requests.push_back(request);
	}
	return m_generation;
}

void PlaybackState::install(uint64_t start, uint64_t end, std::shared_ptr<const RenderedAudio> audio)
{
	if (start >= end) { return; }
	auto next = std::make_shared<PlaybackSnapshot>();
	next->source = m_snapshot->source;
	for (const auto& region : m_snapshot->rendered)
	{
		if (region.end <= start || region.start >= end) { next->rendered.push_back(region); }
		else
		{
			if (region.start < start)
			{
				auto left = region;
				left.end = start;
				next->rendered.push_back(left);
			}
			if (region.end > end)
			{
				auto right = region;
				right.start = end;
				next->rendered.push_back(right);
			}
		}
	}
	next->rendered.push_back({start, end, m_generation, std::move(audio)});
	std::sort(next->rendered.begin(), next->rendered.end(),
		[](const auto& first, const auto& second) { return first.start < second.start; });
	std::atomic_store(&m_snapshot, std::shared_ptr<const PlaybackSnapshot>(next));
}

void PlaybackState::publishCoverage(RequestState& request, bool final)
{
	const auto& segment = request.segment;
	const auto covered = final
		? segment.inputEnd
		: segment.inputStart + static_cast<uint64_t>(double(request.received) * segment.sampleRate / request.rate);
	const auto end = std::min(segment.end, covered);
	request.published = end > segment.start ? end - segment.start : 0;
	install(segment.start, end, request.audio);
}

bool PlaybackState::publish(const svc_event& event)
{
	std::lock_guard lock(m_mutex);
	if (event.generation_id != m_generation || event.segment_id >= m_requests.size()) { return false; }
	auto& request = m_requests[event.segment_id];
	if (request.terminal != SVC_OK || event.type != SVC_AUDIO || !event.request_id || !event.bytes
		|| event.channels != 1 || event.sample_rate < 8000 || event.sample_rate > 384000 || !event.sample_count
		|| event.sample_count > SVC_MAX_PART / 2 || event.byte_count != event.sample_count * 2
		|| event.sample_offset != request.received || event.chunk_index != request.chunks + 1
		|| (request.rate && request.rate != event.sample_rate)
		|| (!request.request.isEmpty() && request.request != QString::fromUtf8(event.request_id)))
	{
		return false;
	}
	const auto projected
		= double(request.received + event.sample_count) * request.segment.sampleRate / event.sample_rate;
	const auto tolerance = 1.0 + double(request.segment.sampleRate) / event.sample_rate;
	if (projected > request.segment.transmittedFrames() + tolerance) { return false; }
	request.request = QString::fromUtf8(event.request_id);
	request.rate = event.sample_rate;
	auto decoded = std::make_shared<std::vector<float>>();
	decoded->reserve(event.sample_count);
	for (uint64_t index = 0; index < event.sample_count; ++index)
	{
		decoded->push_back(qFromLittleEndian<int16_t>(event.bytes + index * 2) / 32768.0f);
	}
	auto audio = request.audio ? std::make_shared<RenderedAudio>(*request.audio) : std::make_shared<RenderedAudio>();
	audio->rate = request.rate;
	audio->inputStart = request.segment.inputStart;
	audio->generation = event.generation_id;
	audio->segment = event.segment_id;
	audio->chunks.push_back({request.received, std::move(decoded)});
	request.received += event.sample_count;
	audio->frames = request.received;
	request.audio = std::move(audio);
	++request.chunks;
	publishCoverage(request, false);
	return true;
}

bool PlaybackState::complete(uint64_t generation, uint64_t segment, svc_status terminal)
{
	std::lock_guard lock(m_mutex);
	if (generation != m_generation || segment >= m_requests.size()) { return false; }
	auto& request = m_requests[segment];
	if (request.terminal != SVC_OK) { return false; }
	if (terminal != SVC_COMPLETE)
	{
		request.terminal = terminal == SVC_CANCELLED ? SVC_CANCELLED : SVC_FAILED;
		return false;
	}
	const auto tolerance = 1.0 + double(request.segment.sampleRate) / std::max<uint32_t>(1, request.rate);
	if (!request.rate
		|| std::abs(double(request.received) * request.segment.sampleRate / request.rate
			   - request.segment.transmittedFrames())
			> tolerance)
	{
		request.terminal = SVC_FAILED;
		return false;
	}
	publishCoverage(request, true);
	request.terminal = SVC_COMPLETE;
	return true;
}

std::shared_ptr<const PlaybackSnapshot> PlaybackState::snapshot() const
{ return std::atomic_load(&m_snapshot); }

double PlaybackState::progress() const
{
	std::lock_guard lock(m_mutex);
	uint64_t published = 0;
	uint64_t target = 0;
	for (const auto& request : m_requests)
	{
		published += request.published;
		target += request.segment.frames();
	}
	return target ? double(published) / target : 0;
}

bool PlaybackState::finished() const
{
	std::lock_guard lock(m_mutex);
	return !m_requests.empty() && std::all_of(m_requests.begin(), m_requests.end(), [](const auto& request) {
		return request.terminal == SVC_COMPLETE;
	});
}

bool PlaybackState::segmentComplete(uint64_t segment) const
{
	std::lock_guard lock(m_mutex);
	return segment < m_requests.size() && m_requests[segment].terminal == SVC_COMPLETE;
}
} // namespace lmms::svc
