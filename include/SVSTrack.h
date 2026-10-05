#ifndef LMMS_SVS_TRACK_H
#define LMMS_SVS_TRACK_H
#include "Track.h"
#include "AudioBusHandle.h"
#include "SVSModel.h"
#include "SVSCapabilities.h"
#include <QDomElement>
#include <optional>
namespace lmms {
class SVSTrack : public Track {
 Q_OBJECT
public:
 explicit SVSTrack(TrackContainer*);
 ~SVSTrack() override;
 QString nodeName() const override { return "svstrack"; }
 bool play(const TimePos&,f_cnt_t,f_cnt_t,int=-1) override;
 void setExportRegions(std::shared_ptr<const QVector<svs::ExportAudioRegion>> regions) {std::atomic_store(&m_exportRegions,std::move(regions));}
 std::optional<bar_t> frozenExportLength() const;
 gui::TrackView* createView(gui::TrackContainerView*) override;
 Clip* createClip(const TimePos&) override;
 void saveTrackSpecificSettings(QDomDocument&,QDomElement&,bool) override;
 void loadTrackSpecificSettings(const QDomElement&) override;
 void setName(const QString&) override;
 void bindVoice(const QString&,const QString&);
 void restoreVoiceName();
 const svs::Voice& voice() const { return m_voice; }
 QString avatarPath() const { return m_portraitSettings.value("avatarPath").toString(m_voice.avatar); }
 QString portraitPath() const { return m_portraitSettings.value("portraitPath").toString(m_voice.portrait); }
 FloatModel* volumeModel() { return &m_volume; }
 FloatModel* panningModel() { return &m_pan; }
 IntModel* mixerChannelModel() { return &m_mix; }
 AudioBusHandle* audioBusHandle() { return &m_bus; }
 QString pluginId() const { return m_pluginId; }
 QString voiceId() const { return m_voiceId; }
 const svs::Capabilities& capabilities() const { return m_capabilities; }
 bool capabilitiesReady() const {return m_capabilitiesReady;}
 const QVector<svs::Dictionary>& dictionaries() const { return m_dictionaries; }
 const QJsonObject& parameters() const { return m_parameters; }
 QString language() const { return m_language; }
 QStringList capabilityDiagnostics() const { return m_capabilityDiagnostics; }
 bool setParameter(const QString&, const QJsonValue&);
 bool setLanguage(const QString&);
 void refreshCapabilities(const QJsonObject& context = {});
 const QJsonObject& portraitSettings() const { return m_portraitSettings; }
 void setPortraitSettings(const QJsonObject&);
 bool readOnly() const {return !m_migrationDiagnostic.isEmpty();}
 QString migrationDiagnostic() const {return m_migrationDiagnostic;}
private:
 std::shared_ptr<const QVector<svs::ExportAudioRegion>> m_exportRegions;
 QString m_migrationDiagnostic;
 FloatModel m_volume,m_pan;
 IntModel m_mix;
 AudioBusHandle m_bus;
 QString m_pluginId,m_voiceId;
 bool m_customName=false;
 svs::Voice m_voice;
 svs::Capabilities m_capabilities;
 bool m_capabilitiesReady=false;
 QVector<svs::Dictionary> m_dictionaries;
 QJsonObject m_parameters;
 QJsonObject m_portraitSettings;
 QString m_language;
 QStringList m_capabilityDiagnostics;
 uint64_t m_capabilityRequest=0;
 QDomElement m_original;
};
}
#endif
