#include "SVCClip.h"

#include <QCryptographicHash>
#include <QDir>
#include <QDomElement>
#include <QFileInfo>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QtEndian>
#include <cmath>

#include "ConfigManager.h"
#include "SVCViews.h"
#include "SampleDecoder.h"
#include "Song.h"

namespace lmms {
namespace {
QString fileDigest(const QString& path)
{
	QFile file(path);
	QCryptographicHash hash(QCryptographicHash::Sha256);
	return file.open(QIODevice::ReadOnly) && hash.addData(&file) ? QString::fromLatin1(hash.result().toHex())
																 : QString();
}

QJsonObject segmentJson(const svc::Segment& segment)
{
	return {{"start", QString::number(segment.start)}, {"end", QString::number(segment.end)},
		{"inputStart", QString::number(segment.inputStart)}, {"inputEnd", QString::number(segment.inputEnd)},
		{"padding", QString::number(segment.padding)}, {"sampleRate", static_cast<int>(segment.sampleRate)},
		{"algorithmVersion", static_cast<int>(segment.algorithmVersion)}};
}

svc::Segment segmentFromJson(const QJsonObject& object)
{
	return {object.value("start").toString().toULongLong(), object.value("end").toString().toULongLong(),
		object.value("inputStart").toString().toULongLong(), object.value("inputEnd").toString().toULongLong(),
		object.value("padding").toString().toULongLong(), static_cast<uint32_t>(object.value("sampleRate").toInt()),
		static_cast<unsigned>(object.value("algorithmVersion").toInt())};
}
} // namespace

SVCClip::SVCClip(Track* track)
	: Clip(track)
{
	Clip::changeLength(TimePos::ticksPerBar());
	setName(tr("SVC audio"));
	connect(Engine::getSong(), &Song::tempoChanged, this, &SVCClip::updateLength);
}

SVCClip::~SVCClip()
{
	if (m_playback) { m_playback->invalidate(); }
}

void SVCClip::setStatus(const QString& status)
{
	m_status = status;
	emit dataChanged();
}

bool SVCClip::setSourceFile(const QString& file)
{
	const auto buffer = SampleDecoder::decode(file);
	if (!buffer || buffer->data.empty())
	{
		setStatus(tr("Audio import failed"));
		return false;
	}
	const auto digest = fileDigest(file);
	if (digest.isEmpty())
	{
		setStatus(tr("Cannot read source audio"));
		return false;
	}
	auto source = std::make_shared<svc::SourceAudio>();
	source->rate = buffer->sampleRate;
	source->stereo.reserve(buffer->data.size() * 2);
	for (const auto& frame : buffer->data)
	{
		source->stereo.push_back(frame[0]);
		source->stereo.push_back(frame[1]);
	}
	if (!m_loading) { addJournalCheckPoint(); }
	if (m_playback) { m_playback->invalidate(); }
	m_playback = std::make_shared<svc::PlaybackState>(source);
	m_sourceFile = QFileInfo(file).absoluteFilePath();
	m_sourceDigest = digest;
	m_cacheReferences = {};
	m_complete = false;
	m_failed = false;
	m_activeSegments.clear();
	if (!m_loading)
	{
		setStartTimeOffset(0);
		setAutoResize(true);
		updateLength();
		Engine::getSong()->setModified();
	}
	setStatus(tr("Needs re-render"));
	return true;
}

void SVCClip::updateLength()
{
	if (m_playback && getAutoResize())
	{
		const auto source = m_playback->snapshot()->source;
		Clip::changeLength(
			std::max(1, static_cast<int>(std::ceil(source->frames() / Engine::framesPerTick(source->rate)))));
	}
	emit dataChanged();
}

void SVCClip::invalidate()
{
	m_complete = false;
	m_failed = false;
	if (m_playback) { m_playback->invalidate(); }
	m_activeSegments.clear();
	setStatus(m_playback ? tr("Needs re-render") : tr("Import audio"));
}

void SVCClip::setStartTimeOffset(const TimePos& offset)
{
	if (offset != startTimeOffset() && !m_loading) { invalidate(); }
	Clip::setStartTimeOffset(offset);
}

void SVCClip::changeLength(const TimePos& length)
{
	if (length != this->length() && !m_loading) { invalidate(); }
	Clip::changeLength(length);
}

uint64_t SVCClip::beginConversion(const std::vector<svc::Segment>& segments)
{
	if (!m_playback) { return 0; }
	const auto generation = m_playback->begin(segments);
	m_complete = false;
	m_failed = false;
	m_activeSegments = segments;
	setStatus(tr("Queued"));
	return generation;
}

bool SVCClip::publish(const svc_event& event)
{
	if (!m_playback || !m_playback->publish(event)) { return false; }
	publishStatus(event);
	return true;
}

void SVCClip::publishStatus(const svc_event& event)
{
	if (!m_playback || event.generation_id != m_playback->generation()) { return; }
	setStatus(tr("Segment %1/%2, backend chunk %3/%4; replaced %5%")
			.arg(event.segment_id + 1)
			.arg(m_activeSegments.size())
			.arg(event.chunk_index)
			.arg(event.total_chunks ? QString::number(event.total_chunks) : tr("?"))
			.arg(m_playback->progress() * 100, 0, 'f', 1));
}

bool SVCClip::finishSegment(
	uint64_t generation, uint64_t segment, svc_status terminal, const QJsonObject& cache, bool alreadyPublished)
{
	if (!m_playback || generation != m_playback->generation() || segment >= m_activeSegments.size()) { return false; }
	const auto completed = alreadyPublished ? terminal == SVC_COMPLETE && m_playback->segmentComplete(segment)
											: m_playback->complete(generation, segment, terminal);
	if (completed && !cache.isEmpty())
	{
		auto entry = cache;
		entry.insert("segment", segmentJson(m_activeSegments[segment]));
		const auto range = m_activeSegments[segment];
		QJsonArray retained;
		for (const auto& reference : m_cacheReferences)
		{
			const auto old = segmentFromJson(reference.toObject().value("segment").toObject());
			if (old.start < range.start || old.end > range.end) { retained.append(reference); }
		}
		retained.append(entry);
		m_cacheReferences = retained;
	}
	m_complete = completed && m_playback->finished();
	m_failed = !completed && terminal != SVC_CANCELLED;
	setStatus(m_complete				? tr("Complete")
			: completed					? tr("Queued next segment")
			: terminal == SVC_CANCELLED ? tr("Cancelled; partial result")
										: tr("Failed; partial result"));
	Engine::getSong()->setModified();
	return completed;
}

gui::ClipView* SVCClip::createView(gui::TrackView* view)
{ return new gui::SVCClipView(this, view); }

Clip* SVCClip::clone()
{
	auto* copy = new SVCClip(getTrack());
	QDomDocument document;
	auto root = document.createElement("clone");
	saveState(document, root);
	copy->restoreState(root.firstChildElement());
	return copy;
}

void SVCClip::saveSettings(QDomDocument&, QDomElement& element)
{
	element.setAttribute("schemaVersion", 1);
	element.setAttribute("sourceFile", m_sourceFile);
	element.setAttribute("sourceDigest", m_sourceDigest);
	element.setAttribute("pos", int(startPosition()));
	element.setAttribute("len", int(length()));
	element.setAttribute("off", int(startTimeOffset()));
	element.setAttribute("name", name());
	element.setAttribute("muted", isMuted());
	element.setAttribute("autoresize", getAutoResize());
	element.setAttribute("status", m_status);
	element.setAttribute("complete", m_complete);
	element.setAttribute(
		"cacheReferences", QString::fromUtf8(QJsonDocument(m_cacheReferences).toJson(QJsonDocument::Compact)));
	if (color()) { element.setAttribute("color", color()->name()); }
}

void SVCClip::loadSettings(const QDomElement& element)
{
	m_loading = true;
	if (m_playback) { m_playback->invalidate(); }
	m_playback.reset();
	m_complete = false;
	m_sourceFile = element.attribute("sourceFile");
	m_sourceDigest.clear();
	m_cacheReferences = {};
	m_activeSegments.clear();
	const auto expectedDigest = element.attribute("sourceDigest");
	const auto references = QJsonDocument::fromJson(element.attribute("cacheReferences").toUtf8()).array();
	const auto imported = setSourceFile(element.attribute("sourceFile"));
	movePosition(element.attribute("pos").toInt());
	Clip::changeLength(std::max(1, element.attribute("len").toInt()));
	Clip::setStartTimeOffset(element.attribute("off").toInt());
	setAutoResize(element.attribute("autoresize", "1").toInt());
	setMuted(element.attribute("muted").toInt());
	setName(element.attribute("name", tr("SVC audio")));
	if (element.hasAttribute("color")) { setColor(QColor(element.attribute("color"))); }
	if (!imported) { setStatus(tr("Source missing; import audio again")); }
	else if (expectedDigest != m_sourceDigest) { setStatus(tr("Source changed; needs re-render")); }
	else
	{
		m_cacheReferences = references;
		const auto restored = !references.isEmpty() && restoreCaches();
		m_complete = restored && element.attribute("complete").toInt();
		setStatus(m_complete ? tr("Complete")
				: restored	 ? tr("Needs re-render")
							 : tr("Result missing or incomplete; original audio restored"));
	}
	m_loading = false;
	emit dataChanged();
}

bool SVCClip::restoreCaches()
{
	bool allComplete = true;
	// Rebuild each independent cached region in reference order; newer overlap
	// replaces older B. A partial file is never accepted on restart.
	for (const auto& reference : m_cacheReferences)
	{
		const auto entry = reference.toObject();
		const auto engine = entry.value("engine").toString();
		const auto hash = entry.value("hash").toString();
		if (!QRegularExpression("^[A-Za-z0-9][A-Za-z0-9_-]{0,63}$").match(engine).hasMatch()
			|| !QRegularExpression("^[a-f0-9]{64}$").match(hash).hasMatch())
		{
			allComplete = false;
			continue;
		}
		const auto path
			= QDir(ConfigManager::inst()->aiCacheDir()).filePath("svc/" + engine + "/output/" + hash + ".wav");
		QFile manifest(path + ".json");
		if (!manifest.open(QIODevice::ReadOnly))
		{
			allComplete = false;
			continue;
		}
		const auto metadata = QJsonDocument::fromJson(manifest.read(1024 * 1024)).object();
		if (metadata.value("state").toString() != "complete"
			|| metadata.value("output_digest").toString() != fileDigest(path))
		{
			allComplete = false;
			continue;
		}
		const auto segment = segmentFromJson(entry.value("segment").toObject());
		uint64_t generation;
		try
		{
			generation = m_playback->begin({segment});
		}
		catch (...)
		{
			allComplete = false;
			continue;
		}
		QFile output(path);
		if (!output.open(QIODevice::ReadOnly))
		{
			allComplete = false;
			continue;
		}
		const auto header = output.read(44);
		if (header.size() != 44 || header.left(4) != "RIFF" || header.mid(8, 4) != "WAVE"
			|| qFromLittleEndian<uint16_t>(header.constData() + 22) != 1
			|| qFromLittleEndian<uint16_t>(header.constData() + 34) != 16
			|| output.size() != 44 + qFromLittleEndian<uint32_t>(header.constData() + 40))
		{
			allComplete = false;
			continue;
		}
		svc_event event{};
		event.size = sizeof(event);
		event.type = SVC_AUDIO;
		event.generation_id = generation;
		event.request_id = "cache-restore";
		event.channels = 1;
		event.sample_rate = qFromLittleEndian<uint32_t>(header.constData() + 24);
		bool valid = true;
		while (!output.atEnd())
		{
			const auto bytes = output.read(SVC_MAX_FEED);
			++event.chunk_index;
			event.bytes = reinterpret_cast<const uint8_t*>(bytes.constData());
			event.byte_count = bytes.size();
			event.sample_count = bytes.size() / 2;
			if (!m_playback->publish(event))
			{
				valid = false;
				break;
			}
			event.sample_offset += event.sample_count;
		}
		allComplete = m_playback->complete(generation, 0, valid ? SVC_COMPLETE : SVC_FAILED) && allComplete;
	}
	return allComplete;
}
} // namespace lmms
