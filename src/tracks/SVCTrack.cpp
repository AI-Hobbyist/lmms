#include "SVCTrack.h"

#include <QDomElement>
#include <QJsonArray>
#include <QJsonDocument>
#include <algorithm>
#include <cmath>

#include "AudioEngine.h"
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
	SVCPlaybackHandle(SVCTrack* track, std::shared_ptr<const svc::PlaybackSnapshot> snapshot, double position,
		f_cnt_t frames, f_cnt_t offset)
		: PlayHandle(Type::SVCPlayHandle, offset)
		, m_track(track)
		, m_snapshot(std::move(snapshot))
		, m_position(position)
		, m_frames(frames)
	{ setAudioBusHandle(track->audioBusHandle()); }
	bool isFinished() const override { return m_frames == 0; }
	bool isFromTrack(const Track* track) const override { return track == m_track; }
	void play(SampleFrame* buffer) override
	{
		const auto count = std::min(m_frames, Engine::audioEngine()->framesPerPeriod() - offset());
		const auto ratio = double(m_snapshot->source->rate) / Engine::audioEngine()->outputSampleRate();
		for (f_cnt_t frame = 0; frame < count; ++frame)
		{
			const double position = m_position + frame * ratio;
			if (position < 0 || position >= m_snapshot->source->frames()) { continue; }
			const auto index = static_cast<uint64_t>(position);
			const auto fraction = position - index;
			for (unsigned channel = 0; channel < 2; ++channel)
			{
				buffer[frame + offset()][channel] = m_snapshot->trackSample(index, channel) * (1 - fraction)
					+ m_snapshot->trackSample(std::min(index + 1, m_snapshot->source->frames() - 1), channel)
						* fraction;
			}
		}
		m_position += count * ratio;
		m_frames -= count;
		setOffset(0);
	}

private:
	SVCTrack* m_track;
	std::shared_ptr<const svc::PlaybackSnapshot> m_snapshot;
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
			continue;
		}
		const auto snapshot = clip->playback()->snapshot();
		if (snapshot->source->frames() == 0) { continue; }
		const auto remainder = Engine::getSong()->getTimeline().frameOffset();
		const double ticks = int(start) - int(clip->startPosition()) - int(clip->startTimeOffset());
		const auto position = ticks * Engine::framesPerTick(snapshot->source->rate)
			+ remainder * snapshot->source->rate / Engine::audioEngine()->outputSampleRate();
		// Song schedules tracks once per tick, even when that tick spans several
		// audio periods. Keep the handle alive until the next tick (or clip end),
		// rather than stopping at the end of the current period's fragment.
		const auto bounded = static_cast<f_cnt_t>(std::ceil(std::max(0.0,
			std::min(double(Engine::framesPerTick()) - remainder,
				(int(clip->endPosition()) - int(start)) * double(Engine::framesPerTick()) - remainder))));
		played = Engine::audioEngine()->addPlayHandle(new SVCPlaybackHandle(this, snapshot, position, bounded, offset))
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
