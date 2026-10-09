#ifndef LMMS_SVS_UNSELECTED_PREVIEW_H
#define LMMS_SVS_UNSELECTED_PREVIEW_H

#include <QCoreApplication>
#include <algorithm>
#include <cmath>
#include <numbers>

#include "SVSModel.h"

namespace lmms::svs {
inline std::shared_ptr<const Audio> renderUnselectedPreview(const Input& input, QString& error,
															const std::shared_ptr<RenderControl>& control)
{
	TimeMapping mapping;
	if (!readTimeMapping(input.document, input.secondsPerTick, mapping, error))
	{
		return {};
	}
	const double frames = std::ceil(input.duration * input.rate);
	if (!std::isfinite(frames) || frames < 0 || frames > 64 * 1024 * 1024 || input.rate == 0)
	{
		error = QCoreApplication::translate("NativeSVS", "Invalid SVS preview duration");
		return {};
	}
	auto audio = std::make_shared<Audio>();
	audio->rate = input.rate;
	audio->revision = input.revision;
	audio->mapping = mapping;
	audio->samples.resize(std::size_t(frames) * 2);
	for (const auto& note : input.notes)
	{
		if (control->cancelled)
		{
			return {};
		}
		const double start = mapping.localSeconds(note.tick);
		const double end = mapping.localSeconds(note.tick + note.duration);
		const double frequency = 440 * std::exp2((note.pitch - 69) / 12);
		if (!std::isfinite(start) || !std::isfinite(end) || !std::isfinite(frequency) || end <= start)
		{
			error = QCoreApplication::translate("NativeSVS", "Invalid SVS preview note");
			return {};
		}
		const auto first = std::size_t(std::clamp(std::ceil(start * input.rate), 0., frames));
		const auto last = std::size_t(std::clamp(std::ceil(end * input.rate), 0., frames));
		for (auto frame = first; frame < last; ++frame)
		{
			if ((frame & 4095) == 0 && control->cancelled)
			{
				return {};
			}
			const double seconds = double(frame) / input.rate;
			const double envelope = std::clamp(std::min(seconds - start, end - seconds) / .005, 0., 1.);
			const float value = float(.15 * envelope * std::sin(2 * std::numbers::pi * frequency * (seconds - start)));
			for (int channel = 0; channel < 2; ++channel)
			{
				audio->samples[2 * frame + channel]
					= std::clamp(audio->samples[2 * frame + channel] + value, -1.f, 1.f);
			}
		}
	}
	audio->waveform.build(audio->samples);
	return audio;
}
} // namespace lmms::svs
#endif
