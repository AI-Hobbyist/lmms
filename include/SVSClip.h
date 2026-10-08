#ifndef LMMS_SVS_CLIP_H
#define LMMS_SVS_CLIP_H
#include "Clip.h"
#include "SVSModel.h"
#include "SVSCurve.h"
#include "SVSSegmentedSynthesis.h"
#include <atomic>
#include <QPointer>
#include <QDomElement>
#include <QJsonArray>
namespace lmms {
class SVSClip : public Clip {
 Q_OBJECT
public:
 explicit SVSClip(Track*);
 ~SVSClip() override;
 QString nodeName() const override { return "svsclip"; }
 gui::ClipView* createView(gui::TrackView*) override;
 Clip* clone() override;
 SVSClip* splitAt(const TimePos& projectTick);
 void setStartTimeOffset(const TimePos&) override;
 void movePosition(const TimePos&) override;
 void changeLength(const TimePos&) override;
 bool readOnly() const;
 QString migrationDiagnostic() const;
 void saveSettings(QDomDocument&,QDomElement&) override;
 void loadSettings(const QDomElement&) override;
 void setNotes(const QVector<svs::Note>&);
 const QVector<svs::Note>& notes() const { return m_notes; }
 const svs::Curves& curves() const { return m_curves; }
 void setEditorData(const QVector<svs::Note>&,const svs::Curves&);
 const QJsonObject& editorState() const { return m_editorState; }
 void setEditorState(const QJsonObject&);
 void synthesize();
 svs::Input captureInput(uint32_t sampleRate) const;
 bool captureCachedInput(svs::Input&) const;
 void invalidate();
 void cancelSynthesis();
 std::shared_ptr<const svs::Audio> audio() const;
 QString status() const { return m_status; }
 QString id() const { return m_id; }
 const QJsonObject& parameters() const { return m_parameters; }
 bool setParameter(const QString&,const QJsonValue&);
 const QJsonObject& globalParameters() const {return m_globalParameters;}
 QJsonValue parameterBase(const svs::Parameter&) const;
 bool setGlobalParameter(const QString&,const QJsonValue&);
 bool setNoteParameter(const QStringList& noteIds,const QString& id,const QJsonValue& value,bool phoneme=false);
 bool importDictionary(const QByteArray&,QString& error);
 const QJsonArray& projectDictionaryData() const { return m_projectDictionaryData; }
private:
 void scheduleSynthesis();
 bool m_synthesisScheduled=false;
 bool m_loading=false;
 QString m_migrationDiagnostic;
 QJsonObject m_notesXmlExtras,m_unparsedCurves;
 QString m_id,m_status="Dirty";
 QString m_cacheKey,m_cacheInputHash;
 uint32_t m_cacheRate=0;
 QVector<svs::Note> m_notes;
 svs::Curves m_curves;
 QJsonObject m_editorState;
 QJsonObject m_parameters,m_globalParameters;
 QJsonArray m_projectDictionaryData;
 QDomElement m_original;
 std::atomic<uint64_t> m_revision{1};
 uint64_t m_generation=1,m_request=0;
 std::shared_ptr<const svs::Audio> m_audio;
 std::shared_ptr<svs::RenderControl> m_renderControl;
 QVector<svs::SynthesisSegment> m_segments;
};
}
#endif
