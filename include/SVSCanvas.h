#ifndef LMMS_SVS_CANVAS_H
#define LMMS_SVS_CANVAS_H
#include "SVSClip.h"
#include <QWidget>
#include <QSet>
#include <QPointF>
#include <QImage>
#include <QColor>
#include <functional>
#include <memory>

class QLineEdit;
class QTimer;
namespace lmms::gui {
class SVSEditTransaction;
class SVSCurveGesture;
class SVSCanvas : public QWidget {
 Q_OBJECT
public:
 enum class Tool { Notes,Freehand,Anchor,Line,Smooth,Erase };
 explicit SVSCanvas(SVSClip*,QWidget* parent=nullptr);
 ~SVSCanvas() override;
 QRectF noteRect(const svs::Note&) const;
 QPointF pointAt(double tick,double pitch) const;
 double tickAt(double x) const;
 double pitchAt(double y) const;
 QPointF curvePointAt(double tick,double pitch) const;
 void setTool(Tool);
 Tool tool() const { return m_tool; }
 void setQuantization(double tick);
 void setZoom(double horizontal,double vertical);
 void setScroll(double tick,double topPitch);
 double scrollTick() const { return m_scrollTick; }
 double topPitch() const { return m_topPitch; }
 double horizontalZoom() const { return m_pixelsPerTick/2; }
 double verticalZoom() const { return m_rowHeight/12; }
 const QSet<QString>& selectedNotes() const { return m_selected; }
 void setMoveCurves(bool value) { m_moveCurves=value; }
 void cancelOperation();
 void beginLyric(const QString& id);
 void copySelection();
 void pasteSelection(double tick);
 void deleteSelection();
 void splitSelection(double tick);
 void transpose(int semitones);
 void setPronunciation(const QString& id,const QString& reading);
 void setThemeColors(const QMap<QString,QColor>& colors) { m_colors=colors; update(); }
 void setPortrait(const QImage& image,bool visible,int transparency,QPointF position={1,1});
 QSize portraitTargetSize() const;
 void setParameterLane(const svs::Parameter&,bool feedback=false);
 QString curveId() const { return m_curveId; }
 bool isParameterLane() const { return m_parameter.has_value(); }
 std::function<void()> batchLyricsRequested;
signals:
 void selectionChanged();
 void viewportChanged();
 void toolChanged();
 void portraitSizeChanged();
protected:
 void paintEvent(QPaintEvent*) override;
 void resizeEvent(QResizeEvent*) override;
 void mousePressEvent(QMouseEvent*) override;
 void mouseMoveEvent(QMouseEvent*) override;
 void mouseReleaseEvent(QMouseEvent*) override;
 void mouseDoubleClickEvent(QMouseEvent*) override;
 void keyPressEvent(QKeyEvent*) override;
 void wheelEvent(QWheelEvent*) override;
 void contextMenuEvent(QContextMenuEvent*) override;
 bool event(QEvent*) override;
 bool eventFilter(QObject*,QEvent*) override;
private:
 enum class Action { None,Frame,Move,LeftEdge,RightEdge,Pan,CreateTail,CurveStroke,CurveFrame,CurveMove,TangentIn,TangentOut };
 QPointer<SVSClip> m_clip;
 std::unique_ptr<SVSEditTransaction> m_transaction;
 std::unique_ptr<SVSCurveGesture> m_curveGesture;
 QSet<double> m_selectedAnchors,m_initialAnchors,m_dragAnchors;
 double m_anchorTick=0;
 Tool m_tool=Tool::Notes;
 Action m_action=Action::None;
 QSet<QString> m_selected,m_initialSelection;
 QMap<QString,QColor> m_colors;
 QLineEdit* m_lyric=nullptr;
 QTimer* m_autoScroll=nullptr;
 QString m_lyricId,m_hitId;
 svs::Note m_createdNote;
 bool m_composing=false,m_moveCurves=false,m_finishing=false,m_mouseCaptured=false;
 QPointF m_begin,m_pointer;
 QRectF m_frame;
 double m_scrollTick=0,m_topPitch=72,m_pixelsPerTick=2,m_rowHeight=24,m_quantization=12,m_beginTick=0,m_beginPitch=0,m_panTick=0,m_panPitch=72;
 QImage m_portrait;
 bool m_showPortrait=true;
 QPointF m_portraitPosition{1,1};
 int m_transparency=70;
 QString m_curveId="svs.pitch";
 std::optional<svs::Parameter> m_parameter;
 bool m_feedback=false;
 const QVector<svs::Note>& displayedNotes() const;
 QColor color(const QString&,QPalette::ColorRole) const;
 double snap(double,Qt::KeyboardModifiers) const;
 QString hitNote(const QPointF&) const;
 void commitOperation();
 void updateOperation(const QPointF&,Qt::KeyboardModifiers);
 void finishLyric(bool commit,int navigate=0);
 void rememberViewport();
 svs::Curve pitchCurve() const;
 void beginCurveStroke(const QPointF&);
 void paintPitch(QPainter&);
 double curveMinimum() const;
 double curveMaximum() const;
 double curveNumber(const QJsonValue&) const;
 QJsonValue curveValue(double) const;
 double curveValueAtY(double) const;
 bool curveEditable() const;
 Tool effectiveTool() const { return m_parameter&&m_tool==Tool::Notes?Tool::Anchor:m_tool; }
};
}
#endif
