#ifndef LMMS_SVS_PIANO_PREVIEW_H
#define LMMS_SVS_PIANO_PREVIEW_H

#include <algorithm>
#include <atomic>
#include <cmath>
#include <memory>

#include "AudioBusHandle.h"
#include "AudioEngine.h"
#include "Engine.h"
#include "PlayHandle.h"
#include "Sample.h"
#include "Song.h"

namespace lmms::gui {
// The audio engine owns the handle; the editor only retains its release flag.
class SVSPianoPreview final : public PlayHandle
{
public:
	SVSPianoPreview(const Track* track, std::shared_ptr<const SampleBuffer> sample, int pitch,
		std::shared_ptr<std::atomic_bool> released)
		: PlayHandle(Type::SamplePlayHandle)
		, m_track(track)
		, m_sample(std::move(sample))
		, m_released(std::move(released))
		, m_ratio(std::exp2((60. - pitch) / 12.))
	{
		// The bundled piano01 sample is C4. Keep audition independent of the voicebank and track mute.
		setAudioBusHandle(new AudioBusHandle("SVS piano preview", false));
	}

	~SVSPianoPreview() override { delete audioBusHandle(); }

	bool isFinished() const override { return m_finished; }
	bool isFromTrack(const Track* track) const override { return track == m_track; }

	void play(SampleFrame* buffer) override
	{
		const auto frames = Engine::audioEngine()->framesPerPeriod();
		zeroSampleFrames(buffer, frames);
		if (Engine::getSong()->isExporting() || m_finished)
		{
			m_finished = true;
			return;
		}
		if (!m_sample.play(buffer, &m_playback, frames, Sample::Loop::Off, m_ratio)) { m_finished = true; }
		const auto rate = Engine::audioEngine()->outputSampleRate();
		for (f_cnt_t frame = 0; frame < frames; ++frame)
		{
			if (m_released->load(std::memory_order_relaxed))
			{
				m_releaseGain = std::max(0., m_releaseGain - 1. / (rate * .015));
			}
			const auto attack = std::min(1., double(m_frames++) / (rate * .003));
			buffer[frame] *= float(.65 * attack * m_releaseGain);
		}
		m_finished |= m_releaseGain <= 0;
	}

private:
	const Track* m_track;
	Sample m_sample;
	Sample::PlaybackState m_playback;
	std::shared_ptr<std::atomic_bool> m_released;
	double m_ratio;
	double m_releaseGain = 1;
	uint64_t m_frames = 0;
	bool m_finished = false;
};
} // namespace lmms::gui

#endif
