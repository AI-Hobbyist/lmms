#include "SVSExportSnapshot.h"
#include "SVSClip.h"
#include "SVSTrack.h"
#include "SVSSynthesisScheduler.h"
#include "SVSTempoSource.h"
#include "Song.h"
#include "EffectChain.h"
#include <QTimer>
#include <QCoreApplication>
#include <QRunnable>
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QThread>
#include "Engine.h"
namespace lmms::svs {
namespace {
QString mixContext(SVSTrack& track)
{
	QDomDocument document;
	auto root = document.createElement("effects");
	document.appendChild(root);
	track.audioBusHandle()->effects()->saveState(document, root);
	return QString::number(track.volumeModel()->value(), 'g', 9) + "/"
		+ QString::number(track.panningModel()->value(), 'g', 9) + "/"
		+ QString::number(track.mixerChannelModel()->value()) + "/" + QString::number(track.isMuted()) + "/"
		+ document.toString(-1);
}
}
QVector<ExportSnapshot::Region> ExportSnapshot::capture(Song& song, uint32_t rate)
{
	// Resolve any coalesced changes before capturing versions for all regions.
	TempoSource::forSong(song).snapshot();
	QVector<Region> result;
	for (auto* base : song.tracks())
		if (base->type() == Track::Type::SVS && !base->isMuted())
		{
			auto* track = static_cast<SVSTrack*>(base);
			for (auto* item : track->getClips())
			{
				auto* clip = static_cast<SVSClip*>(item);
				if (clip->isMuted())
					continue;
				Region region;
				region.track = track;
				region.trackName = track->name();
				region.clipName = clip->name();
				region.position = int(clip->startPosition());
				region.length = int(clip->length());
				region.contentOffset = -int(clip->startTimeOffset());
				region.input = clip->captureInput(rate);
				region.plugin = Registry::instance().plugin(track->pluginId());
				region.mixContext = mixContext(*track);
				region.catalogPending
					= region.plugin && track->voice().id.isEmpty() && Registry::instance().scanning(track->pluginId());
				if (region.catalogPending)
					clip->captureCachedInput(region.input);
				if (track->voice().id.isEmpty() && !region.catalogPending)
					region.plugin.reset();
				if (clip->notes().isEmpty())
				{
				}
				else if (clip->readOnly())
					region.diagnostic = clip->migrationDiagnostic();
				else if (!region.plugin && !clip->captureCachedInput(region.input))
					region.diagnostic = "Missing voice/plugin";
				else if (region.plugin && !track->capabilitiesReady())
				{
					region.declarationPending = true;
					region.voicePackage = track->voice().package;
				}
				result.push_back(std::move(region));
			}
		}
	return result;
}
ExportSnapshot::ExportSnapshot(QVector<Region> regions, QObject* parent)
	: QObject(parent)
	, m_regions(std::move(regions))
{
	connect(&TempoSource::forSong(*Engine::getSong()), &TempoSource::changed, this,
		[this] { invalidateActive(tr("Project tempo changed while rendering SVS export; restart export")); });
	for (int index = 0; index < m_regions.size(); ++index)
		if (m_regions[index].track)
		{
			auto* track = m_regions[index].track.data();
			auto changed = [this, index] {
				// Automation values are applied by the render thread. Owner-thread model
				// writes are editor/API changes to the live mixer, outside the frozen input.
				if (QThread::currentThread() != thread())
					return;
				invalidateActive(
					locate(index, tr("SVS mix controls or routing changed while rendering; restart export")));
			};
			connect(track->volumeModel(), &FloatModel::dataChanged, this, changed, Qt::DirectConnection);
			connect(track->panningModel(), &FloatModel::dataChanged, this, changed, Qt::DirectConnection);
			connect(track->mixerChannelModel(), &IntModel::dataChanged, this, changed, Qt::DirectConnection);
			connect(track->getMutedModel(), &BoolModel::dataChanged, this, changed, Qt::DirectConnection);
			connect(track->audioBusHandle()->effects(), &EffectChain::dataChanged, this, changed, Qt::DirectConnection);
			connect(
				track->audioBusHandle()->effects(), &EffectChain::aboutToClear, this, changed, Qt::DirectConnection);
			connect(m_regions[index].track, &QObject::destroyed, this, [this, index] {
				// Frozen PCM cannot reach the mixer after its owning track disappears.
				// Treat this as an export lifecycle failure, even when synthesis failures
				// were explicitly allowed to become silence.
				if (m_state == State::Ready && !m_activeTracks.isEmpty())
				{
					invalidateActive(locate(index, tr("Track deleted while rendering SVS export")));
					return;
				}
				if (m_state != State::Captured && m_state != State::Preparing && m_state != State::Ready)
					return;
				m_diagnostics << locate(index, tr("Track deleted while preparing SVS export"));
				for (const auto& control : m_controls)
					control->cancel();
				finish(State::Failed);
			});
		}
}
void ExportSnapshot::invalidateActive(const QString& reason)
{
	if (m_state != State::Ready || m_activeTracks.isEmpty() || !Engine::getSong()->isExporting())
		return;
	m_diagnostics << reason;
	m_state = State::Failed;
	for (const auto& control : m_controls)
		control->cancel();
	const auto notify = invalidated;
	if (notify)
		notify(reason);
}
ExportSnapshot::~ExportSnapshot()
{
	for (const auto& control : m_controls)
		control->cancel();
	deactivate();
}
bool ExportSnapshot::activate(Song& song)
{
	if (m_state != State::Ready)
		return false;
	const auto tempo = TempoSource::forSong(song).snapshot();
	// PCM is frozen independently of the live editor. The existing LMMS mixer
	// and transport still use the live project, so reject a changed context
	// before starting them rather than silently combining two project versions.
	for (int index = 0; index < m_regions.size(); ++index)
	{
		const auto& region = m_regions[index];
		QString reason;
		if (!region.track)
			reason = tr("Track deleted while preparing SVS export");
		else if (region.mixContext != mixContext(*region.track))
			reason = tr("SVS mix controls, routing or effects changed during export preparation; restart export");
		else if (region.input.tempoSnapshot && region.input.tempoSnapshot->toJson() != tempo->toJson())
			reason = tr("Project tempo changed during SVS export preparation; restart export");
		if (!reason.isEmpty())
		{
			m_diagnostics << locate(index, reason);
			finish(State::Failed);
			return false;
		}
	}
	freezeTimeline(song);
	return true;
}
void ExportSnapshot::freezeTimeline(Song& song)
{
	deactivate();
	for (auto* base : song.tracks())
		if (base->type() == Track::Type::SVS)
		{
			auto* track = static_cast<SVSTrack*>(base);
			auto clips = std::make_shared<QVector<ExportAudioRegion>>();
			for (const auto& region : m_regions)
				if (region.track == track)
					clips->push_back(
						{region.position, region.position + region.length, region.contentOffset, region.audio});
			track->setExportRegions(std::move(clips));
			m_activeTracks.push_back(track);
		}
}
void ExportSnapshot::deactivate()
{
	for (const auto& track : m_activeTracks)
		if (track)
			track->setExportRegions({});
	m_activeTracks.clear();
}
QString ExportSnapshot::locate(int index, const QString& reason) const
{
	const auto& region = m_regions[index];
	return tr("SVS track '%1', clip '%2' (%3), tick %4: %5")
		.arg(region.trackName, region.clipName, region.input.clipId)
		.arg(region.position)
		.arg(reason);
}
void ExportSnapshot::finish(State state)
{
	m_state = state;
	if (m_completionScheduled)
		return;
	m_completionScheduled = true;
	QTimer::singleShot(0, this, [this] {
		if (completed)
			completed();
	});
}
void ExportSnapshot::prepare(bool ignore)
{
	if (m_state != State::Captured)
		return;
	m_state = State::Preparing;
	m_ignore = ignore;
	m_remaining = m_regions.size();
	for (int index = 0; index < m_regions.size(); ++index)
		if (!m_regions[index].track)
		{
			m_diagnostics << locate(index, tr("Track deleted before preparing SVS export"));
			finish(State::Failed);
			return;
		}
	// Start on the event loop so a caller can always attach completion first.
	QTimer::singleShot(0, this, [this] {
		if (m_state != State::Preparing)
			return;
		if (m_regions.isEmpty())
		{
			finish(State::Ready);
			return;
		}
		for (int index = 0; index < m_regions.size() && m_state == State::Preparing; ++index)
		{
			const auto& region = m_regions[index];
			if (region.input.notes.isEmpty())
			{
				if (--m_remaining == 0)
					finish(State::Ready);
				continue;
			}
			if (region.catalogPending && region.diagnostic.isEmpty())
			{
				awaitCatalog(index);
				continue;
			}
			if (!region.diagnostic.isEmpty() || (!region.plugin && !region.input.document.contains("cacheOnlyKey")))
			{
				receive(index, {}, region.diagnostic.isEmpty() ? "Missing voice/plugin" : region.diagnostic);
				continue;
			}
			if (region.declarationPending)
				declare(index);
			else
				submit(index, region.input);
		}
	});
}
void ExportSnapshot::awaitCatalog(int index)
{
	if (m_state != State::Preparing)
		return;
	auto& region = m_regions[index];
	const auto id = region.input.document["pluginId"].toString();
	if (Registry::instance().scanning(id))
	{
		QTimer::singleShot(20, this, [this, index] { awaitCatalog(index); });
		return;
	}
	region.catalogPending = false;
	for (const auto& voice : Registry::instance().voices())
		if (voice.pluginId == id && voice.id == region.input.voiceId)
		{
			region.voicePackage = voice.package;
			region.input.document.remove("cacheOnlyKey");
			region.input.document["voiceVersion"] = voice.version;
			region.input.document["pluginVersion"] = voice.metadata["pluginVersion"];
			declare(index);
			return;
		}
	region.plugin.reset();
	if (region.input.document.contains("cacheOnlyKey"))
		submit(index, region.input);
	else
		receive(index, {}, "Missing voice/plugin");
}
void ExportSnapshot::submit(int index, Input input)
{
	if (m_state != State::Preparing)
		return;
	m_regions[index].input = input;
	QPointer<ExportSnapshot> target(this);
	m_controls.push_back(SynthesisScheduler::instance().submit(
		m_regions[index].plugin, std::move(input), 100, [](const QString&) {},
		[target, index](auto audio, const auto& error) {
			if (target)
				target->receive(index, std::move(audio), error);
		}));
}
void ExportSnapshot::declare(int index)
{
	const auto& region = m_regions[index];
	QPointer<ExportSnapshot> target(this);
	SynthesisScheduler::instance().declarationPool().start(QRunnable::create(
		[input = region.input, plugin = region.plugin, package = region.voicePackage, target, index]() mutable {
			auto context = input.document;
			const auto parameters = context["trackParameters"].toObject();
			for (auto i = parameters.begin(); i != parameters.end(); ++i)
				context[i.key()] = i.value();
			context["noteCount"] = input.notes.size();
			QJsonArray notes;
			for (const auto& note : input.notes)
				notes.append(QJsonObject{{"id", note.id}, {"tick", note.tick}, {"duration", note.duration},
					{"pitch", note.pitch}, {"lyric", note.lyric}, {"language", note.language},
					{"pronunciation", note.pronunciation}, {"parameters", note.parameters},
					{"phonemes", note.phonemes}});
			context["notes"] = notes;
			QString error;
			const auto schema = plugin->capabilities(input.voiceId, context, error);
			Capabilities capabilities;
			if (error.isEmpty())
				Capabilities::parse(schema, capabilities, error);
			if (error.isEmpty())
			{
				input.document["capabilities"] = schema;
				if (!capabilities.languages.contains(input.document["language"].toString()))
					input.document["language"] = capabilities.defaultLanguage;
				QJsonArray dictionaries;
				const auto root = QFileInfo(package).canonicalFilePath() + "/";
				for (const auto& resource : capabilities.dictionaryResources)
				{
					const auto path = QFileInfo(QDir(package).filePath(resource)).canonicalFilePath();
					QFile file(path);
					Dictionary dictionary;
					if (!path.startsWith(root, Qt::CaseInsensitive) || !file.open(QIODevice::ReadOnly))
					{
						error = "Missing voice dictionary: " + resource;
						break;
					}
					if (!Dictionary::parse(file.read(4 * 1024 * 1024 + 1), capabilities.phonemeSet, dictionary, error))
						break;
					if (dictionary.phonemeSet != capabilities.phonemeSetId
						|| !capabilities.languages.contains(dictionary.language))
					{
						error = "Incompatible voice dictionary: " + resource;
						break;
					}
					dictionaries.append(QJsonObject{{"id", dictionary.id}, {"version", dictionary.version},
						{"hash", dictionary.hash}, {"language", dictionary.language},
						{"phonemeSet", dictionary.phonemeSet}, {"entries", dictionary.entries}});
				}
				input.document["voiceDictionaries"] = dictionaries;
			}
			QMetaObject::invokeMethod(
				QCoreApplication::instance(),
				[target, index, input = std::move(input), error] {
					if (!target || target->m_state != State::Preparing)
						return;
					if (!error.isEmpty())
						target->receive(index, {}, error);
					else
						target->submit(index, input);
				},
				Qt::QueuedConnection);
		}));
}
void ExportSnapshot::receive(int index, std::shared_ptr<const Audio> audio, const QString& error)
{
	if (m_state != State::Preparing)
		return;
	auto& region = m_regions[index];
	if (audio && (!audio->complete || audio->revision != region.input.revision))
	{
		audio.reset();
		region.diagnostic = "SVS export result incomplete or version mismatch";
	}
	if (!audio)
	{
		if (region.diagnostic.isEmpty())
			region.diagnostic = error.isEmpty() ? "SVS synthesis failed" : error;
		m_diagnostics << locate(index, region.diagnostic);
		if (!m_ignore)
		{
			for (const auto& control : m_controls)
				control->cancel();
			finish(State::Failed);
			return;
		}
	}
	region.audio = std::move(audio);
	if (--m_remaining == 0)
		finish(State::Ready);
}
void ExportSnapshot::cancel()
{
	if (m_state == State::Failed || m_state == State::Cancelled)
		return;
	for (const auto& control : m_controls)
		control->cancel();
	finish(State::Cancelled);
}
}
