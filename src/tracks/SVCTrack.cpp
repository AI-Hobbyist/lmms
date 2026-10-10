#include "SVCTrack.h"

#include <QDomElement>
#include <QJsonArray>
#include <QJsonDocument>
#include <algorithm>
#include <array>
#include <cmath>

#include "AudioEngine.h"
#include "AudioResampler.h"
#include "EffectChain.h"
#include "Mixer.h"
#include "PlayHandle.h"
#include "SVCCatalog.h"
#include "SVCClip.h"
#include "SVCConversion.h"
#include "SVCViews.h"
#include "Song.h"
#include "panning.h"
#include "volume.h"

namespace lmms {
namespace {
class SVCPlaybackHandle : public PlayHandle
{
public:
	SVCPlaybackHandle(SVCTrack* track, std::shared_ptr<svc::PlaybackState> state,
		std::shared_ptr<std::atomic<bool>> active, double position, f_cnt_t frames, f_cnt_t offset)
		: PlayHandle(Type::SVCPlayHandle, offset)
		, m_track(track)
		, m_state(std::move(state))
		, m_active(std::move(active))
		, m_position(position)
		, m_frames(frames)
	{ setAudioBusHandle(track->audioBusHandle()); }
	bool isFinished() const override { return m_frames == 0 || !m_active->load(); }
	bool isFromTrack(const Track* track) const override { return track == m_track; }
	void play(SampleFrame* buffer) override
	{
		if (!m_active->load())
		{
			m_frames = 0;
			return;
		}
		const auto snapshot = m_state->snapshot();
		const auto count = std::min(m_frames, Engine::audioEngine()->framesPerPeriod() - offset());
		const auto outputRate = Engine::audioEngine()->outputSampleRate();
		const auto sourceRate = snapshot->source->rate;
		const auto ratio = double(sourceRate) / outputRate;
		f_cnt_t written = 0;
		while (written < count)
		{
			if (m_position < 0 || m_position >= snapshot->source->frames())
			{
				m_position += ratio;
				++written;
				continue;
			}
			const auto* region = snapshot->renderedRegion(m_position);
			const auto audio = region ? region->audio : nullptr;
			const auto rate = audio ? audio->rate : sourceRate;
			const auto origin = audio ? audio->inputStart : 0;
			const auto generation = audio ? audio->generation : 0;
			const auto segment = audio ? audio->segment : 0;
			auto boundary = region ? region->end : snapshot->source->frames();
			if (!region)
			{
				for (const auto& next : snapshot->rendered)
				{
					if (next.start > m_position)
					{
						boundary = next.start;
						break;
					}
				}
			}
			if (!m_streamReady || rate != m_rate || origin != m_origin || generation != m_generation
				|| segment != m_segment || outputRate != m_outputRate)
			{
				m_streamReady = true;
				m_rate = rate;
				m_origin = origin;
				m_generation = generation;
				m_segment = segment;
				m_outputRate = outputRate;
				m_inputFrame = static_cast<uint64_t>(std::max(0.0, (m_position - origin) * rate / sourceRate + 1e-7));
				m_inputOffset = m_inputCount = 0;
				if (rate != outputRate)
				{
					if (!m_resampler) { m_resampler = std::make_unique<AudioResampler>(AudioResampler::Mode::Linear); }
					else
					{
						m_resampler->reset();
					}
					m_resampler->setRatio(rate, outputRate);
				}
			}
			const auto run = std::min(count - written,
				static_cast<f_cnt_t>(std::max(1.0, std::ceil((boundary - m_position) / ratio - 1e-7))));
			const auto read = [&](uint64_t frame, unsigned channel) {
				return audio ? audio->sample(frame) : snapshot->sourceSample(frame, channel);
			};
			if (rate == outputRate)
			{
				// Native output already matches the device: no resampler or source-rate intermediate.
				for (f_cnt_t frame = 0; frame < run; ++frame, ++m_inputFrame)
				{
					for (unsigned channel = 0; channel < 2; ++channel)
					{
						buffer[offset() + written + frame][channel] = read(m_inputFrame, channel);
					}
				}
			}
			else
			{
				// Same AudioResampler / Linear mode and streaming input accounting as Sample::play.
				f_cnt_t generated = 0;
				while (generated < run)
				{
					if (m_inputOffset == m_inputCount)
					{
						const auto available = audio ? audio->frames : snapshot->source->frames();
						m_inputCount = std::min<uint64_t>(
							m_input.size(), available > m_inputFrame ? available - m_inputFrame : 0);
						m_inputOffset = 0;
						for (size_t frame = 0; frame < m_inputCount; ++frame, ++m_inputFrame)
						{
							for (unsigned channel = 0; channel < 2; ++channel)
							{
								m_input[frame][channel] = read(m_inputFrame, channel);
							}
						}
					}
					if (!m_inputCount) { break; }
					const auto result
						= m_resampler->process({&m_input[m_inputOffset][0], 2, m_inputCount - m_inputOffset},
							{&buffer[offset() + written + generated][0], 2, run - generated});
					m_inputOffset += result.inputFramesUsed;
					generated += result.outputFramesGenerated;
					if (!result.inputFramesUsed && !result.outputFramesGenerated) { break; }
				}
			}
			written += run;
			m_position += run * ratio;
		}
		m_frames -= count;
		setOffset(0);
	}

private:
	SVCTrack* m_track;
	std::shared_ptr<svc::PlaybackState> m_state;
	std::shared_ptr<std::atomic<bool>> m_active;
	std::unique_ptr<AudioResampler> m_resampler;
	std::array<SampleFrame, DEFAULT_BUFFER_SIZE> m_input;
	size_t m_inputOffset = 0, m_inputCount = 0;
	uint64_t m_inputFrame = 0, m_origin = 0, m_generation = 0, m_segment = 0;
	uint32_t m_rate = 0, m_outputRate = 0;
	bool m_streamReady = false;
	double m_position;
	f_cnt_t m_frames;
};
} // namespace

SVCTrack::SVCTrack(TrackContainer* container)
	: Track(Type::SVC, container)
	, m_volume(DefaultVolume, MinVolume, MaxVolume, 0.1f, this, tr("Volume"))
	, m_pan(DefaultPanning, PanningLeft, PanningRight, 0.1f, this, tr("Panning"))
	, m_mix(0, 0, Engine::mixer()->numChannels() - 1, this, tr("Mixer channel"))
	, m_bus("SVC", true, &m_volume, &m_pan, &m_mutedModel)
{
	setName(tr("Singing Voice Conversion"));
	m_chunks = svc::chunkDefaults();
	m_pan.setCenterValue(DefaultPanning);
	connect(&m_mix, &IntModel::dataChanged, this, [this]() { m_bus.setNextMixerChannel(m_mix.value()); });
	connect(this, &SVCTrack::renderRequested, this, [this] { svc::ConversionService::instance().render(this); });
	connect(&svc::Catalog::instance(), &svc::Catalog::connectionChanged, this, [this](const QString& id) {
		if (m_selection.value("engine_id") == id) { svc::ConversionService::instance().cancel(this); }
	});
}

SVCTrack::~SVCTrack()
{
	Engine::audioEngine()->removePlayHandlesOfTypes(this, PlayHandle::Type::SVCPlayHandle);
	for (auto* clip : getClips())
	{
		disconnect(clip, &QObject::destroyed, this, nullptr);
		static_cast<SVCClip*>(clip)->invalidate();
	}
}

void SVCTrack::setName(const QString& name)
{
	Track::setName(name);
	m_bus.setName(name);
}

bool SVCTrack::play(const TimePos& start, f_cnt_t, f_cnt_t offset, int clipNumber)
{
	if (clipNumber >= 0 || isMuted() || !tryLock()) { return false; }
	bool played = false;
	for (auto* base : getClips())
	{
		auto* clip = static_cast<SVCClip*>(base);
		if (clip->isMuted() || !clip->playback() || start < clip->startPosition() || start >= clip->endPosition())
		{
			const auto active = m_activePlayback.find(clip);
			if (active != m_activePlayback.end())
			{
				if (auto token = active->second.active.lock()) { token->store(false); }
			}
			continue;
		}
		const auto snapshot = clip->playback()->snapshot();
		if (snapshot->source->frames() == 0) { continue; }
		auto found = m_activePlayback.find(clip);
		if (found == m_activePlayback.end())
		{
			found = m_activePlayback.emplace(clip, ActivePlayback{}).first;
			connect(clip, &QObject::destroyed, this, [this, clip]() {
				const auto found = m_activePlayback.find(clip);
				if (found != m_activePlayback.end())
				{
					if (auto active = found->second.active.lock()) { active->store(false); }
					m_activePlayback.erase(found);
				}
			});
		}
		auto& previous = found->second;
		const auto origin = int(clip->startPosition()) + int(clip->startTimeOffset());
		if (auto active = previous.active.lock())
		{
			if (active->load() && previous.nextTick == int(start) && previous.origin == origin
				&& previous.end == int(clip->endPosition()) && previous.state.lock() == clip->playback())
			{
				previous.nextTick = int(start) + 1;
				played = true;
				continue;
			}
			active->store(false);
		}
		const auto remainder = Engine::getSong()->getTimeline().frameOffset();
		const double ticks = int(start) - int(clip->startPosition()) - int(clip->startTimeOffset());
		const auto position = ticks * Engine::framesPerTick(snapshot->source->rate)
			+ remainder * snapshot->source->rate / Engine::audioEngine()->outputSampleRate();
		// A single streaming handle preserves the resampler state across periods/ticks.
		// Seek/loop/crop changes cancel it and start a new stream at the requested time.
		const auto bounded = static_cast<f_cnt_t>(std::ceil(
			std::max(0.0, (int(clip->endPosition()) - int(start)) * double(Engine::framesPerTick()) - remainder)));
		auto active = std::make_shared<std::atomic<bool>>(true);
		previous = {active, clip->playback(), int(start) + 1, origin, int(clip->endPosition())};
		played = Engine::audioEngine()->addPlayHandle(
					 new SVCPlaybackHandle(this, clip->playback(), active, position, bounded, offset))
			|| played;
	}
	unlock();
	if (played) { emit playbackActivity(); }
	return played;
}

gui::TrackView* SVCTrack::createView(gui::TrackContainerView* container)
{ return new gui::SVCTrackView(this, container); }

Clip* SVCTrack::createClip(const TimePos& position)
{
	auto* clip = new SVCClip(this);
	clip->movePosition(position);
	return clip;
}

void SVCTrack::invalidateClips()
{
	for (auto* clip : getClips())
	{
		static_cast<SVCClip*>(clip)->invalidate();
	}
	emit dataChanged();
}

bool SVCTrack::setSelection(const QJsonObject& selection)
{
	// Connection credentials live in engine settings, never in project selection.
	const QStringList allowed{"engine_id", "model_id", "weight_id", "speaker_id", "index_id", "parameters"};
	for (auto it = selection.begin(); it != selection.end(); ++it)
	{
		if (!allowed.contains(it.key())) { return false; }
	}
	const auto encoded = QJsonDocument(selection).toJson(QJsonDocument::Compact).toLower();
	if (encoded.contains("\"token\"") || encoded.contains("\"authorization\"") || encoded.contains("\"bearer_token\""))
	{
		return false;
	}
	if (selection == m_selection) { return true; }
	const auto modelName = [](const QJsonObject& selection) {
		const auto profile = svc::Catalog::instance().engine(selection.value("engine_id").toString());
		for (const auto& value : profile.capabilities.value("models").toArray())
		{
			const auto model = value.toObject();
			if (model.value("id") == selection.value("model_id"))
			{
				return model.value("name").toString(model.value("id").toString());
			}
		}
		return QString{};
	};
	const auto previousModel = modelName(m_selection);
	const auto nextModel = modelName(selection);
	const auto defaultName
		= name() == tr("Singing Voice Conversion") || (!previousModel.isEmpty() && name() == previousModel);
	addJournalCheckPoint();
	m_selection = selection;
	if (defaultName && !nextModel.isEmpty()) { setName(nextModel); }
	invalidateClips();
	Engine::getSong()->setModified();
	return true;
}

bool SVCTrack::setChunkConfig(const svc::ChunkConfig& config)
{
	if (!config.validate().isEmpty()) { return false; }
	addJournalCheckPoint();
	m_chunks = config;
	invalidateClips();
	Engine::getSong()->setModified();
	return true;
}

void SVCTrack::saveTrackSpecificSettings(QDomDocument& document, QDomElement& element, bool)
{
	element.setAttribute("schemaVersion", 1);
	element.setAttribute("selection", QString::fromUtf8(QJsonDocument(m_selection).toJson(QJsonDocument::Compact)));
	element.setAttribute("silenceThresholdDbfs", m_chunks.silenceThresholdDbfs);
	element.setAttribute("lengthThresholdSeconds", m_chunks.lengthThresholdSeconds);
	element.setAttribute("forcedChunkSeconds", m_chunks.forcedChunkSeconds);
	m_volume.saveSettings(document, element, "vol");
	m_pan.saveSettings(document, element, "pan");
	m_mix.saveSettings(document, element, "mixch");
	m_bus.effects()->saveState(document, element);
}

void SVCTrack::loadTrackSpecificSettings(const QDomElement& element)
{
	m_selection = QJsonDocument::fromJson(element.attribute("selection").toUtf8()).object();
	m_chunks = {element.attribute("silenceThresholdDbfs", "-70").toDouble(),
		element.attribute("lengthThresholdSeconds", "30").toDouble(),
		element.attribute("forcedChunkSeconds", "10").toDouble()};
	m_volume.loadSettings(element, "vol");
	m_pan.loadSettings(element, "pan");
	m_mix.setRange(0, Engine::mixer()->numChannels() - 1);
	m_mix.loadSettings(element, "mixch");
	m_bus.effects()->clear();
	const auto effects = element.firstChildElement(m_bus.effects()->nodeName());
	if (!effects.isNull()) { m_bus.effects()->restoreState(effects); }
}
} // namespace lmms
