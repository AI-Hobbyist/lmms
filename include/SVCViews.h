#pragma once

#include "ClipView.h"
#include "TrackView.h"

namespace lmms {
class SVCTrack;
class SVCClip;
namespace gui {
class SVCTrackView : public TrackView
{
	Q_OBJECT
public:
	SVCTrackView(SVCTrack* track, TrackContainerView* container);
};

class SVCClipView : public ClipView
{
	Q_OBJECT
public:
	SVCClipView(SVCClip* clip, TrackView* view);

protected:
	void paintEvent(QPaintEvent*) override;
	void mouseDoubleClickEvent(QMouseEvent*) override;
	void dragEnterEvent(QDragEnterEvent*) override;
	void dropEvent(QDropEvent*) override;
	void constructContextMenu(QMenu* menu) override;

private:
	void importAudio();
	SVCClip* m_clip;
};
} // namespace gui
} // namespace lmms
