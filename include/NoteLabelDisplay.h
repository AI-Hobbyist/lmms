#ifndef LMMS_NOTE_LABEL_DISPLAY_H
#define LMMS_NOTE_LABEL_DISPLAY_H

#include <QFont>
#include <QRectF>
#include <QString>

class QPainter;
class QToolButton;
class QWidget;

namespace lmms::gui::noteLabels {
bool enabled();
bool numbered();
bool isSetting(const QString& group, const QString& key);
QString text(int pitch);
void draw(QPainter& painter, const QRectF& rectangle, int pitch, Qt::Alignment alignment);
QToolButton* createControls(QWidget* parent);
} // namespace lmms::gui::noteLabels

#endif
