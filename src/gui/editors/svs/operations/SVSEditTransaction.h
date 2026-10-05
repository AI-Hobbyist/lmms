#ifndef LMMS_SVS_EDIT_TRANSACTION_H
#define LMMS_SVS_EDIT_TRANSACTION_H
#include "SVSClip.h"
#include <QPointer>

namespace lmms::gui {
// Model writes happen once, on commit. Drag previews never start synthesis.
class SVSEditTransaction {
public:
 explicit SVSEditTransaction(SVSClip* clip):m_clip(clip) {}
 void begin() { if(!m_clip) return; originalNotes=m_clip->notes(); originalCurves=m_clip->curves(); notes=originalNotes; curves=originalCurves; m_active=true; }
 void commit() { if(m_active&&m_clip) m_clip->setEditorData(notes,curves); m_active=false; }
 void cancel() { notes=originalNotes; curves=originalCurves; m_active=false; }
 bool active() const { return m_active; }
 QVector<svs::Note> originalNotes,notes;
 svs::Curves originalCurves,curves;
private:
 QPointer<SVSClip> m_clip;
 bool m_active=false;
};
}
#endif
