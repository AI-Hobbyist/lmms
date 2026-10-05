#ifndef LMMS_SVS_CLIP_H
#define LMMS_SVS_CLIP_H
#include "Clip.h"
#include "SVSModel.h"
#include <atomic>
#include <QPointer>
#include <QDomElement>
namespace lmms {
class SVSClip : public Clip {
 Q_OBJECT
public:
 explicit SVSClip(Track*);
 ~SVSClip() override;
 QString nodeName() const override { return "svsclip"; }
 gui::ClipView* createView(gui::TrackView*) override;
 Clip* clone() override;
 void saveSettings(QDomDocument&,QDomElement&) override;
 void loadSettings(const QDomElement&) override;
 void setNotes(const QVector<svs::Note>&);
 const QVector<svs::Note>& notes() const { return m_notes; }
 void synthesize();
 void invalidate();
 std::shared_ptr<const svs::Audio> audio() const;
 QString status() const { return m_status; }
 QString id() const { return m_id; }
private:
 QString m_id,m_status="Dirty";
 QVector<svs::Note> m_notes;
 QDomElement m_original;
 std::atomic<uint64_t> m_revision{1};
 uint64_t m_generation=1,m_request=0;
 std::shared_ptr<const svs::Audio> m_audio;
};
}
#endif
