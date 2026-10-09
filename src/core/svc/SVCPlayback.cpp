#include "SVCPlayback.h"

#include <QCoreApplication>
#include <QtEndian>
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace lmms::svc {
float PlaybackSnapshot::sourceSample(uint64_t frame, unsigned channel) const
{ return source && frame < source->frames() && channel < 2 ? source->stereo[frame * 2 + channel] : 0; }

float PlaybackSnapshot::renderedSample(uint64_t frame, bool& available) const
{
	const auto next = std::upper_bound(rendered.begin(), rendered.end(), frame,
		[](uint64_t position, const PlaybackRegion& region) { return position < region.start; });
	if (next != rendered.begin())
	{
		const auto& region = *std::prev(next);
		if (frame < region.end)
		{
			available = true;
			return (*region.mono)[frame - region.storageStart];
		}
	}
	available = false;
	return 0;
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

void PlaybackState::install(uint64_t start, std::shared_ptr<const std::vector<float>> data)
{
	if (data->empty()) { return; }
	auto next = std::make_shared<PlaybackSnapshot>();
	next->source = m_snapshot->source;
	const auto end = start + data->size();
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
	next->rendered.push_back({start, end, start, m_generation, std::move(data)});
	std::sort(next->rendered.begin(), next->rendered.end(),
		[](const auto& first, const auto& second) { return first.start < second.start; });
	std::atomic_store(&m_snapshot, std::shared_ptr<const PlaybackSnapshot>(next));
}

void PlaybackState::resample(RequestState& request, const std::vector<float>& data, uint64_t dataOffset, bool final)
{
	// Keep absolute phase across incoming chunks; only the last neighbor sample
	// waits for the next chunk. Context/padding never enters an effective region.
	auto samples = std::make_shared<std::vector<float>>();
	const auto& segment = request.segment;
	const uint64_t first = std::max(segment.start, segment.inputStart + request.cursor);
	while (request.cursor < segment.transmittedFrames())
	{
		const double position = double(request.cursor) * request.rate / segment.sampleRate;
		const auto index = static_cast<uint64_t>(std::floor(position));
		const auto fraction = position - index;
		if (!final && (index >= request.received || (fraction > 1e-12 && index + 1 >= request.received))) { break; }
		const auto effectiveFrame = segment.inputStart + request.cursor;
		if (effectiveFrame >= segment.start && effectiveFrame < segment.end)
		{
			const auto at = [&](uint64_t point) {
				if (point < dataOffset || point - dataOffset >= data.size()) { return request.last; }
				return data[point - dataOffset];
			};
			samples->push_back(at(index) * (1 - fraction) + at(index + 1) * fraction);
		}
		++request.cursor;
	}
	request.published += samples->size();
	install(first, samples);
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
	std::vector<float> decoded;
	const auto offset = request.received ? request.received - 1 : 0;
	if (request.received) { decoded.push_back(request.last); }
	for (uint64_t index = 0; index < event.sample_count; ++index)
	{
		decoded.push_back(qFromLittleEndian<int16_t>(event.bytes + index * 2) / 32768.0f);
	}
	request.received += event.sample_count;
	++request.chunks;
	resample(request, decoded, offset, false);
	request.last = decoded.back();
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
	resample(request, {request.last}, request.received - 1, true);
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
