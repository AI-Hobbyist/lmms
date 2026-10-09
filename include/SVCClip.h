#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <memory>

#include "Clip.h"
#include "SVCPlayback.h"

namespace lmms {
class SVCClip : public Clip
{
	Q_OBJECT
public:
	explicit SVCClip(Track* track);
	~SVCClip() override;
	QString nodeName() const override { return "svcclip"; }
	gui::ClipView* createView(gui::TrackView* view) override;
	Clip* clone() override;
	void saveSettings(QDomDocument& document, QDomElement& element) override;
	void loadSettings(const QDomElement& element) override;
	bool setSourceFile(const QString& file);
	QString sourceFile() const { return m_sourceFile; }
	std::shared_ptr<svc::PlaybackState> playback() const { return m_playback; }
	QString status() const { return m_status; }
	bool conversionComplete() const { return m_complete; }
	bool conversionFailed() const { return m_failed; }
	void setStatus(const QString& status);
	void invalidate();
	uint64_t beginConversion(const std::vector<svc::Segment>& segments);
	bool publish(const svc_event& event);
	void publishStatus(const svc_event& event);
	bool finishSegment(uint64_t generation, uint64_t segment, svc_status terminal, const QJsonObject& cache = {},
		bool alreadyPublished = false);
	void setStartTimeOffset(const TimePos& offset) override;
	void changeLength(const TimePos& length) override;
	void updateLength() override;
	const QJsonArray& cacheReferences() const { return m_cacheReferences; }

private:
	bool restoreCaches();
	QString m_sourceFile;
	QString m_sourceDigest;
	QString m_status = "Import audio";
	std::shared_ptr<svc::PlaybackState> m_playback;
	QJsonArray m_cacheReferences;
	std::vector<svc::Segment> m_activeSegments;
	bool m_loading = false;
	bool m_complete = false;
	bool m_failed = false;
};
} // namespace lmms
