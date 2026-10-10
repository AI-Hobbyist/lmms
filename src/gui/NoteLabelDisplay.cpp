#include "NoteLabelDisplay.h"

#include "ConfigManager.h"
#include "ComboBox.h"
#include <QHBoxLayout>
#include <QLabel>
#include <QFontDatabase>
#include <QFontMetricsF>
#include <QPainter>
#include <QPainterPath>
#include <QPalette>
#include <QSignalBlocker>
#include <algorithm>
#include <cmath>

namespace lmms::gui::noteLabels {
namespace {
const QStringList Names{"C", "C♯", "D", "D♯", "E", "F", "F♯", "G", "G♯", "A", "A♯", "B"};

int setting(const char* key, int fallback, int maximum)
{ return std::clamp(ConfigManager::inst()->value("ui", key, QString::number(fallback)).toInt(), 0, maximum); }

// Digits and accidentals use Jianpu ASCII. Construct octave dots separately so
// every MIDI octave is supported, including beyond the font's two-dot ligatures.
QPainterPath numberedPath(int pitch, int reference)
{
	static const QString family = [] {
		const int id = QFontDatabase::addApplicationFont(":/JianpuASCII.ttf");
		return QFontDatabase::applicationFontFamilies(id).value(0);
	}();
	QFont font(family);
	font.setPixelSize(100);
	const int relative = pitch - reference;
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
{ return ConfigManager::inst()->value("ui", "notelabelmode", "pitch") == "numbered"; }

bool isSetting(const QString& group, const QString& key)
{
	return group == "ui"
		&& (key == "printnotelabels" || key == "notelabelmode" || key == "notelabeltonic" || key == "notelabeloctave"
			|| key == "pitchalignmentaxis" || key == "pitchalignmentlabelposition");
}

int referencePitch(const QWidget* context)
{
	int octave = setting("notelabeloctave", 5, 10);
	for (auto* widget = context; widget; widget = widget->parentWidget())
	{
		const auto local = widget->property("noteLabelReferenceOctave");
		if (local.isValid())
		{
			if (local.toInt() >= 0) { octave = std::clamp(local.toInt(), 0, 10); }
			break;
		}
	}
	return 12 * octave + setting("notelabeltonic", 0, 11);
}

QString text(int pitch, const QWidget* context)
{
	if (!numbered()) { return Names[(pitch % 12 + 12) % 12] + QString::number(int(std::floor(pitch / 12.0)) - 1); }
	const int relative = pitch - referencePitch(context);
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
	const auto path = numberedPath(pitch, referencePitch());
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
	const QColor& pitchColor, const QColor& timeColor, const QWidget* context)
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
	const bool showKey = numbered();
	const QString standard = Names[pitch % 12] + QString::number(pitch / 12 - 1)
		+ QString(" · %1 Hz").arg(440. * std::pow(2., (pitch - 69) / 12.), 0, 'f', 2);
	const QString pitchInformation
		= standard + (showKey ? " · 1=" + Names[setting("notelabeltonic", 0, 11)] : QString{});
	const int bar = int(std::floor(tick / ticksPerBar));
	const QString time = QObject::tr("Bar %1 · Beat %2").arg(bar + 1)
		.arg(1 + (tick - bar * ticksPerBar) / ticksPerBeat, 0, 'f', 2);
	const qreal labelHeight = metrics.height() + 10;
	const int reference = referencePitch(context);
	const auto keyPath = showKey ? numberedPath(pitch, reference) : QPainterPath{};
	const auto keyBounds = keyPath.boundingRect();
	// Keep the numeral large even when several octave dots extend the glyph.
	const qreal keyScale = showKey ? 20.0 / numberedPath(reference, reference).boundingRect().height() : 1.0;
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
		const qreal labelWidth = std::max(metrics.horizontalAdvance(pitchInformation) + keyWidth,
			metrics.horizontalAdvance(time)) + 12;
		const qreal x = std::clamp(pointer.x() + 12, area.left(), std::max(area.left(), area.right() - labelWidth));
		const qreal y = std::clamp(pointer.y() + 12, area.top(),
			std::max(area.top(), area.bottom() - pitchLabelHeight - labelHeight - 4));
		panel(pitchInformation, QPointF(x, y), showKey);
		panel(time, QPointF(x, y + pitchLabelHeight + 4), false);
	}
	else
	{
		panel(time, QPointF(pointer.x() + 12, area.top() + 4), false);
		const qreal pitchY = pointer.y() - pitchLabelHeight - 8;
		panel(pitchInformation, QPointF(area.left() + 4,
			pitchY < area.top() + labelHeight + 8 ? pointer.y() + 8 : pitchY), showKey);
	}
	painter.restore();
}

QWidget* createControls(QWidget* parent, QWidget* owner)
{
	// Seed shared settings before connecting the two piano-roll selectors.
	const QList<QPair<QString, QString>> defaults{
		{"printnotelabels", "0"}, {"notelabeltonic", "0"}, {"notelabeloctave", "5"},
		{"pitchalignmentaxis", "0"}, {"pitchalignmentlabelposition", "axes"}};
	for (const auto& option : defaults)
	{
		ConfigManager::inst()->setValue(
			"ui", option.first, ConfigManager::inst()->value("ui", option.first, option.second));
	}
	ConfigManager::inst()->setValue("ui", "notelabelmode", "numbered");

	auto* controls = new QWidget(parent);
	controls->setObjectName("noteLabelDisplayControls");
	controls->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
	controls->setFixedHeight(32);
	auto* layout = new QHBoxLayout(controls);
	layout->setContentsMargins(0, 5, 0, 5);
	layout->setSpacing(6);
	auto* label = new QLabel("1=", controls);
	auto* tonic = new ComboBox(controls);
	tonic->setObjectName("noteLabelTonicComboBox");
	tonic->setFixedSize(72, ComboBox::DEFAULT_HEIGHT);
	label->setFont(tonic->font());
	label->setBuddy(tonic);
	for (const auto& name : Names)
	{
		tonic->model()->addItem(name);
	}
	layout->addWidget(label, 0, Qt::AlignVCenter);
	layout->addWidget(tonic, 0, Qt::AlignVCenter);
	auto* referenceLabel = new QLabel(QObject::tr("Reference"), controls);
	auto* reference = new ComboBox(controls);
	reference->setObjectName("noteLabelReferenceComboBox");
	reference->model()->addItem(QObject::tr("Follow global"));
	for (int octave = -1; octave <= 9; ++octave)
	{
		reference->model()->addItem("C" + QString::number(octave));
	}
	reference->setFixedSize(std::max(96, QFontMetrics(reference->font())
		.horizontalAdvance(QObject::tr("Follow global")) + 30), ComboBox::DEFAULT_HEIGHT);
	referenceLabel->setFont(reference->font());
	referenceLabel->setBuddy(reference);
	reference->setToolTip(QObject::tr("Reference C pitch for this piano roll only; Follow global uses the global setting."));
	owner->setProperty("noteLabelReferenceOctave", -1);
	QObject::connect(reference->model(), &Model::dataChanged, controls, [reference, owner] {
		owner->setProperty("noteLabelReferenceOctave", reference->model()->value() - 1);
		owner->update();
		for (auto* child : owner->findChildren<QWidget*>()) { child->update(); }
	});
	layout->addWidget(referenceLabel, 0, Qt::AlignVCenter);
	layout->addWidget(reference, 0, Qt::AlignVCenter);

	auto refresh = [controls, tonic] {
		controls->setVisible(alignmentEnabled());
		QSignalBlocker blocker(tonic->model());
		tonic->model()->setValue(setting("notelabeltonic", 0, 11));
		tonic->update();
	};
	QObject::connect(tonic->model(), &Model::dataChanged, controls, [tonic] {
		ConfigManager::inst()->setValue("ui", "notelabeltonic", QString::number(tonic->model()->value()));
		ConfigManager::inst()->setValue("ui", "notelabelmode", "numbered");
		ConfigManager::inst()->saveConfigFile();
	});
	QObject::connect(ConfigManager::inst(), &ConfigManager::valueChanged, controls,
		[refresh](const QString& group, const QString& key, const QString&) {
			if (isSetting(group, key)) { refresh(); }
		});
	refresh();
	return controls;
}
} // namespace lmms::gui::noteLabels
