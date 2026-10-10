#ifndef LMMS_NOTE_LABEL_DISPLAY_H
#define LMMS_NOTE_LABEL_DISPLAY_H

#include <QFont>
#include <QRectF>
#include <QString>

class QPainter;
class QPalette;
class QColor;
class QToolButton;
class QWidget;

namespace lmms::gui::noteLabels {
bool enabled();
bool numbered();
bool isSetting(const QString& group, const QString& key);
QString text(int pitch);
void draw(QPainter& painter, const QRectF& rectangle, int pitch, Qt::Alignment alignment);
QToolButton* createControls(QWidget* parent);
bool alignmentEnabled();
void drawAlignment(QPainter& painter, const QRectF& area, const QPointF& pointer, int pitch,
	double tick, double ticksPerBar, double ticksPerBeat, const QPalette& palette,
	const QColor& pitchColor, const QColor& timeColor);
} // namespace lmms::gui::noteLabels

#endif
