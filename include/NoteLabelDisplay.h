#ifndef LMMS_NOTE_LABEL_DISPLAY_H
#define LMMS_NOTE_LABEL_DISPLAY_H

#include <QFont>
#include <QRectF>
#include <QString>

class QPainter;
class QPalette;
class QColor;
class QWidget;

namespace lmms::gui::noteLabels {
bool enabled();
bool numbered();
bool isSetting(const QString& group, const QString& key);
int referencePitch(const QWidget* context = nullptr);
QString text(int pitch, const QWidget* context = nullptr);
void draw(QPainter& painter, const QRectF& rectangle, int pitch, Qt::Alignment alignment);
QWidget* createControls(QWidget* parent, QWidget* owner);
bool alignmentEnabled();
void drawAlignment(QPainter& painter, const QRectF& area, const QPointF& pointer, int pitch,
	double tick, double ticksPerBar, double ticksPerBeat, const QPalette& palette,
	const QColor& pitchColor, const QColor& timeColor, const QWidget* context = nullptr);
} // namespace lmms::gui::noteLabels

#endif
