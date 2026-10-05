#ifndef LMMS_SVS_RESULT_STRIP_H
#define LMMS_SVS_RESULT_STRIP_H
#include "SVSClip.h"
#include <QWidget>
#include <QPointer>
#include <QColor>
class QTimer;
namespace lmms::gui {
class SVSResultStrip : public QWidget {
 Q_OBJECT
public:
 explicit SVSResultStrip(SVSClip*,QWidget* parent=nullptr);
 void setViewport(double tick,double zoom);
 void setThemeColors(const QMap<QString,QColor>& colors) { m_colors=colors; update(); }
 QString selectedNote() const { return m_selectedNote; }
 int selectedPhoneme() const { return m_selectedIndex; }
 QJsonObject selectedParameters() const;
 bool setSelectedParameter(const QString&,const QJsonValue&);
 void cancelOperation();
signals:
 void selectionChanged();
 void scrollRequested(double tick);
protected:
 void paintEvent(QPaintEvent*) override;
 void mousePressEvent(QMouseEvent*) override;
 void mouseMoveEvent(QMouseEvent*) override;
 void mouseReleaseEvent(QMouseEvent*) override;
 void keyPressEvent(QKeyEvent*) override;
 void contextMenuEvent(QContextMenuEvent*) override;
 bool event(QEvent*) override;
private:
 struct Cell { QString note,symbol; int index=0; double tick=0,duration=0; QJsonObject parameters; };
 QPointer<SVSClip> m_clip;
 QMap<QString,QColor> m_colors;
 QVector<svs::Note> m_before,m_preview;
 QString m_selectedNote;
 int m_selectedIndex=-1,m_boundary=-1;
 double m_scroll=0,m_pixelsPerTick=2;
 bool m_dragging=false,m_finishing=false;
 QPointF m_pointer;
 QTimer* m_autoScroll=nullptr;
 QVector<Cell> cells() const;
 QJsonObject manualPhonemes(const QString&) const;
 void updateBoundary(double tick);
 double secondsPerTick() const;
 double tickAt(double x) const { return m_scroll+(x-60)/m_pixelsPerTick; }
 double xAt(double tick) const { return 60+(tick-m_scroll)*m_pixelsPerTick; }
 QColor color(const QString&,QPalette::ColorRole) const;
};
}
#endif
