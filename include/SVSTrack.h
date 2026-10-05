#ifndef LMMS_SVS_TRACK_H
#define LMMS_SVS_TRACK_H
#include "Track.h"
#include "AudioBusHandle.h"
#include "SVSModel.h"
#include <QDomElement>
namespace lmms {
class SVSTrack : public Track {
 Q_OBJECT
public:
 explicit SVSTrack(TrackContainer*);
 ~SVSTrack() override;
 QString nodeName() const override { return "svstrack"; }
 bool play(const TimePos&,f_cnt_t,f_cnt_t,int=-1) override;
 gui::TrackView* createView(gui::TrackContainerView*) override;
 Clip* createClip(const TimePos&) override;
 void saveTrackSpecificSettings(QDomDocument&,QDomElement&,bool) override;
 void loadTrackSpecificSettings(const QDomElement&) override;
 void setName(const QString&) override;
 void bindVoice(const QString&,const QString&);
 void restoreVoiceName();
 const svs::Voice& voice() const { return m_voice; }
 FloatModel* volumeModel() { return &m_volume; }
 FloatModel* panningModel() { return &m_pan; }
 IntModel* mixerChannelModel() { return &m_mix; }
 AudioBusHandle* audioBusHandle() { return &m_bus; }
 QString pluginId() const { return m_pluginId; }
 QString voiceId() const { return m_voiceId; }
private:
 FloatModel m_volume,m_pan;
 IntModel m_mix;
 AudioBusHandle m_bus;
 QString m_pluginId,m_voiceId;
 bool m_customName=false;
 svs::Voice m_voice;
 QDomElement m_original;
};
}
#endif
