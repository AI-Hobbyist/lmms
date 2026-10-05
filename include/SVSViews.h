#ifndef LMMS_SVS_VIEWS_H
#define LMMS_SVS_VIEWS_H
#include "TrackView.h"
#include "ClipView.h"
#include <QDialog>
#include <QPointer>
namespace lmms { class SVSTrack; class SVSClip;
namespace gui {
class SVSTrackView : public TrackView {
 Q_OBJECT
public:
 SVSTrackView(SVSTrack*,TrackContainerView*);
protected:
 void dragEnterEvent(QDragEnterEvent*) override;
 void dropEvent(QDropEvent*) override;
};
class SVSClipView : public ClipView {
 Q_OBJECT
public:
 SVSClipView(SVSClip*,TrackView*);
protected:
 void paintEvent(QPaintEvent*) override;
 void mouseDoubleClickEvent(QMouseEvent*) override;
private: SVSClip* m_clip;
};
class SVSPianoRoll : public QDialog {
 Q_OBJECT
public: explicit SVSPianoRoll(SVSClip*,QWidget* parent=nullptr);
};
}
}
#endif
