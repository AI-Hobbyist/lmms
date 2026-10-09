#pragma once

#include <QJsonObject>

#include "AudioBusHandle.h"
#include "SVCChunking.h"
#include "Track.h"

namespace lmms {
class SVCTrack : public Track
{
	Q_OBJECT
public:
	explicit SVCTrack(TrackContainer* container);
	~SVCTrack() override;
	QString nodeName() const override { return "svctrack"; }
	bool play(const TimePos& start, f_cnt_t frames, f_cnt_t offset, int clipNumber = -1) override;
	gui::TrackView* createView(gui::TrackContainerView* container) override;
	Clip* createClip(const TimePos& position) override;
	void saveTrackSpecificSettings(QDomDocument& document, QDomElement& element, bool preset) override;
	void loadTrackSpecificSettings(const QDomElement& element) override;
	void setName(const QString& name) override;
	FloatModel* volumeModel() { return &m_volume; }
	FloatModel* panningModel() { return &m_pan; }
	IntModel* mixerChannelModel() { return &m_mix; }
	AudioBusHandle* audioBusHandle() { return &m_bus; }
	const QJsonObject& selection() const { return m_selection; }
	bool setSelection(const QJsonObject& selection);
	const svc::ChunkConfig& chunkConfig() const { return m_chunks; }
	bool setChunkConfig(const svc::ChunkConfig& config);
	void invalidateClips();

signals:
	void playbackActivity();

	void renderRequested();

private:
	FloatModel m_volume;
	FloatModel m_pan;
	IntModel m_mix;
	AudioBusHandle m_bus;
	QJsonObject m_selection;
	svc::ChunkConfig m_chunks;
};
} // namespace lmms
