#pragma once

#include <QPointer>

#include "ClipView.h"
#include "TrackView.h"

namespace lmms {
class SVCTrack;
class SVCClip;
namespace gui {
class SVCWindow;
class SVCTrackView : public TrackView
{
	Q_OBJECT
public:
	SVCTrackView(SVCTrack* track, TrackContainerView* container);
	void openWindow(SVCClip* clip = nullptr);

private:
	QPointer<SVCWindow> m_window;
};

class SVCClipView : public ClipView
{
	Q_OBJECT
	Q_PROPERTY(QColor svcTrackColor MEMBER m_trackColor)
	Q_PROPERTY(QColor svcSourceColor MEMBER m_sourceColor)
	Q_PROPERTY(QColor svcRenderedColor MEMBER m_renderedColor)
	Q_PROPERTY(QColor svcPendingColor MEMBER m_pendingColor)
	Q_PROPERTY(QColor svcErrorColor MEMBER m_errorColor)
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
	QColor m_trackColor{"#8064B5"};
	QColor m_sourceColor{"#788A9B"};
	QColor m_renderedColor{"#42B8A5"};
	QColor m_pendingColor{"#D5A64A"};
	QColor m_errorColor{"#D66A72"};
};
} // namespace gui
} // namespace lmms
