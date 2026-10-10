#include "NoteLabelDisplay.h"

#include "ConfigManager.h"
#include <QActionGroup>
#include <QFontDatabase>
#include <QFontMetricsF>
#include <QMenu>
#include <QPainter>
#include <QPainterPath>
#include <QPalette>
#include <QSignalBlocker>
#include <QToolButton>
#include <algorithm>
#include <cmath>

namespace lmms::gui::noteLabels {
namespace {
const QStringList Names{"C", "C♯", "D", "D♯", "E", "F", "F♯", "G", "G♯", "A", "A♯", "B"};

int setting(const char* key, int fallback, int maximum)
{ return std::clamp(ConfigManager::inst()->value("ui", key, QString::number(fallback)).toInt(), 0, maximum); }

int referencePitch()
{ return 12 * setting("notelabeloctave", 5, 10) + setting("notelabeltonic", 0, 11); }

// Digits and accidentals use Jianpu ASCII. Construct octave dots separately so
// every MIDI octave is supported, including beyond the font's two-dot ligatures.
QPainterPath numberedPath(int pitch)
{
	static const QString family = [] {
		const int id = QFontDatabase::addApplicationFont(":/JianpuASCII.ttf");
		return QFontDatabase::applicationFontFamilies(id).value(0);
	}();
	QFont font(family);
	font.setPixelSize(100);
	const int relative = pitch - referencePitch();
	const int octave = int(std::floor(relative / 12.0));
	static const QStringList Degrees{"1", "^1", "2", "^2", "3", "4", "^4", "5", "^5", "6", "^6", "7"};
	QPainterPath path;
	path.addText(0, 0, font, Degrees[(relative % 12 + 12) % 12]);
	const auto bounds = path.boundingRect();
	const qreal radius = bounds.height() * .055;
	const qreal spacing = radius * 3;
	// Accidentals precede the digit; center dots on the right-hand digit.
	const qreal center = bounds.right() - QFontMetricsF(font).horizontalAdvance("1") / 2;
	for (int index = 0; index < std::abs(octave); ++index)
	{
		const qreal y = octave > 0 ? bounds.top() - spacing * (index + 1) : bounds.bottom() + spacing * (index + 1);
		path.addEllipse(QPointF(center, y), radius, radius);
	}
	return path;
}
} // namespace

bool enabled()
{ return ConfigManager::inst()->value("ui", "printnotelabels").toInt() != 0; }

bool numbered()
{ return enabled() && ConfigManager::inst()->value("ui", "notelabelmode", "pitch") == "numbered"; }

bool isSetting(const QString& group, const QString& key)
{
	return group == "ui"
		&& (key == "printnotelabels" || key == "notelabelmode" || key == "notelabeltonic" || key == "notelabeloctave"
			|| key == "pitchalignmentaxis" || key == "pitchalignmentlabelposition");
}

QString text(int pitch)
{
	if (!numbered()) { return Names[(pitch % 12 + 12) % 12] + QString::number(int(std::floor(pitch / 12.0)) - 1); }
	const int relative = pitch - referencePitch();
	static const QStringList Degrees{"1", "^1", "2", "^2", "3", "4", "^4", "5", "^5", "6", "^6", "7"};
	const int octave = int(std::floor(relative / 12.0));
	return Degrees[(relative % 12 + 12) % 12] + QString(std::abs(octave), octave > 0 ? '\'' : ',');
}

void draw(QPainter& painter, const QRectF& rectangle, int pitch, Qt::Alignment alignment)
{
	if (!numbered())
	{
		painter.drawText(rectangle, alignment | Qt::AlignVCenter, text(pitch));
		return;
	}
	if (rectangle.width() <= 0 || rectangle.height() <= 0) { return; }
	const auto path = numberedPath(pitch);
	const auto bounds = path.boundingRect();
	const qreal textHeight = std::min(rectangle.height() * .8, QFontMetricsF(painter.font()).capHeight());
	const qreal scale = std::min(textHeight / bounds.height(), rectangle.width() / bounds.width());
	const qreal x = alignment.testFlag(Qt::AlignRight) ? rectangle.right() - bounds.width() * scale : rectangle.left();
	painter.save();
	painter.setRenderHint(QPainter::Antialiasing);
	painter.translate(x, rectangle.center().y() - bounds.height() * scale / 2);
	painter.scale(scale, scale);
	painter.translate(-bounds.left(), -bounds.top());
	painter.fillPath(path, painter.pen().color());
	painter.restore();
}

bool alignmentEnabled()
{
	return ConfigManager::inst()->value("ui", "pitchalignmentaxis", "0").toInt() != 0;
}

void drawAlignment(QPainter& painter, const QRectF& area, const QPointF& pointer, int pitch,
	double tick, double ticksPerBar, double ticksPerBeat, const QPalette& palette,
	const QColor& pitchColor, const QColor& timeColor)
{
	if (!alignmentEnabled() || !area.contains(pointer) || pitch < 0 || pitch > 127 || tick < 0
		|| ticksPerBar <= 0 || ticksPerBeat <= 0)
	{
		return;
	}
	painter.save();
	painter.setClipRect(area);
	const auto fallback = palette.color(QPalette::Highlight).lighter(150);
	painter.setPen(QPen(pitchColor.isValid() ? pitchColor : fallback, 1, Qt::DashLine));
	painter.drawLine(QPointF(area.left(), pointer.y()), QPointF(area.right(), pointer.y()));
	painter.setPen(QPen(timeColor.isValid() ? timeColor : fallback, 1, Qt::DashLine));
	painter.drawLine(QPointF(pointer.x(), area.top()), QPointF(pointer.x(), area.bottom()));
	QFont font = painter.font();
	font.setPixelSize(13);
	painter.setFont(font);
	const QFontMetricsF metrics(font);
	const QString standard = Names[pitch % 12] + QString::number(pitch / 12 - 1)
		+ QString(" · %1 Hz").arg(440. * std::pow(2., (pitch - 69) / 12.), 0, 'f', 2);
	const int bar = int(std::floor(tick / ticksPerBar));
	const QString time = QObject::tr("Bar %1 · Beat %2").arg(bar + 1)
		.arg(1 + (tick - bar * ticksPerBar) / ticksPerBeat, 0, 'f', 2);
	const qreal labelHeight = metrics.height() + 10;
	const bool showKey = numbered();
	const auto keyPath = showKey ? numberedPath(pitch) : QPainterPath{};
	const auto keyBounds = keyPath.boundingRect();
	// Keep the numeral large even when several octave dots extend the glyph.
	const qreal keyScale = showKey ? 20.0 / numberedPath(referencePitch()).boundingRect().height() : 1.0;
	const qreal keyWidth = showKey ? keyBounds.width() * keyScale + 10 : 0;
	const qreal pitchLabelHeight = std::max(labelHeight, keyBounds.height() * keyScale + 10);
	auto panel = [&](const QString& label, QPointF position, bool keyLabel) {
		const qreal keySpace = keyLabel ? keyWidth : 0;
		const qreal panelHeight = keyLabel ? pitchLabelHeight : labelHeight;
		const qreal labelWidth = metrics.horizontalAdvance(label) + keySpace + 12;
		position.setX(std::clamp(position.x(), area.left(), std::max(area.left(), area.right() - labelWidth)));
		position.setY(std::clamp(position.y(), area.top(), std::max(area.top(), area.bottom() - panelHeight)));
		const QRectF box(position, QSizeF(labelWidth, panelHeight));
		painter.fillRect(box, palette.color(QPalette::ToolTipBase));
		painter.setPen(palette.color(QPalette::ToolTipText));
		if (keyLabel)
		{
			painter.save();
			painter.setRenderHint(QPainter::Antialiasing);
			painter.translate(box.left() + 6, box.center().y() - keyBounds.height() * keyScale / 2);
			painter.scale(keyScale, keyScale);
			painter.translate(-keyBounds.left(), -keyBounds.top());
			painter.fillPath(keyPath, painter.pen().color());
			painter.restore();
		}
		painter.drawText(box.adjusted(6 + keySpace, 0, -6, 0), Qt::AlignVCenter | Qt::AlignLeft, label);
	};
	if (ConfigManager::inst()->value("ui", "pitchalignmentlabelposition", "axes") == "cursor")
	{
		const qreal labelWidth = std::max(metrics.horizontalAdvance(standard) + keyWidth,
			metrics.horizontalAdvance(time)) + 12;
		const qreal x = std::clamp(pointer.x() + 12, area.left(), std::max(area.left(), area.right() - labelWidth));
		const qreal y = std::clamp(pointer.y() + 12, area.top(),
			std::max(area.top(), area.bottom() - pitchLabelHeight - labelHeight - 4));
		panel(standard, QPointF(x, y), showKey);
		panel(time, QPointF(x, y + pitchLabelHeight + 4), false);
	}
	else
	{
		panel(time, QPointF(pointer.x() + 12, area.top() + 4), false);
		const qreal pitchY = pointer.y() - pitchLabelHeight - 8;
		panel(standard, QPointF(area.left() + 4,
			pitchY < area.top() + labelHeight + 8 ? pointer.y() + 8 : pitchY), showKey);
	}
	painter.restore();
}

QToolButton* createControls(QWidget* parent)
{
	// ConfigManager signals changes to existing values. Seed the shared options
	// before connecting controls so the first user change also reaches both rolls.
	const QList<QPair<QString, QString>> defaults{
		{"printnotelabels", "0"}, {"notelabelmode", "pitch"}, {"notelabeltonic", "0"}, {"notelabeloctave", "5"},
		{"pitchalignmentaxis", "0"}, {"pitchalignmentlabelposition", "axes"}};
	for (const auto& option : defaults)
	{
		ConfigManager::inst()->setValue(
			"ui", option.first, ConfigManager::inst()->value("ui", option.first, option.second));
	}
	auto* button = new QToolButton(parent);
	button->setObjectName("noteLabelDisplayButton");
	button->setPopupMode(QToolButton::InstantPopup);
	button->setToolTip(
		QObject::tr("Note labels: synchronized with SVS / instrument piano rolls; enable all note labels to use this"));
	auto* menu = new QMenu(button);
	button->setMenu(menu);
	auto addOptions = [button](QMenu* target, const QStringList& names, const QString& key, const QStringList& values) {
		auto* group = new QActionGroup(target);
		for (int index = 0; index < names.size(); ++index)
		{
			auto* action = target->addAction(names[index]);
			action->setCheckable(true);
			group->addAction(action);
			action->setData(values[index]);
			QObject::connect(action, &QAction::triggered, button, [key, value = values[index]] {
				ConfigManager::inst()->setValue("ui", key, value);
				ConfigManager::inst()->saveConfigFile();
			});
		}
		return group;
	};
	auto* modes
		= addOptions(menu, {QObject::tr("Standard pitch names CDEFGAB"), QObject::tr("Numbered notation 1234567")},
			"notelabelmode", {"pitch", "numbered"});
	auto* tonicMenu = menu->addMenu(QObject::tr("Numbered notation key"));
	QStringList tonics, tonicValues;
	for (int index = 0; index < 12; ++index)
	{
		tonics.append("1=" + Names[index]);
		tonicValues.append(QString::number(index));
	}
	auto* tonicsGroup = addOptions(tonicMenu, tonics, "notelabeltonic", tonicValues);
	auto refresh = [button, modes, tonicMenu, tonicsGroup] {
		button->setEnabled(enabled());
		button->setText(numbered() ? "123 · 1=" + Names[setting("notelabeltonic", 0, 11)] : "CDE / 123");
		button->setToolTip(numbered()
				? QObject::tr("Numbered notation reference: %1%2; change the reference C in global settings")
					  .arg(Names[setting("notelabeltonic", 0, 11)])
					  .arg(setting("notelabeloctave", 5, 10) - 1)
				: QObject::tr("Note labels: synchronized with SVS / instrument piano rolls; enable all note labels to "
							  "use this"));
		tonicMenu->menuAction()->setVisible(numbered());
		const QList<QPair<QActionGroup*, QString>> choices{
			{modes, ConfigManager::inst()->value("ui", "notelabelmode", "pitch")},
			{tonicsGroup, QString::number(setting("notelabeltonic", 0, 11))}};
		for (const auto& choice : choices)
		{
			for (auto* action : choice.first->actions())
			{
				QSignalBlocker blocker(action);
				action->setChecked(action->data().toString() == choice.second);
			}
		}
	};
	QObject::connect(ConfigManager::inst(), &ConfigManager::valueChanged, button,
		[refresh](const QString& group, const QString& key, const QString&) {
			if (isSetting(group, key)) { refresh(); }
		});
	refresh();
	return button;
}
} // namespace lmms::gui::noteLabels
