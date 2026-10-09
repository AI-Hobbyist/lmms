#include "NoteLabelDisplay.h"

#include "ConfigManager.h"
#include <QActionGroup>
#include <QFontDatabase>
#include <QFontMetricsF>
#include <QMenu>
#include <QPainter>
#include <QPainterPath>
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
		&& (key == "printnotelabels" || key == "notelabelmode" || key == "notelabeltonic" || key == "notelabeloctave");
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

QToolButton* createControls(QWidget* parent)
{
	// ConfigManager signals changes to existing values. Seed the shared options
	// before connecting controls so the first user change also reaches both rolls.
	const QList<QPair<QString, QString>> defaults{
		{"printnotelabels", "0"}, {"notelabelmode", "pitch"}, {"notelabeltonic", "0"}, {"notelabeloctave", "5"}};
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
