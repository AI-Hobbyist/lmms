#include "SVSTrack.h"
#include "SVSClip.h"
#include "SVSTimeMapping.h"
#include "SVSSynthesisScheduler.h"
#include "SVSXml.h"
#include "SVSViews.h"
#include "ConfigManager.h"
#include "Engine.h"
#include "AudioEngine.h"
#include "Mixer.h"
#include "Song.h"
#include "EffectChain.h"
#include "SampleFrame.h"
#include "volume.h"
#include "panning.h"
#include <cmath>
#include <algorithm>
#include <QFile>
#include <QDir>
#include <QJsonDocument>
#include <QPointer>
#include <QCoreApplication>
#include <QThreadPool>
#include <QRunnable>
namespace lmms {
namespace {
class SVSPlaybackHandle : public PlayHandle
{
public:
	SVSPlaybackHandle(SVSTrack* track, std::shared_ptr<const svs::Audio> audio,
		std::shared_ptr<const svs::VolumeAutomation> volume, double start, f_cnt_t frames, f_cnt_t offset)
		: PlayHandle(Type::SVSPlayHandle, offset)
		, m_track(track)
		, m_audio(std::move(audio))
		, m_volume(std::move(volume))
		, m_start(start)
		, m_frames(frames)
	{
		setAudioBusHandle(track->audioBusHandle());
	}
	bool isFinished() const override { return m_frames == 0; }
	bool isFromTrack(const Track* track) const override { return track == m_track; }
	void play(SampleFrame* buffer) override
	{
		const double ratio = double(m_audio->rate) / Engine::audioEngine()->outputSampleRate();
		const auto available = Engine::audioEngine()->framesPerPeriod();
		const auto count = std::min(m_frames, available - offset());
		for (f_cnt_t f = 0; f < count; ++f)
		{
			const double position = m_start + f * ratio;
			if (!std::isfinite(position) || position < 0 || position + 1 >= double(m_audio->samples.size() / 2))
				continue;
			auto index = static_cast<size_t>(position);
			const float fraction = position - index;
			const double tick = m_audio->mapping.tickAtLocalSeconds(
				m_audio->mapping.localSeconds(m_audio->startTick) + position / m_audio->rate);
			const double gain = m_volume ? m_volume->gainAt(tick) : 1.;
			for (int channel = 0; channel < 2; ++channel)
				buffer[f + offset()][channel] = gain
					* (m_audio->samples[index * 2 + channel] * (1 - fraction)
						+ m_audio->samples[(index + 1) * 2 + channel] * fraction);
		}
		m_start += count * ratio;
		m_frames -= count;
		setOffset(0);
	}

private:
	SVSTrack* m_track;
	std::shared_ptr<const svs::Audio> m_audio;
	std::shared_ptr<const svs::VolumeAutomation> m_volume;
	double m_start;
	f_cnt_t m_frames;
};
}
SVSTrack::SVSTrack(TrackContainer* tc)
	: Track(Type::SVS, tc)
	, m_volume(DefaultVolume, MinVolume, MaxVolume, 0.1f, this, "Volume")
	, m_pan(DefaultPanning, PanningLeft, PanningRight, 1, this, "Panning")
	, m_mix(0, 0, Engine::mixer()->numChannels() - 1, this, "Mixer channel")
	, m_bus("SVS", true, &m_volume, &m_pan, &m_mutedModel)
{
	svs::addHostVolume(m_capabilities);
	Track::setName("SVS");
	connect(Engine::getSong(), &Song::playbackStateChanged, this, [this] {
		if (!Engine::getSong()->isPlaying()) { clearNoteActivity(); }
	});
	connect(Engine::getSong(), &Song::playbackPositionJumped, this, &SVSTrack::clearNoteActivity);
	connect(&m_mutedModel, &BoolModel::dataChanged, this, [this] {
		if (isMuted()) { clearNoteActivity(); }
	});
	m_pan.setCenterValue(DefaultPanning);
	connect(&m_mix, &IntModel::dataChanged, this, [this] { m_bus.setNextMixerChannel(m_mix.value()); });
	m_portraitSettings = {{"visible", ConfigManager::inst()->value("svs", "portraitVisible", "1") != "0"},
		{"transparency", std::clamp(ConfigManager::inst()->value("svs", "portraitTransparency", "70").toInt(), 0, 100)},
		{"x", 1.}, {"y", 1.}};
	connect(&svs::Registry::instance(), &svs::Registry::catalogChanged, this, [this](const QString& plugin) {
		if (plugin != m_pluginId)
			return;
		svs::Voice next;
		for (const auto& candidate : svs::Registry::instance().voices())
			if (candidate.pluginId == plugin && candidate.id == m_voiceId)
			{
				next = candidate;
				break;
			}
		const bool contentChanged = next.version != m_voice.version || next.id != m_voice.id;
		m_voice = next;
		if (!m_customName && !next.name.isEmpty())
		{
			Track::setName(next.name);
			m_bus.setName(name());
		}
		if (contentChanged)
		{
			++m_capabilityRequest;
			m_capabilitiesReady = false;
			m_capabilities = {};
			svs::addHostVolume(m_capabilities);
			m_dictionaries.clear();
			m_capabilityDiagnostics = next.id.isEmpty() ? QStringList{QCoreApplication::translate("NativeSVS",
															  "Voicebank is missing; project data is retained")}
														: QStringList{};
			for (auto* base : getClips())
				static_cast<SVSClip*>(base)->invalidate();
			if (!next.id.isEmpty())
				refreshCapabilities();
		}
		emit dataChanged();
	});
	connect(&svs::Registry::instance(), &svs::Registry::catalogScanFinished, this,
		[this](const QString& plugin, const QString&) {
			if (plugin == m_pluginId && m_voice.id.isEmpty())
				for (auto* base : getClips())
					static_cast<SVSClip*>(base)->synthesize();
		});
}
SVSTrack::~SVSTrack()
{
	const auto* preview = Engine::getSong()->previewClip();
	if (preview && preview->getTrack() == this) { Engine::getSong()->stopPreviewOf(preview); }
	Engine::audioEngine()->removePlayHandlesOfTypes(this, PlayHandle::Type::SVSPlayHandle);
}
void SVSTrack::setName(const QString& name)
{
	m_customName = true;
	Track::setName(name);
	m_bus.setName(name);
}
void SVSTrack::restoreVoiceName()
{
	addJournalCheckPoint();
	m_customName = false;
	Track::setName(m_voice.name.isEmpty() ? "SVS" : m_voice.name);
	m_bus.setName(name());
}
void SVSTrack::bindVoice(const QString& plugin, const QString& voice)
{
	if (readOnly())
		return;
	addJournalCheckPoint();
	m_pluginId = plugin;
	m_voiceId = voice;
	m_voice = {};
	for (const auto& v : svs::Registry::instance().voices())
		if (v.pluginId == plugin && v.id == voice)
		{
			m_voice = v;
			break;
		}
	// Local developer fixtures are opt-in configuration, never SDK/package resources.
	if (plugin == "org.lmms.svs.example")
		for (const auto& key : {QString("avatarPath"), QString("portraitPath")})
		{
			const auto path = ConfigManager::inst()->value("svs", "test" + key.left(1).toUpper() + key.mid(1));
			if (!m_portraitSettings.contains(key) && !path.isEmpty())
				m_portraitSettings[key] = path;
		}
	++m_capabilityRequest;
	m_capabilities = {};
	svs::addHostVolume(m_capabilities);
	m_capabilitiesReady = false;
	m_dictionaries.clear();
	m_capabilityDiagnostics.clear();
	refreshCapabilities();
	if (!m_customName)
		Track::setName(m_voice.name.isEmpty() ? "SVS" : m_voice.name);
	m_bus.setName(name());
	for (auto* clip : getClips())
	{
		auto* c = static_cast<SVSClip*>(clip);
		c->invalidate();
		c->synthesize();
	}
	emit dataChanged();
	Engine::getSong()->setModified();
}
Clip* SVSTrack::createClip(const TimePos& pos)
{
	auto* clip = new SVSClip(this);
	clip->movePosition(pos);
	return clip;
}
bool SVSTrack::setParameter(const QString& id, const QJsonValue& value)
{
	if (readOnly())
		return false;
	const auto* parameter = m_capabilities.parameter(id, "track");
	if (!parameter || !parameter->writable || !parameter->enabled || !parameter->accepts(value))
		return false;
	if (m_parameters.value(id) == value)
		return true;
	addJournalCheckPoint();
	m_parameters[id] = value;
	refreshCapabilities();
	for (auto* clip : getClips())
	{
		auto* c = static_cast<SVSClip*>(clip);
		c->invalidate();
		c->synthesize();
	}
	emit dataChanged();
	Engine::getSong()->setModified();
	return true;
}
bool SVSTrack::setLanguage(const QString& value)
{
	if (readOnly())
		return false;
	if (!m_capabilities.languages.contains(value))
		return false;
	if (m_language == value)
		return true;
	addJournalCheckPoint();
	m_language = value;
	refreshCapabilities();
	for (auto* clip : getClips())
	{
		auto* c = static_cast<SVSClip*>(clip);
		c->invalidate();
		c->synthesize();
	}
	emit dataChanged();
	Engine::getSong()->setModified();
	return true;
}
void SVSTrack::refreshCapabilities(const QJsonObject& editorContext)
{
	if (readOnly())
		return;
	auto plugin = svs::Registry::instance().plugin(m_pluginId);
	if (!plugin || m_voice.id.isEmpty())
		return;
	const auto request = ++m_capabilityRequest;
	const auto voice = m_voiceId;
	const auto package = m_voice.package;
	auto context = m_parameters;
	for (auto i = editorContext.begin(); i != editorContext.end(); ++i)
		context[i.key()] = i.value();
	auto clipParameters = context["clipParameters"].toObject();
	clipParameters.remove(svs::VolumeId);
	if (context.contains("clipParameters")) { context["clipParameters"] = clipParameters; }
	context["language"] = m_language;
	QPointer<SVSTrack> target(this);
	svs::SynthesisScheduler::instance().declarationPool().start(QRunnable::create([plugin, target, voice, package,
																					  request, context] {
		QString error;
		auto declaration = plugin->capabilities(voice, context, error);
		svs::Capabilities parsed;
		if (error.isEmpty())
			svs::Capabilities::parse(declaration, parsed, error);
		QVector<svs::Dictionary> dictionaries;
		QStringList diagnostics;
		if (error.isEmpty())
			for (const auto& resource : parsed.dictionaryResources)
			{
				const auto root = QFileInfo(package).canonicalFilePath() + "/";
				const auto path = QFileInfo(QDir(package).filePath(resource)).canonicalFilePath();
				QFile file(path);
				svs::Dictionary dictionary;
				QString reason;
				if (!path.startsWith(root, Qt::CaseInsensitive) || !file.open(QIODevice::ReadOnly))
					diagnostics << QCoreApplication::translate("NativeSVS", "Missing dictionary: %1").arg(resource);
				else if (!svs::Dictionary::parse(file.read(4 * 1024 * 1024 + 1), parsed.phonemeSet, dictionary, reason))
					diagnostics << resource + ": " + reason;
				else if (dictionary.phonemeSet != parsed.phonemeSetId
					|| !parsed.languages.contains(dictionary.language))
					diagnostics << QCoreApplication::translate(
						"NativeSVS", "%1: incompatible dictionary language/phoneme set")
									   .arg(resource);
				else
					dictionaries.push_back(dictionary);
			}
		QMetaObject::invokeMethod(
			QCoreApplication::instance(),
			[target, request, parsed, dictionaries, diagnostics, error] {
				if (!target || target->m_capabilityRequest != request)
					return;
				if (!error.isEmpty())
				{
					target->m_capabilitiesReady = false;
					target->m_capabilityDiagnostics = {error};
					emit target->dataChanged();
					for (auto* base : target->getClips())
						static_cast<SVSClip*>(base)->synthesize();
					return;
				}
				const bool changed = !target->m_capabilitiesReady || target->m_capabilities.original != parsed.original;
				target->m_capabilities = parsed;
				svs::addHostVolume(target->m_capabilities);
				target->m_capabilitiesReady = true;
				target->m_dictionaries = dictionaries;
				target->m_capabilityDiagnostics = diagnostics;
				if (!parsed.languages.contains(target->m_language))
					target->m_language = parsed.defaultLanguage;
				emit target->dataChanged();
				if (!changed)
					return;
				for (auto* clip : target->getClips())
				{
					auto* current = static_cast<SVSClip*>(clip);
					if (current->status() == "Cancelled")
						continue;
					current->invalidate();
					current->synthesize();
				}
			},
			Qt::QueuedConnection);
	}));
}
gui::TrackView* SVSTrack::createView(gui::TrackContainerView* view)
{
	return new gui::SVSTrackView(this, view);
}
std::optional<bar_t> SVSTrack::frozenExportLength() const
{
	const auto regions = std::atomic_load(&m_exportRegions);
	if (!regions)
		return {};
	double end = 0;
	for (const auto& region : *regions)
		end = std::max(end, region.end);
	return bar_t(end / TimePos::ticksPerBar());
}
void SVSTrack::clearNoteActivity()
{
	lock();
	for (int i = 0; i < m_activeNotes.size(); ++i)
	{
		emit noteEnded();
	}
	m_activeNotes.clear();
	m_lastActivityTick = -1;
	unlock();
}
bool SVSTrack::play(const TimePos& start, f_cnt_t frames, f_cnt_t offset, int clipNum)
{
	if (isMuted() || !tryLock()) return false;
	bool played = false;
	QSet<QPair<quintptr, int>> activeNotes;
	double playTick = int(start);
	if (m_lastActivityTick >= 0 && (int(start) < m_lastActivityTick || int(start) > m_lastActivityTick + 1))
	{
		for (int i = 0; i < m_activeNotes.size(); ++i)
		{
			emit noteEnded();
		}
		m_activeNotes.clear();
	}
	m_lastActivityTick = int(start);
	auto playRegion = [&](std::shared_ptr<const svs::Audio> audio, std::shared_ptr<const svs::VolumeAutomation> volume,
						  double position, double end, double contentOffset) {
		if (!audio || playTick >= end) return false;
		const auto& mapping = audio->mapping;
		const double begin = contentOffset == 0 ? std::min(position, mapping.projectTick(audio->startTick)) : position;
		if (playTick < begin) return false;
		double localTick = mapping.localTick(playTick);
		const double remainder = Engine::getSong()->getTimeline().frameOffset();
		double sampleStart = mapping.samplePosition(localTick, audio->startTick, audio->rate)
			+ remainder * audio->rate / Engine::audioEngine()->outputSampleRate();
		auto bounded = f_cnt_t(std::ceil(std::max(
			0., std::min(double(Engine::framesPerTick()), (end - playTick) * Engine::framesPerTick()) - remainder)));
		return Engine::audioEngine()->addPlayHandle(
			new SVSPlaybackHandle(this, std::move(audio), std::move(volume), sampleStart, bounded, offset));
	};
	const auto frozen = Engine::getSong()->isExporting() ? std::atomic_load(&m_exportRegions) : nullptr;
	if (frozen)
	{
		for (const auto& region : *frozen)
			played
				= playRegion(region.audio, region.volume, region.position, region.end, region.contentOffset) || played;
	}
	else
	{
		for (auto* base : getClips())
		{
			auto* clip = static_cast<SVSClip*>(base);
			if (clipNum >= 0 && getClipNum(clip) != clipNum) { continue; }
			playTick = int(start) + (clipNum >= 0 ? int(clip->startPosition()) + int(clip->startTimeOffset()) : 0);
			const double tick = playTick + Engine::getSong()->getTimeline().frameOffset() / Engine::framesPerTick();
			if (!clip->isMuted() && tick >= int(clip->startPosition()) && tick < int(clip->endPosition()))
			{
				svs::TimeMapping mapping;
				mapping.position = int(clip->startPosition());
				mapping.contentOffset = -int(clip->startTimeOffset());
				const auto local = mapping.localTick(tick);
				for (int i = 0; i < clip->notes().size(); ++i)
				{
					const auto& note = clip->notes()[i];
					if (local >= note.tick && local < note.tick + note.duration)
					{
						activeNotes.insert({reinterpret_cast<quintptr>(clip), i});
					}
				}
			}
			if (!clip->isMuted())
				played = playRegion(clip->audio(), clip->volumeAutomation(), int(clip->startPosition()),
							 int(clip->endPosition()), -int(clip->startTimeOffset()))
					|| played;
		}
	}
	for (const auto& note : m_activeNotes)
	{
		if (!activeNotes.contains(note)) { emit noteEnded(); }
	}
	for (const auto& note : activeNotes)
	{
		if (!m_activeNotes.contains(note)) { emit noteStarted(); }
	}
	m_activeNotes = std::move(activeNotes);
	unlock();
	return played;
}
void SVSTrack::setPortraitSettings(const QJsonObject& input)
{
	if (readOnly())
		return;
	auto settings = input;
	settings["visible"] = input["visible"].toBool(true);
	settings["transparency"] = std::clamp(input["transparency"].toInt(70), 0, 100);
	settings["x"] = std::clamp(input["x"].toDouble(1), 0., 1.);
	settings["y"] = std::clamp(input["y"].toDouble(1), 0., 1.);
	if (settings == m_portraitSettings)
		return;
	addJournalCheckPoint();
	m_portraitSettings = settings;
	emit dataChanged();
	Engine::getSong()->setModified();
}
void SVSTrack::saveTrackSpecificSettings(QDomDocument& doc, QDomElement& node, bool)
{
	if (readOnly())
	{
		svs::copyXml(doc, node, m_original);
		return;
	}
	if (!m_original.isNull())
		svs::applyXmlExtras(doc, node, svs::xmlExtras(m_original, {}, {m_bus.effects()->nodeName()}));
	node.setAttribute("schemaVersion", 1);
	node.setAttribute("pluginId", m_pluginId);
	node.setAttribute("voiceId", m_voiceId);
	if (!m_voice.version.isEmpty())
		node.setAttribute("voiceVersion", m_voice.version);
	if (!m_voice.metadata["pluginVersion"].toString().isEmpty())
		node.setAttribute("pluginVersion", m_voice.metadata["pluginVersion"].toString());
	node.setAttribute("nameMode", m_customName ? "custom" : "followVoice");
	m_volume.saveSettings(doc, node, "vol");
	m_pan.saveSettings(doc, node, "pan");
	m_mix.saveSettings(doc, node, "mixch");
	m_bus.effects()->saveState(doc, node);
	node.setAttribute("language", m_language);
	node.setAttribute("parameters", QString::fromUtf8(QJsonDocument(m_parameters).toJson(QJsonDocument::Compact)));
	node.setAttribute(
		"portraitSettings", QString::fromUtf8(QJsonDocument(m_portraitSettings).toJson(QJsonDocument::Compact)));
}
void SVSTrack::loadTrackSpecificSettings(const QDomElement& node)
{
	m_original = node.cloneNode(true).toElement();
	m_migrationDiagnostic.clear();
	svs::supportedXmlSchema(node, m_migrationDiagnostic);
	svs::jsonObjectAttribute(node, "parameters", m_parameters, m_migrationDiagnostic);
	QJsonObject portrait;
	if (node.hasAttribute("portraitSettings"))
		svs::jsonObjectAttribute(node, "portraitSettings", portrait, m_migrationDiagnostic);
	m_language = node.attribute("language");
	m_customName = node.attribute("nameMode") == "custom";
	if (readOnly())
	{
		++m_capabilityRequest;
		m_pluginId = node.attribute("pluginId");
		m_voiceId = node.attribute("voiceId");
		m_voice = {};
		m_capabilities = {};
		svs::addHostVolume(m_capabilities);
		m_capabilitiesReady = false;
		m_dictionaries.clear();
		m_capabilityDiagnostics = {m_migrationDiagnostic};
		for (auto* base : getClips())
			static_cast<SVSClip*>(base)->invalidate();
		emit dataChanged();
		return;
	}
	if (node.hasAttribute("portraitSettings"))
		setPortraitSettings(portrait);
	bindVoice(node.attribute("pluginId"), node.attribute("voiceId"));
	if (m_voice.version.isEmpty())
		m_voice.version = node.attribute("voiceVersion");
	if (!m_voice.metadata.contains("pluginVersion"))
		m_voice.metadata["pluginVersion"] = node.attribute("pluginVersion");
	m_volume.loadSettings(node, "vol");
	m_pan.loadSettings(node, "pan");
	m_mix.loadSettings(node, "mixch");
	m_bus.effects()->clear();
	auto effects = node.firstChildElement(m_bus.effects()->nodeName());
	if (!effects.isNull())
		m_bus.effects()->restoreState(effects);
}
}
