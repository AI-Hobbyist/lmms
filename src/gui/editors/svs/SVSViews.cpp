#include "SVSViews.h"
#include "SVSTrack.h"
#include "SVSClip.h"
#include "SVSParameterPanel.h"
#include "SVSCanvas.h"
#include "SVSResultStrip.h"
#include "SVSPitchRanges.h"
#include "SVSLyricEditor.h"
#include "SVSImageLoader.h"
#include "ConfigManager.h"
#include "TrackLabelButton.h"
#include "StringPairDrag.h"
#include "Knob.h"
#include "MixerChannelLcdSpinBox.h"
#include "RenameDialog.h"
#include "EffectRackView.h"
#include "embed.h"
#include "Engine.h"
#include "Song.h"
#include "SongEditor.h"
#include "TimeLineWidget.h"
#include "Timeline.h"
#include "GuiApplication.h"
#include "MainWindow.h"
#include "SubWindow.h"
#include <QDialog>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QComboBox>
#include <QLabel>
#include <QPushButton>
#include <QPainter>
#include <QMouseEvent>
#include <QMenu>
#include <QDropEvent>
#include <QScrollBar>
#include <QToolButton>
#include <QButtonGroup>
#include <QSignalBlocker>
#include <QInputDialog>
#include <QLineEdit>
#include <QUuid>
#include <QFileDialog>
#include <QFile>
#include <QTabWidget>
#include <QLocale>
#include <QStyle>
#include <QStyleOptionToolButton>
#include <QScrollArea>
#include <QSplitter>
#include <QCheckBox>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QSlider>
#include <QFrame>
#include <QFileInfo>
#include <QFormLayout>
#include <algorithm>
#include <memory>
#include <cmath>
#include "FadeButton.h"
#include "PianoRoll.h"
#include "Mixer.h"
#include "MixerView.h"

namespace lmms::gui {
namespace {
// Global base values are declared by the selected engine/voice, never named here.
class SVSGlobalControls final : public QWidget
{
	struct Row
	{
		QWidget* body;
		QLabel* label;
		QWidget* editor;
		QSlider* slider;
		svs::Parameter parameter;
	};
	QMap<QString, Row> m_rows;
	QVBoxLayout* m_layout;
	bool m_refreshing = false;
	std::function<void(const svs::Parameter&, const QJsonValue&)> m_setter;
	std::function<void()> m_endGesture;
	void finishGesture()
	{
		if (m_endGesture)
		{
			auto finish = std::move(m_endGesture);
			m_endGesture = {};
			finish();
		}
	}
	bool eventFilter(QObject* target, QEvent* event) override
	{
		if (event->type() == QEvent::UngrabMouse || event->type() == QEvent::Hide
			|| event->type() == QEvent::WindowDeactivate)
			finishGesture();
		return QWidget::eventFilter(target, event);
	}
	static double sliderValue(const svs::Parameter& p, int position)
	{
		const double t = position / 10000.;
		const auto raw = p.scale == "log" && p.minimum > 0 ? p.minimum * std::pow(p.maximum / p.minimum, t)
														   : p.minimum + (p.maximum - p.minimum) * t;
		return std::clamp(p.minimum + std::round((raw - p.minimum) / p.step) * p.step, p.minimum, p.maximum);
	}

public:
	std::function<std::function<void()>(const svs::Parameter&)> beginGesture;
	~SVSGlobalControls() override { finishGesture(); }
	explicit SVSGlobalControls(QWidget* parent)
		: QWidget(parent)
		, m_layout(new QVBoxLayout(this))
	{
		setObjectName("svsGlobalControls");
		m_layout->setContentsMargins(0, 0, 0, 0);
	}
	void refresh(const QVector<svs::Parameter>& parameters, const QJsonObject& trackValues,
		const QJsonObject& clipValues, const QJsonObject& globals, const QJsonObject& context,
		std::function<void(const svs::Parameter&, const QJsonValue&)> setter)
	{
		m_refreshing = true;
		m_setter = std::move(setter);
		QSet<QString> present;
		int order = 0;
		auto sorted = parameters;
		std::stable_sort(sorted.begin(), sorted.end(),
			[](const auto& a, const auto& b) { return a.group == b.group ? a.order < b.order : a.group < b.group; });
		for (const auto& p : sorted)
		{
			if (!svs::globalParameter(p) || !p.writable || !p.isVisible(context))
				continue;
			const auto key = p.scope + "." + p.id;
			present.insert(key);
			if (m_rows.contains(key) && m_rows[key].parameter.type != p.type)
			{
				delete m_rows[key].body;
				m_rows.remove(key);
			}
			if (!m_rows.contains(key))
			{
				auto* body = new QWidget(this);
				auto* layout = new QVBoxLayout(body);
				layout->setContentsMargins(0, 6, 0, 6);
				auto* label = new QLabel(body);
				layout->addWidget(label);
				auto* line = new QHBoxLayout;
				layout->addLayout(line);
				QWidget* editor = nullptr;
				QSlider* slider = nullptr;
				if (p.type == "float" || p.type == "int")
				{
					slider = new QSlider(Qt::Horizontal, body);
					slider->setRange(0, 10000);
					slider->setTracking(true);
					slider->setObjectName("svsGlobalSlider." + key);
					line->addWidget(slider, 1);
					slider->installEventFilter(this);
					connect(slider, &QSlider::sliderPressed, this, [this, key] {
						finishGesture();
						if (beginGesture)
							m_endGesture = beginGesture(m_rows[key].parameter);
					});
					connect(slider, &QSlider::sliderReleased, this, [this] { finishGesture(); });
					auto* value = new QDoubleSpinBox(body);
					value->setKeyboardTracking(false);
					value->setObjectName("svsGlobalValue." + key);
					value->setMaximumWidth(100);
					line->addWidget(value);
					editor = value;
					connect(slider, &QSlider::valueChanged, this, [this, key](int position) {
						if (m_refreshing)
							return;
						const auto p = m_rows[key].parameter;
						const double absolute = sliderValue(p, position);
						{
							QSignalBlocker block(m_rows[key].editor);
							static_cast<QDoubleSpinBox*>(m_rows[key].editor)
								->setValue(absolute - p.defaultValue.toDouble());
						}
						if (m_setter)
						{
							auto setter = m_setter;
							setter(p, absolute);
						}
					});
					connect(value, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [this, key](double offset) {
						if (m_refreshing)
							return;
						const auto p = m_rows[key].parameter;
						if (m_setter)
						{
							auto setter = m_setter;
							setter(p, std::clamp(p.defaultValue.toDouble() + offset, p.minimum, p.maximum));
						}
					});
				}
				else if (p.type == "bool")
				{
					auto* value = new QCheckBox(body);
					value->setObjectName("svsGlobalValue." + key);
					line->addWidget(value);
					editor = value;
					connect(value, &QCheckBox::clicked, this, [this, key](bool checked) {
						if (!m_refreshing && m_setter)
						{
							auto setter = m_setter;
							setter(m_rows[key].parameter, checked);
						}
					});
				}
				else
				{
					auto* value = new QComboBox(body);
					value->setObjectName("svsGlobalValue." + key);
					line->addWidget(value);
					editor = value;
					connect(value, qOverload<int>(&QComboBox::activated), this, [this, key, value](int index) {
						if (!m_refreshing && m_setter)
						{
							auto setter = m_setter;
							setter(m_rows[key].parameter, value->itemData(index).toString());
						}
					});
				}
				m_rows.insert(key, {body, label, editor, slider, p});
			}
			auto& row = m_rows[key];
			row.parameter = p;
			row.label->setText(p.name);
			row.body->setVisible(true);
			row.body->setEnabled(p.enabled);
			row.body->setToolTip(p.disabledReason);
			m_layout->removeWidget(row.body);
			m_layout->insertWidget(order++, row.body);
			const auto base = svs::parameterBase(p, trackValues, clipValues, globals);
			QSignalBlocker block(row.editor);
			if (auto* value = qobject_cast<QDoubleSpinBox*>(row.editor))
			{
				const double absolute = base.toDouble(), neutral = p.defaultValue.toDouble();
				auto precision = [](double number) {
					auto text = QString::number(number, 'f', 6);
					while (text.endsWith('0'))
						text.chop(1);
					return std::max(0, int(text.size() - text.indexOf('.') - 1));
				};
				value->setDecimals(p.type == "int" ? 0
												   : std::max({precision(p.step), precision(neutral),
														 precision(p.minimum), precision(p.maximum)}));
				value->setRange(p.minimum - neutral, p.maximum - neutral);
				value->setSingleStep(p.step);
				value->setSuffix(p.unit.isEmpty() ? QString{} : " " + p.unit);
				value->setValue(absolute - neutral);
				QSignalBlocker sliderBlock(row.slider);
				const double fraction = p.maximum == p.minimum ? 0
					: p.scale == "log" && p.minimum > 0
					? std::log(std::clamp(absolute, p.minimum, p.maximum) / p.minimum) / std::log(p.maximum / p.minimum)
					: (absolute - p.minimum) / (p.maximum - p.minimum);
				row.slider->setValue(int(std::round(fraction * 10000)));
			}
			else if (auto* value = qobject_cast<QCheckBox*>(row.editor))
				value->setChecked(base.toBool());
			else if (auto* value = qobject_cast<QComboBox*>(row.editor))
			{
				value->clear();
				for (const auto& choice : p.choices)
				{
					const auto item = choice.toObject();
					value->addItem(item["name"].toString(item["id"].toString()), item["id"].toString());
				}
				value->setCurrentIndex(value->findData(base.toString()));
			}
		}
		for (auto i = m_rows.begin(); i != m_rows.end(); ++i)
			if (!present.contains(i.key()))
				i->body->hide();
		m_refreshing = false;
	}
};
// TuneLab parameter tabs: left click edits, right click toggles an overlay.
class SVSParameterTab final : public QToolButton
{
public:
	using QToolButton::QToolButton;
	std::function<void()> toggleVisibility;
	QColor curveColor;
	bool curveVisible = true;
	void displayState(bool editing, bool visible)
	{
		setChecked(editing);
		curveVisible = visible;
		update();
	}

protected:
	void mousePressEvent(QMouseEvent* event) override
	{
		if (event->button() == Qt::RightButton)
		{
			if (toggleVisibility)
				toggleVisibility();
			event->accept();
			return;
		}
		QToolButton::mousePressEvent(event);
	}
	void paintEvent(QPaintEvent*) override
	{
		QPainter painter(this);
		QStyleOptionToolButton option;
		initStyleOption(&option);
		option.text.clear();
		option.icon = QIcon{};
		style()->drawComplexControl(QStyle::CC_ToolButton, &option, &painter, this);
		const auto box = rect().adjusted(1, 1, -1, -1);
		auto fill = curveColor;
		fill.setAlpha(curveVisible ? (isChecked() ? 90 : 35) : 10);
		painter.setRenderHint(QPainter::Antialiasing);
		painter.setBrush(fill);
		painter.setPen(QPen(isChecked() ? curveColor : palette().color(QPalette::Mid), isChecked() ? 2 : 1));
		painter.drawRoundedRect(box, 3, 3);
		painter.setPen(curveVisible ? curveColor : palette().color(QPalette::Disabled, QPalette::Text));
		painter.drawText(rect().adjusted(8, 0, -8, 0), Qt::AlignCenter, text());
	}
};
}

QDialog* createSVSPluginSettings(SVSTrack* track, QWidget* parent)
{
	auto* dialog = new QDialog(parent);
	dialog->setObjectName("svsPluginSettings");
	dialog->setWindowTitle(QObject::tr("SVS plugin settings"));
	dialog->resize(480, 420);
	auto* layout = new QVBoxLayout(dialog);
	auto* tabs = new QTabWidget(dialog);
	layout->addWidget(tabs);
	auto* page = new QWidget(tabs);
	auto* form = new QFormLayout(page);
	tabs->addTab(page, QObject::tr("Voice"));
	auto* speakers = new QComboBox(page);
	speakers->setObjectName("svsPluginSpeaker");
	auto refreshSpeakers = [track, speakers] {
		QSignalBlocker block(speakers);
		speakers->clear();
		speakers->addItem(QObject::tr("未选定"), QString{});
		for (const auto& voice : svs::Registry::instance().voices())
			speakers->addItem(voice.name, voice.pluginId + "\n" + voice.id);
		speakers->setCurrentIndex(std::max(0, speakers->findData(track->pluginId() + "\n" + track->voiceId())));
	};
	refreshSpeakers();
	QObject::connect(&svs::Registry::instance(), &svs::Registry::catalogChanged, dialog, refreshSpeakers);
	QObject::connect(track, &Track::dataChanged, dialog, refreshSpeakers);
	form->addRow(QObject::tr("Speaker"), speakers);
	QObject::connect(speakers, qOverload<int>(&QComboBox::activated), dialog, [track, speakers](int index) {
		if (index == 0)
		{
			track->bindVoice({}, {});
			return;
		}
		const auto key = speakers->itemData(index).toString().split('\n');
		if (key.size() != 2)
			return;
		track->addJournalCheckPoint();
		track->saveJournallingState(false);
		track->bindVoice(key[0], key[1]);
		track->restoreVoiceName();
		track->restoreJournallingState();
	});
	auto* avatar = new QLineEdit(page);
	avatar->setObjectName("svsPluginAvatarPath");
	auto* portrait = new QLineEdit(page);
	portrait->setObjectName("svsPluginPortraitPath");
	auto addPath = [&](const QString& title, QLineEdit* field, const QString& key) {
		auto* row = new QWidget(page);
		auto* line = new QHBoxLayout(row);
		line->setContentsMargins(0, 0, 0, 0);
		line->addWidget(field, 1);
		auto* browse = new QPushButton(QObject::tr("Browse"), row);
		line->addWidget(browse);
		form->addRow(title, row);
		auto submit = [track, field, key] {
			auto settings = track->portraitSettings();
			settings[key] = field->text();
			track->setPortraitSettings(settings);
		};
		QObject::connect(field, &QLineEdit::editingFinished, dialog, submit);
		QObject::connect(browse, &QPushButton::clicked, dialog, [dialog, field, submit] {
			const auto path = QFileDialog::getOpenFileName(dialog, QObject::tr("Choose image"), field->text(),
				QObject::tr("Images (*.png *.jpg *.jpeg *.webp *.svg *.bmp)"));
			if (!path.isEmpty())
			{
				field->setText(path);
				submit();
			}
		});
	};
	addPath(QObject::tr("Avatar"), avatar, "avatarPath");
	addPath(QObject::tr("Portrait"), portrait, "portraitPath");
	auto* visible = new QCheckBox(QObject::tr("Show portrait"), page);
	visible->setObjectName("svsPluginPortraitVisible");
	form->addRow(visible);
	QObject::connect(visible, &QCheckBox::clicked, dialog, [track](bool value) {
		auto settings = track->portraitSettings();
		settings["visible"] = value;
		track->setPortraitSettings(settings);
	});
	auto* transparency = new QSpinBox(page);
	transparency->setObjectName("svsPluginPortraitTransparency");
	transparency->setRange(0, 100);
	transparency->setSuffix("%");
	form->addRow(QObject::tr("Portrait transparency"), transparency);
	QObject::connect(transparency, qOverload<int>(&QSpinBox::valueChanged), dialog, [track](int value) {
		auto settings = track->portraitSettings();
		settings["transparency"] = value;
		track->setPortraitSettings(settings);
	});
	auto* reset = new QPushButton(QObject::tr("Use voice images"), page);
	form->addRow(reset);
	QObject::connect(reset, &QPushButton::clicked, dialog, [track] {
		auto settings = track->portraitSettings();
		settings.remove("avatarPath");
		settings.remove("portraitPath");
		track->setPortraitSettings(settings);
	});
	tabs->addTab(new EffectRackView(track->audioBusHandle()->effects(), tabs), QObject::tr("Effects"));
	auto refresh = [track, dialog, speakers, avatar, portrait, visible, transparency] {
		dialog->setEnabled(!track->readOnly());
		QSignalBlocker s(speakers), a(avatar), p(portrait), v(visible), t(transparency);
		speakers->setCurrentIndex(std::max(0, speakers->findData(track->pluginId() + "\n" + track->voiceId())));
		if (!avatar->hasFocus())
			avatar->setText(track->avatarPath());
		if (!portrait->hasFocus())
			portrait->setText(track->portraitPath());
		visible->setChecked(track->portraitSettings()["visible"].toBool(true));
		transparency->setValue(track->portraitSettings()["transparency"].toInt(70));
	};
	QObject::connect(track, &Track::dataChanged, dialog, refresh);
	QObject::connect(track, &QObject::destroyed, dialog, &QObject::deleteLater);
	refresh();
	return dialog;
}

SVSTrackView::SVSTrackView(SVSTrack* track, TrackContainerView* container)
	: TrackView(track, container)
{
	setModel(track);
	auto* label = new TrackLabelButton(this, getTrackSettingsWidget());
	label->setObjectName("svsTrackAvatar");
	auto* avatar = new SVSImageLoader(label);
	avatar->changed = [label, avatar] {
		const auto image = avatar->image();
		label->setIcon(image.isNull() ? embed::getIconPixmap("svs_track") : QPixmap::fromImage(image));
		label->setToolTip(avatar->diagnostic());
	};
	auto refresh = [label, track, avatar] {
		const auto path = track->avatarPath();
		avatar->request(
			track->portraitSettings().contains("avatarPath") ? QFileInfo(path).absolutePath() : track->voice().package,
			path, label->iconSize() * label->devicePixelRatioF());
	};
	refresh();
	connect(track, &Track::dataChanged, this, refresh);
	auto* volume = new VolumeKnob(KnobType::Small17, tr("VOL"), getTrackSettingsWidget(),
		Knob::LabelRendering::LegacyFixedFontSize, tr("Track volume"));
	volume->setModel(track->volumeModel());
	auto* pan = new Knob(KnobType::Small17, tr("PAN"), getTrackSettingsWidget(),
		Knob::LabelRendering::LegacyFixedFontSize, tr("Panning"));
	pan->setModel(track->panningModel());
	auto* mix = new MixerChannelLcdSpinBox(2, getTrackSettingsWidget(), tr("Mixer channel"), this);
	mix->setModel(track->mixerChannelModel());

	auto* activity = new FadeButton(getTrackSettingsWidget());
	m_activityIndicator = activity;
	activity->setMuted(track->isMuted());
	activity->setObjectName("voiceTrackActivity");
	activity->setFixedSize(8, 28);
	connect(track, &SVSTrack::noteStarted, activity, &FadeButton::activate, Qt::QueuedConnection);
	connect(track, &SVSTrack::noteEnded, activity, &FadeButton::noteEnd, Qt::QueuedConnection);
	auto* layout = new QHBoxLayout(getTrackSettingsWidget());
	layout->setContentsMargins(0, 0, 0, 0);
	layout->setSpacing(1);
	layout->addWidget(label);
	layout->addWidget(mix);
	layout->addWidget(activity);
	layout->addWidget(volume);
	layout->addWidget(pan);
	auto* pluginSettings = createSVSPluginSettings(track, this);
	connect(label, &QToolButton::clicked, pluginSettings, [pluginSettings] {
		pluginSettings->show();
		pluginSettings->raise();
		pluginSettings->activateWindow();
	});
	setAcceptDrops(true);
}
QMenu* SVSTrackView::createMixerMenu(QString title, QString newMixerLabel)
{
	auto* track = static_cast<SVSTrack*>(getTrack());
	const auto channelIndex = track->mixerChannelModel()->value();
	const auto* channel = Engine::mixer()->mixerChannel(channelIndex);
	if (title.contains("%2")) { title = title.arg(channelIndex).arg(channel->m_name); }
	auto* menu = new QMenu(title, this);
	auto assign = [this, track](int index) {
		track->mixerChannelModel()->setValue(index);
		getGUI()->mixerView()->setCurrentMixerChannel(index);
	};
	menu->addAction(newMixerLabel, this, [track, assign] {
		const auto index = getGUI()->mixerView()->addNewChannel();
		auto* channel = Engine::mixer()->mixerChannel(index);
		channel->m_name = track->name();
		channel->setColor(track->color());
		assign(index);
	});
	menu->addSeparator();
	for (int index = 0; index < Engine::mixer()->numChannels(); ++index)
	{
		if (index == channelIndex) { continue; }
		const auto* candidate = Engine::mixer()->mixerChannel(index);
		menu->addAction(tr("%1: %2").arg(index).arg(candidate->m_name), this, [assign, index] { assign(index); });
	}
	return menu;
}

void SVSTrackView::dragEnterEvent(QDragEnterEvent* event)
{
	if (!StringPairDrag::processDragEnterEvent(event, "svsvoice"))
		TrackView::dragEnterEvent(event);
}
void SVSTrackView::dropEvent(QDropEvent* event)
{
	if (StringPairDrag::decodeKey(event) == "svsvoice")
	{
		auto value = StringPairDrag::decodeValue(event);
		auto split = value.lastIndexOf('/');
		static_cast<SVSTrack*>(getTrack())->bindVoice(value.left(split), value.mid(split + 1));
		event->accept();
	}
	else
		TrackView::dropEvent(event);
}
SVSClipView::SVSClipView(SVSClip* clip, TrackView* view)
	: ClipView(clip, view)
	, m_clip(clip)
{
	connect(clip, &Clip::dataChanged, this, [this] {
		setToolTip(m_clip->status());
		update();
	});
	connect(ConfigManager::inst(), &ConfigManager::valueChanged, this,
		[this](const QString& group, const QString& key, const QString&) {
			if (group == "svs" && key == "showBackgroundWaveform")
				update();
		});
}
void SVSClipView::constructContextMenu(QMenu* menu)
{
	menu->addAction(embed::getIconPixmap("ghost_note"), tr("Set as ghost in piano-roll"), this, [this] {
		auto* pianoRoll = getGUI()->pianoRoll();
		pianoRoll->setGhostSVSClip(m_clip);
		pianoRoll->parentWidget()->show();
		pianoRoll->show();
		pianoRoll->setFocus();
	});
	menu->addSeparator();
	menu->addAction(embed::getIconPixmap("edit_rename"), tr("Change name"), this, [this] {
		auto name = m_clip->name();
		RenameDialog dialog(name);
		if (dialog.exec() == QDialog::Accepted) { m_clip->setName(name); }
	});
}

void SVSClipView::paintEvent(QPaintEvent*)
{
	QPainter p(this);
	if (cornerRadius() > 0)
	{
		p.setRenderHint(QPainter::Antialiasing);
		p.setClipPath(clipOutline());
	}
	const auto background = getColorForDisplay(palette().window().color());
	p.fillRect(rect(), background);
	p.setClipRect(rect().adjusted(1, 1, -1, -1), Qt::IntersectClip);
	if (const auto audio = m_clip->audio();
		audio && width() > 0 && ConfigManager::inst()->value("svs", "showBackgroundWaveform", "0").toInt() != 0)
	{
		auto tint = palette().text().color();
		tint.setAlpha(64);
		p.setPen(tint);
		const auto length = double(int(m_clip->length())), offset = double(int(m_clip->startTimeOffset()));
		for (int x = 1; x < width() - 1; ++x)
		{
			const auto from
				= audio->mapping.samplePosition(x * length / width() - offset, audio->startTick, audio->rate),
				to = audio->mapping.samplePosition((x + 1) * length / width() - offset, audio->startTick, audio->rate);
			if (to <= 0 || from >= double(audio->samples.size() / 2) || to <= from)
				continue;
			const auto first = size_t(std::max(0., from)),
					   end = std::min(audio->samples.size() / 2, size_t(std::ceil(std::max(0., to))));
			auto peak = audio->waveform.peak(first, end);
			if (end - first < 64)
			{
				peak = {};
				for (auto frame = first; frame < end; ++frame)
				{
					peak.minimum = std::min({peak.minimum, audio->samples[frame * 2], audio->samples[frame * 2 + 1]});
					peak.maximum = std::max({peak.maximum, audio->samples[frame * 2], audio->samples[frame * 2 + 1]});
				}
			}
			p.drawLine(QPointF(x, height() * .5 - std::clamp(double(peak.maximum), -1., 1.) * (height() - 4) * .5),
				QPointF(x, height() * .5 - std::clamp(double(peak.minimum), -1., 1.) * (height() - 4) * .5));
		}
	}
	for (const auto& note : m_clip->notes())
	{
		double x = (note.tick + int(m_clip->startTimeOffset())) / int(m_clip->length()) * width();
		double w = note.duration / int(m_clip->length()) * width();
		double y = height() - 5 - (note.pitch - 36) / 60 * (height() - 10);
		p.fillRect(QRectF(x, y, std::max(1., w), 2), palette().text().color());
	}
	p.setPen(palette().text().color());
	p.drawText(3, 12, m_clip->name());
	p.drawText(3, height() - 3, m_clip->status());
	if (cornerRadius() > 0)
	{
		paintFlatBorder(p);
	}
}
void SVSClipView::mouseDoubleClickEvent(QMouseEvent*)
{
	if (!getGUI())
		return;
	if (!m_editor)
		m_editor = new SVSPianoRoll(m_clip);
	m_editor->openIn(getGUI()->mainWindow());
}
void SVSPianoRoll::openIn(MainWindow* mainWindow)
{
	if (!m_subWindow)
	{
		m_subWindow = mainWindow->addWindowedWidget(this,
			Qt::WindowTitleHint | Qt::WindowSystemMenuHint | Qt::WindowMinMaxButtonsHint | Qt::WindowCloseButtonHint);
		installEventFilter(this);
		m_subWindow->resize(
			size() + QSize(2 * m_subWindow->frameWidth(), m_subWindow->titleBarHeight() + m_subWindow->frameWidth()));
	}
	m_subWindow->show();
	mainWindow->workspace()->setActiveSubWindow(m_subWindow);
	if (m_subWindow->isDetached())
	{
		raise();
		activateWindow();
	}
	else
		m_subWindow->raise();
}
SVSPianoRoll::SVSPianoRoll(SVSClip* clip, QWidget* parent)
	: QWidget(parent)
{
	setWindowIcon(embed::getIconPixmap("piano"));
	setWindowTitle(tr("SVS Piano Roll — LMMS"));
	resize(1100, 740);
	auto* layout = new QVBoxLayout(this);
	auto* toolbarScroll = new QScrollArea(this);
	toolbarScroll->setObjectName("svsToolbarScroll");
	toolbarScroll->setWidgetResizable(true);
	toolbarScroll->setFrameShape(QFrame::NoFrame);
	toolbarScroll->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
	toolbarScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
	layout->addWidget(toolbarScroll);
	auto* toolbarBody = new QWidget(toolbarScroll);
	auto* toolbar = new QHBoxLayout(toolbarBody);
	toolbar->setContentsMargins(0, 0, 0, 0);
	toolbarScroll->setWidget(toolbarBody);
	auto nativeIcon = [](const QString& name) {
		QIcon icon("resources:" + name + ".png");
		return icon.isNull() ? QIcon("data:/themes/default/" + name + ".png") : icon;
	};
	auto iconButton = [nativeIcon](QToolButton* button, const QString& icon, const QString& title) {
		button->setIcon(nativeIcon(icon));
		button->setIconSize(QSize(24, 24));
		button->setText(title);
		button->setToolTip(title);
		button->setAccessibleName(title);
		button->setToolButtonStyle(Qt::ToolButtonIconOnly);
	};
	auto* play = new QToolButton(this);
	play->setObjectName("svsPlayButton");
	iconButton(play, "play", tr("Play song"));
	toolbar->addWidget(play);
	connect(play, &QToolButton::clicked, Engine::getSong(), &Song::playSong);
	auto* stop = new QToolButton(this);
	stop->setObjectName("svsStopButton");
	iconButton(stop, "stop", tr("Stop"));
	toolbar->addWidget(stop);
	connect(stop, &QToolButton::clicked, Engine::getSong(), &Song::stop);
	auto* render = new QPushButton(tr("Synthesize"), this);
	toolbar->addWidget(render);
	connect(render, &QPushButton::clicked, clip, &SVSClip::synthesize);
	auto* status = new QLabel(clip->status(), this);
	toolbar->addWidget(status);
	connect(clip, &Clip::dataChanged, this, [clip, status, render] {
		status->setText(clip->status());
		render->setEnabled(!clip->readOnly());
	});
	render->setEnabled(!clip->readOnly());
	connect(clip, &QObject::destroyed, this, [this] {
		if (m_subWindow)
			deleteLater();
		else
			close();
	});
	auto* body = new QHBoxLayout;
	layout->addLayout(body, 1);
	auto* outerGrid = new QGridLayout;
	body->addLayout(outerGrid, 1);
	auto* splitter = new QSplitter(Qt::Vertical, this);
	splitter->setObjectName("svsEditorAreas");
	outerGrid->addWidget(splitter, 0, 0);
	auto* noteArea = new QWidget(splitter);
	auto* grid = new QGridLayout(noteArea);
	grid->setContentsMargins(0, 0, 0, 0);
	auto* canvas = new SVSCanvas(clip, noteArea);
	m_canvas = canvas;
	canvas->setThemeColors(m_colors);
	grid->addWidget(canvas, 0, 0);
	auto* parameterBody = new QWidget(splitter);
	parameterBody->setObjectName("svsParameterArea");
	auto* parameterLayout = new QVBoxLayout(parameterBody);
	parameterLayout->setContentsMargins(0, 0, 0, 0);
	parameterLayout->setSpacing(0);
	auto* parameterCanvas = new SVSCanvas(clip, parameterBody);
	parameterCanvas->setThemeColors(m_colors);
	svs::Parameter emptyParameter;
	parameterCanvas->setParameterLane(emptyParameter);
	parameterCanvas->setParameterActive(false);
	parameterCanvas->setMinimumHeight(80);
	parameterLayout->addWidget(parameterCanvas, 1);
	auto* parameterScroll = new QScrollArea(parameterBody);
	parameterScroll->setObjectName("svsParameterTabs");
	parameterScroll->setWidgetResizable(true);
	parameterScroll->setFrameShape(QFrame::NoFrame);
	parameterScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
	parameterScroll->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
	parameterScroll->setFixedHeight(52);
	auto* tabBody = new QWidget(parameterScroll);
	auto* parameterTabs = new QHBoxLayout(tabBody);
	parameterTabs->setContentsMargins(8, 2, 8, 2);
	parameterTabs->setSpacing(6);
	parameterTabs->addStretch();
	auto* resultDivider = new QFrame(tabBody);
	resultDivider->setObjectName("svsReadOnlyDivider");
	resultDivider->setFrameShape(QFrame::VLine);
	resultDivider->setFrameShadow(QFrame::Plain);
	parameterTabs->addWidget(resultDivider);
	parameterTabs->addStretch();
	parameterScroll->setWidget(tabBody);
	parameterLayout->addWidget(parameterScroll);
	splitter->setSizes({440, 180});
	const auto storedSizes = clip->editorState()["areaSizes"].toArray();
	if (storedSizes.size() == 2)
		splitter->setSizes({storedSizes[0].toInt(440), storedSizes[1].toInt(180)});
	connect(splitter, &QSplitter::splitterMoved, this, [clip, splitter] {
		auto state = clip->editorState();
		QJsonArray sizes;
		for (auto size : splitter->sizes())
			sizes.append(size);
		state["areaSizes"] = sizes;
		clip->setEditorState(state);
	});
	struct Lane
	{
		SVSParameterTab* tab;
		svs::Parameter parameter;
		bool feedback;
		bool available = false;
	};
	auto lanes = std::make_shared<QMap<QString, Lane>>();
	auto selectedParameter = std::make_shared<QString>(clip->editorState()["selectedParameter"].toString());
	auto updateParameterDisplay = [clip, parameterCanvas, lanes, selectedParameter] {
		QVector<QPair<svs::Parameter, bool>> overlays;
		bool active = false;
		const auto states = clip->editorState()["lanes"].toObject();
		for (auto i = lanes->begin(); i != lanes->end(); ++i)
		{
			auto& lane = i.value();
			const bool selected = lane.available && i.key() == *selectedParameter;
			const bool visible = states[i.key()].toObject()["visible"].toBool(true);
			lane.tab->displayState(selected, visible);
			lane.tab->setVisible(lane.available);
			if (selected)
			{
				parameterCanvas->setParameterLane(lane.parameter, lane.feedback);
				parameterCanvas->setReferenceVisible(visible);
				active = true;
			}
			else if (lane.available && visible)
				overlays.append({lane.parameter, lane.feedback});
		}
		parameterCanvas->setParameterActive(active);
		parameterCanvas->setParameterOverlays(overlays);
	};
	connect(canvas, &SVSCanvas::viewportChanged, this, [canvas, parameterCanvas] {
		if (parameterCanvas->horizontalZoom() != canvas->horizontalZoom())
			parameterCanvas->setZoom(canvas->horizontalZoom(), parameterCanvas->verticalZoom());
		if (parameterCanvas->scrollTick() != canvas->scrollTick())
			parameterCanvas->setScroll(canvas->scrollTick(), parameterCanvas->topPitch());
	});
	connect(parameterCanvas, &SVSCanvas::viewportChanged, this, [canvas, parameterCanvas] {
		if (canvas->horizontalZoom() != parameterCanvas->horizontalZoom())
			canvas->setZoom(parameterCanvas->horizontalZoom(), canvas->verticalZoom());
		if (canvas->scrollTick() != parameterCanvas->scrollTick())
			canvas->setScroll(parameterCanvas->scrollTick(), canvas->topPitch());
	});
	parameterCanvas->setZoom(canvas->horizontalZoom(), parameterCanvas->verticalZoom());
	parameterCanvas->setScroll(canvas->scrollTick(), parameterCanvas->topPitch());
	auto* strip = new SVSResultStrip(clip, noteArea);
	strip->setThemeColors(m_colors);
	grid->addWidget(strip, 1, 0);
	grid->setRowStretch(0, 1);
	grid->setRowStretch(1, 0);
	connect(canvas, &SVSCanvas::viewportChanged, this,
		[canvas, strip] { strip->setViewport(canvas->scrollTick(), canvas->horizontalZoom()); });
	strip->setViewport(canvas->scrollTick(), canvas->horizontalZoom());
	connect(strip, &SVSResultStrip::scrollRequested, this,
		[canvas](double tick) { canvas->setScroll(tick, canvas->topPitch()); });
	auto* horizontal = new QScrollBar(Qt::Horizontal, this);
	horizontal->setObjectName("svsHorizontalScroll");
	outerGrid->addWidget(horizontal, 1, 0);
	auto* vertical = new QScrollBar(Qt::Vertical, this);
	vertical->setObjectName("svsVerticalScroll");
	vertical->setRange(0, 12700);
	grid->addWidget(vertical, 0, 1);
	auto refreshScroll = [clip, canvas, horizontal, vertical] {
		QSignalBlocker h(horizontal), v(vertical);
		horizontal->setRange(0, std::max(19200, int(clip->length()) * 100));
		horizontal->setValue(int(canvas->scrollTick() * 100));
		vertical->setValue(int((127 - canvas->topPitch()) * 100));
	};
	connect(canvas, &SVSCanvas::viewportChanged, this, refreshScroll);
	connect(clip, &Clip::dataChanged, this, refreshScroll);
	refreshScroll();
	connect(horizontal, &QScrollBar::valueChanged, this,
		[canvas](int value) { canvas->setScroll(value / 100., canvas->topPitch()); });
	connect(vertical, &QScrollBar::valueChanged, this,
		[canvas](int value) { canvas->setScroll(canvas->scrollTick(), 127 - value / 100.); });
	auto* followTimeline = new QToolButton(this);
	followTimeline->setObjectName("svsFollowSongTimeline");
	followTimeline->setCheckable(true);
	followTimeline->setChecked(clip->editorState()["followSongTimeline"].toBool(true));
	iconButton(followTimeline, "autoscroll_stepped_on", tr("随 Song Editor 滚动"));
	followTimeline->setToolTip(tr("随 Song Editor 时间轴自动滚动，沿用主编辑器的滚动模式；到达片段边界后停止"));
	toolbar->insertWidget(2, followTimeline);
	connect(followTimeline, &QToolButton::toggled, this, [clip](bool enabled) {
		auto state = clip->editorState();
		state["followSongTimeline"] = enabled;
		clip->setEditorState(state);
	});
	auto followPosition = [this, target = QPointer<SVSClip>(clip), canvas, followTimeline] {
		auto* song = Engine::getSong();
		if (!target || !isVisible() || !followTimeline->isChecked() || !song->isPlaying()
			|| song->playMode() != Song::PlayMode::Song)
		{
			return;
		}
		auto mode = TimeLineWidget::defaultAutoScrollState();
		if (auto* gui = getGUI(); gui && gui->songEditor())
		{
			mode = gui->songEditor()->m_editor->timeLine()->autoScroll();
		}
		if (mode == TimeLineWidget::AutoScrollState::Disabled)
		{
			return;
		}
		const auto projectTick = song->getTimeline(Song::PlayMode::Song).ticks();
		if (projectTick < int(target->startPosition()) || projectTick >= int(target->endPosition())) { return; }
		const double localTick
			= projectTick - double(int(target->startPosition())) - double(int(target->startTimeOffset()));
		const double visibleTicks = canvas->tickAt(canvas->width()) - canvas->scrollTick();
		if (visibleTicks <= 0)
		{
			return;
		}
		double contentEnd = int(target->length()) - int(target->startTimeOffset());
		for (const auto& note : target->notes())
		{
			contentEnd = std::max(contentEnd, note.tick + note.duration);
		}
		double next = canvas->scrollTick();
		if (mode == TimeLineWidget::AutoScrollState::Continuous)
		{
			next = localTick - visibleTicks / 2;
		}
		else if (localTick < next || localTick >= next + visibleTicks)
		{
			next = localTick;
		}
		next = std::clamp(next, 0., std::max(0., contentEnd - visibleTicks));
		if (next != canvas->scrollTick())
		{
			canvas->setScroll(next, canvas->topPitch());
		}
	};
	connect(&Engine::getSong()->getTimeline(Song::PlayMode::Song), &Timeline::positionChanged, this, followPosition);
	connect(followTimeline, &QToolButton::toggled, this, followPosition);
	auto* tools = new QButtonGroup(this);
	canvas->batchLyricsRequested = [this, clip, canvas] {
		auto* dialog = new SVSLyricEditor(clip, canvas->selectedNotes(), this);
		dialog->setAttribute(Qt::WA_DeleteOnClose);
		dialog->open();
	};
	auto* lyrics = new QToolButton(this);
	lyrics->setObjectName("svsBatchLyricsButton");
	lyrics->setText(tr("Lyrics"));
	toolbar->addWidget(lyrics);
	connect(lyrics, &QToolButton::clicked, this, [canvas] {
		if (canvas->batchLyricsRequested)
			canvas->batchLyricsRequested();
	});
	QIcon lyricsIcon("resources:svs_lyrics.svg");
	if (lyricsIcon.isNull())
		lyricsIcon = QIcon("data:/themes/default/svs_lyrics.svg");
	lyrics->setIcon(lyricsIcon);
	lyrics->setIconSize(QSize(24, 24));
	lyrics->setToolButtonStyle(Qt::ToolButtonIconOnly);
	lyrics->setToolTip(tr("Batch lyrics"));
	lyrics->setAccessibleName(tr("Batch lyrics"));
	const QStringList toolNames{
		tr("Select"), tr("Pencil"), tr("Pitch pen"), tr("Anchor"), tr("Smooth"), tr("Line"), tr("Erase")};
	const QStringList toolIcons{"svs_tool_select", "svs_tool_pencil", "svs_tool_pitch", "svs_tool_anchor",
		"svs_tool_smooth", "svs_tool_line", "svs_tool_erase"};
	for (int i = 0; i < toolNames.size(); ++i)
	{
		auto* button = new QToolButton(this);
		button->setObjectName(QString("svsTool%1").arg(i));
		button->setText(toolNames[i]);
		QIcon icon(QString("resources:%1.svg").arg(toolIcons[i]));
		if (icon.isNull())
			icon = QIcon(QString("data:/themes/default/%1.svg").arg(toolIcons[i]));
		button->setIcon(icon);
		button->setIconSize(QSize(24, 24));
		button->setToolButtonStyle(Qt::ToolButtonIconOnly);
		button->setCheckable(true);
		button->setFocusPolicy(Qt::NoFocus);
		button->setToolTip(i < 5 ? tr("%1 (%2)").arg(toolNames[i]).arg(i + 1) : toolNames[i]);
		tools->addButton(button, i);
		toolbar->addWidget(button);
	}
	connect(tools, &QButtonGroup::idClicked, this, [canvas, parameterCanvas, strip](int id) {
		strip->cancelOperation();
		canvas->setTool(static_cast<SVSCanvas::Tool>(id));
		parameterCanvas->setTool(static_cast<SVSCanvas::Tool>(id));
	});
	auto syncTool = [canvas, parameterCanvas, strip, tools](SVSCanvas* source) {
		strip->cancelOperation();
		const auto tool = source->tool();
		canvas->setTool(tool);
		parameterCanvas->setTool(tool);
		if (auto* button = tools->button(static_cast<int>(tool)))
			button->setChecked(true);
	};
	connect(canvas, &SVSCanvas::toolChanged, this, [canvas, syncTool] { syncTool(canvas); });
	connect(parameterCanvas, &SVSCanvas::toolChanged, this, [parameterCanvas, syncTool] { syncTool(parameterCanvas); });
	canvas->setTool(SVSCanvas::Tool::Pencil);
	auto* parameterVisible = new QToolButton(this);
	parameterVisible->setObjectName("svsParameterAreaVisible");
	parameterVisible->setText(tr("Parameters"));
	parameterVisible->setCheckable(true);
	parameterVisible->setChecked(clip->editorState()["parameterAreaVisible"].toBool(true));
	toolbar->addWidget(parameterVisible);
	parameterBody->setVisible(parameterVisible->isChecked());
	iconButton(parameterVisible, "automation", tr("Show/hide parameters"));
	connect(parameterVisible, &QToolButton::toggled, this, [clip, parameterBody, parameterCanvas](bool visible) {
		parameterCanvas->cancelOperation();
		parameterBody->setVisible(visible);
		auto state = clip->editorState();
		state["parameterAreaVisible"] = visible;
		clip->setEditorState(state);
	});
	auto* quantization = new QComboBox(this);
	quantization->setObjectName("svsQuantization");
	quantization->setToolTip(tr("Quantization"));
	auto* quantizationIcon = new QLabel(this);
	quantizationIcon->setObjectName("svsQuantizationIcon");
	quantizationIcon->setPixmap(nativeIcon("quantize").pixmap(16, 16));
	quantizationIcon->setToolTip(quantization->toolTip());
	quantizationIcon->setBuddy(quantization);
	toolbar->addWidget(quantizationIcon);
	for (int divisor : {1, 2, 4, 8, 16, 32, 64})
		quantization->addItem(QString("1/%1").arg(divisor), double(TimePos::ticksPerBar()) / divisor);
	quantization->setCurrentIndex(
		std::max(0, quantization->findData(clip->editorState()["quantization"].toDouble(12))));
	toolbar->addWidget(quantization);
	strip->setQuantization(quantization->currentData().toDouble());
	connect(strip, &SVSResultStrip::notePreviewChanged, canvas, &SVSCanvas::setNotePreview);
	connect(quantization, qOverload<int>(&QComboBox::activated), this, [canvas, strip, quantization](int index) {
		const auto tick = quantization->itemData(index).toDouble();
		canvas->setQuantization(tick);
		strip->setQuantization(tick);
	});
	auto* zoomOut = new QToolButton(this);
	zoomOut->setText(tr("−"));
	toolbar->addWidget(zoomOut);
	auto* zoomIn = new QToolButton(this);
	zoomIn->setText(tr("+"));
	toolbar->addWidget(zoomIn);
	connect(zoomOut, &QToolButton::clicked, this,
		[canvas] { canvas->setZoom(canvas->horizontalZoom() / 1.25, canvas->verticalZoom()); });
	connect(zoomIn, &QToolButton::clicked, this,
		[canvas] { canvas->setZoom(canvas->horizontalZoom() * 1.25, canvas->verticalZoom()); });
	auto* barZoom = new QComboBox(this);
	barZoom->setObjectName("svsBarZoom");
	barZoom->setToolTip(tr("Horizontal zoom: bars visible in the note area"));
	barZoom->addItem(tr("Custom"), 0);
	for (int bars : {1, 2, 4, 8, 16})
		barZoom->addItem(tr("%n bar(s)", nullptr, bars), bars);
	auto* zoomIcon = new QLabel(this);
	zoomIcon->setObjectName("svsBarZoomIcon");
	zoomIcon->setPixmap(nativeIcon("zoom_x").pixmap(16, 16));
	zoomIcon->setToolTip(barZoom->toolTip());
	zoomIcon->setBuddy(barZoom);
	toolbar->addWidget(zoomIcon);
	toolbar->addWidget(barZoom);
	connect(barZoom, qOverload<int>(&QComboBox::activated), this, [canvas, barZoom](int index) {
		const int bars = barZoom->itemData(index).toInt();
		if (bars > 0)
			canvas->setZoom(
				double(canvas->width() - 60) / (2. * TimePos::ticksPerBar() * bars), canvas->verticalZoom());
	});
	connect(canvas, &SVSCanvas::viewportChanged, this, [canvas, barZoom] {
		const auto bars = double(canvas->width() - 60) / (2. * TimePos::ticksPerBar() * canvas->horizontalZoom());
		int index = 0;
		for (int i = 1; i < barZoom->count(); ++i)
			if (std::abs(bars - barZoom->itemData(i).toInt()) < .01)
				index = i;
		barZoom->setCurrentIndex(index);
	});
	toolbar->addStretch();
	auto* track = static_cast<SVSTrack*>(clip->getTrack());
	auto* sidebarScroll = new QScrollArea(this);
	sidebarScroll->setObjectName("svsVoicePanel");
	sidebarScroll->setWidgetResizable(true);
	sidebarScroll->setMaximumWidth(280);
	sidebarScroll->setMinimumWidth(220);
	sidebarScroll->setFrameShape(QFrame::NoFrame);
	body->addWidget(sidebarScroll);
	auto* sidebar = new QWidget(sidebarScroll);
	sidebarScroll->setWidget(sidebar);
	auto* sidebarLayout = new QVBoxLayout(sidebar);
	sidebarLayout->addWidget(new QLabel(tr("Singer"), sidebar));
	auto* singer = new QComboBox(sidebar);
	singer->setObjectName("svsSinger");
	sidebarLayout->addWidget(singer);
	auto* sidebarLanguageLabel = new QLabel(tr("Language"), sidebar);
	auto* sidebarLanguage = new QComboBox(sidebar);
	sidebarLanguage->setObjectName("svsSidebarLanguage");
	sidebarLanguageLabel->setBuddy(sidebarLanguage);
	sidebarLayout->addWidget(sidebarLanguageLabel);
	sidebarLayout->addWidget(sidebarLanguage);
	auto refreshSidebarLanguage = [track, sidebarLanguage, sidebarLanguageLabel] {
		const QSignalBlocker block(sidebarLanguage);
		sidebarLanguage->clear();
		for (const auto& language : track->capabilities().languages)
		{
			const auto name = QLocale(language).nativeLanguageName();
			sidebarLanguage->addItem(name.isEmpty() ? language : name, language);
		}
		sidebarLanguage->setCurrentIndex(sidebarLanguage->findData(track->language()));
		const bool available = track->capabilitiesReady() && sidebarLanguage->count() > 1;
		sidebarLanguageLabel->setVisible(available);
		sidebarLanguage->setVisible(available);
		sidebarLanguage->setEnabled(!track->readOnly());
	};
	connect(sidebarLanguage, qOverload<int>(&QComboBox::activated), this,
		[track, sidebarLanguage](int index) { track->setLanguage(sidebarLanguage->itemData(index).toString()); });
	connect(track, &Track::dataChanged, this, refreshSidebarLanguage);
	refreshSidebarLanguage();
	auto refreshSingers = [this, track, singer] {
		QSignalBlocker block(singer);
		singer->clear();
		singer->addItem(tr("未选定"), QString{});
		for (const auto& voice : svs::Registry::instance().voices())
			singer->addItem(voice.name, voice.pluginId + "\n" + voice.id);
		singer->setCurrentIndex(std::max(0, singer->findData(track->pluginId() + "\n" + track->voiceId())));
	};
	refreshSingers();
	connect(&svs::Registry::instance(), &svs::Registry::catalogChanged, this, refreshSingers);
	connect(singer, qOverload<int>(&QComboBox::activated), this, [track, singer](int index) {
		if (index == 0)
		{
			track->bindVoice({}, {});
			return;
		}
		const auto key = singer->itemData(index).toString().split('\n');
		if (key.size() == 2)
			track->bindVoice(key[0], key[1]);
	});
	connect(track, &Track::dataChanged, this, refreshSingers);
	auto* singerHint = new QLabel(tr("请选择一个歌手"), sidebar);
	singerHint->setObjectName("svsSelectSingerHint");
	sidebarLayout->addWidget(singerHint);
	auto refreshSingerHint = [track, singerHint] {
		singerHint->setVisible(track->pluginId().isEmpty() && track->voiceId().isEmpty());
	};
	connect(track, &Track::dataChanged, this, refreshSingerHint);
	refreshSingerHint();
	auto* rangesLabel = new QLabel(sidebar);
	rangesLabel->setObjectName("svsPitchRanges");
	rangesLabel->setWordWrap(true);
	rangesLabel->setTextFormat(Qt::PlainText);
	sidebarLayout->addWidget(rangesLabel);
	auto refreshRanges = [track, rangesLabel] {
		const auto ranges = SVSPitchRanges::fromMetadata(track->voice().metadata);
		rangesLabel->setVisible(
			ranges.present && ConfigManager::inst()->value("svs", "showVoicePitchRanges", "1").toInt() != 0);
		rangesLabel->setText(tr("Available: %1\nComfortable: %2\nWeak spots: %3")
								 .arg(ranges.availableText, ranges.comfortableText, ranges.weakText)
			+ (ranges.invalid.isEmpty() ? QString{} : tr("\nUnrecognized pitch: %1").arg(ranges.invalid.join(", "))));
	};
	connect(track, &Track::dataChanged, this, refreshRanges);
	connect(ConfigManager::inst(), &ConfigManager::valueChanged, this,
		[refreshRanges](const QString& group, const QString& key, const QString&) {
			if (group == "svs" && key == "showVoicePitchRanges")
				refreshRanges();
		});
	refreshRanges();
	auto* recordingPanel = new QWidget(sidebar);
	recordingPanel->setObjectName("svsPitchRecordingPanel");
	auto* recordingLayout = new QVBoxLayout(recordingPanel);
	recordingLayout->setContentsMargins(0, 0, 0, 0);
	auto* recordingStatus = new QLabel(recordingPanel);
	recordingStatus->setObjectName("svsPitchRecordingStatus");
	recordingStatus->setWordWrap(true);
	recordingLayout->addWidget(recordingStatus);
	auto* recordings = new QComboBox(recordingPanel);
	recordings->setObjectName("svsPitchRecordings");
	recordings->setToolTip(tr("切换到此前的音高重录结果"));
	recordingLayout->addWidget(recordings);
	auto* fixedSeed = new QCheckBox(tr("固定当前种子"), recordingPanel);
	fixedSeed->setObjectName("svsPitchFixedSeed");
	auto* seed = new QDoubleSpinBox(recordingPanel);
	seed->setObjectName("svsPitchRecordingSeed");
	seed->setDecimals(0);
	seed->setRange(0, UINT32_MAX);
	seed->setPrefix(tr("种子: "));
	const auto recordingOptions = clip->editorState()["pitchRecordingOptions"].toObject();
	fixedSeed->setChecked(recordingOptions["fixed"].toBool());
	seed->setValue(recordingOptions["seed"].toDouble());
	recordingLayout->addWidget(fixedSeed);
	recordingLayout->addWidget(seed);
	auto* record = new QPushButton(tr("音高重录"), recordingPanel);
	record->setObjectName("svsRecordPitch");
	recordingLayout->addWidget(record);
	sidebarLayout->addWidget(recordingPanel);
	auto storeRecordingOptions = [clip, fixedSeed, seed] {
		auto state = clip->editorState();
		state["pitchRecordingOptions"] = QJsonObject{{"fixed", fixedSeed->isChecked()}, {"seed", seed->value()}};
		clip->setEditorState(state);
	};
	connect(fixedSeed, &QCheckBox::toggled, this, storeRecordingOptions);
	connect(seed, qOverload<double>(&QDoubleSpinBox::valueChanged), this, storeRecordingOptions);
	auto displayedRecording = std::make_shared<int>(recordingOptions.contains("seed")
		? clip->editorState()["pitchRecordingCurrent"].toInt() : -1);
	auto refreshRecordings = [clip, recordingPanel, recordingStatus, recordings, seed, displayedRecording] {
		const QSignalBlocker block(recordings);
		recordings->clear();
		const auto history = clip->editorState()["pitchRecordings"].toArray();
		for (const auto& value : history)
		{
			const auto entry = value.toObject();
			recordings->addItem(
				tr("重录 %1 · 种子 %2").arg(entry["number"].toInt()).arg(entry["seed"].toDouble(), 0, 'f', 0));
		}
		const int current
			= std::clamp(clip->editorState()["pitchRecordingCurrent"].toInt(), 0, std::max(0, int(history.size()) - 1));
		recordings->setCurrentIndex(current < history.size() ? current : -1);
		if (current != *displayedRecording)
		{
			seed->setValue(history.isEmpty() ? clip->captureInput(44100).document["seed"].toDouble()
											 : history[current].toObject()["seed"].toDouble());
			*displayedRecording = current;
		}
		recordingPanel->setEnabled(!clip->readOnly() && clip->supportsPitchRecording());
		recordingStatus->setText(!clip->supportsPitchRecording() ? tr("声库不支持自动音高")
				: history.isEmpty() ? tr("当前: 原始音高 · 尚未重录")
									: tr("当前: 重录 %1 / %2\n种子: %3")
										  .arg(current)
										  .arg(history.size() - 1)
										  .arg(history[current].toObject()["seed"].toDouble(), 0, 'f', 0));
	};
	connect(clip, &Clip::dataChanged, this, refreshRecordings);
	connect(track, &Track::dataChanged, this, refreshRecordings);
	connect(recordings, qOverload<int>(&QComboBox::activated), clip, &SVSClip::selectPitchRecording);
	connect(record, &QPushButton::clicked, this, [clip, canvas] {
		QVector<QPair<double, double>> ranges;
		for (const auto& note : clip->notes())
		{
			if (canvas->selectedNotes().isEmpty() || canvas->selectedNotes().contains(note.id))
			{
				ranges.append({note.tick, note.tick + note.duration});
			}
		}
		clip->regeneratePitch(ranges);
	});
	refreshRecordings();
	auto* globalControls = new SVSGlobalControls(sidebar);
	sidebarLayout->addWidget(globalControls);
	sidebarLayout->addStretch();
	globalControls->beginGesture = [clip = QPointer<SVSClip>(clip), track = QPointer<SVSTrack>(track)](
									   const svs::Parameter& p) -> std::function<void()> {
		if (!clip || !track)
			return {};
		if (p.scope == "track")
		{
			track->addJournalCheckPoint();
			track->saveJournallingState(false);
			return [track] {
				if (track)
					track->restoreJournallingState();
			};
		}
		clip->addJournalCheckPoint();
		clip->saveJournallingState(false);
		return [clip] {
			if (clip)
				clip->restoreJournallingState();
		};
	};
	auto* side = new QDialog(this);
	side->setObjectName("svsEditorSettings");
	side->setWindowTitle(tr("SVS editor settings"));
	auto* controls = new QVBoxLayout(side);
	auto* settings = new QToolButton(this);
	settings->setText(tr("Settings"));
	settings->setObjectName("svsEditorSettingsButton");
	toolbar->addWidget(settings);
	connect(settings, &QToolButton::clicked, side, [side] {
		side->show();
		side->raise();
	});
	iconButton(settings, "setup_general", tr("SVS editor settings"));
	auto* properties = new QToolButton(this);
	properties->setText(tr("Properties"));
	properties->setCheckable(true);
	properties->setChecked(true);
	toolbar->addWidget(properties);
	connect(properties, &QToolButton::toggled, sidebarScroll, &QWidget::setVisible);
	toolbarBody->ensurePolished();
	toolbarScroll->setFixedHeight(toolbarBody->sizeHint().height() + style()->pixelMetric(QStyle::PM_ScrollBarExtent));
	auto* portrait = new SVSImageLoader(canvas);
	portrait->setObjectName("svsPortraitLoader");
	auto* portraitVisible = new QCheckBox(tr("Show portrait"), side);
	portraitVisible->setObjectName("svsPortraitVisible");
	controls->addWidget(portraitVisible);
	auto* portraitRow = new QHBoxLayout;
	controls->addLayout(portraitRow);
	portraitRow->addWidget(new QLabel(tr("Transparency"), side));
	auto* transparency = new QSlider(Qt::Horizontal, side);
	transparency->setObjectName("svsPortraitTransparency");
	transparency->setRange(0, 100);
	portraitRow->addWidget(transparency, 1);
	auto* transparencyValue = new QSpinBox(side);
	transparencyValue->setObjectName("svsPortraitTransparencyValue");
	transparencyValue->setRange(0, 100);
	transparencyValue->setSuffix("%");
	portraitRow->addWidget(transparencyValue);
	auto* portraitDiagnostic = new QLabel(side);
	portraitDiagnostic->setObjectName("svsPortraitDiagnostic");
	portraitDiagnostic->setWordWrap(true);
	controls->addWidget(portraitDiagnostic);
	auto applyPortrait = [track = QPointer<SVSTrack>(track), canvas, portrait, portraitVisible, transparency,
							 transparencyValue, portraitDiagnostic] {
		if (!track)
			return;
		const auto settings = track->portraitSettings();
		{
			QSignalBlocker v(portraitVisible), s(transparency), n(transparencyValue);
			portraitVisible->setChecked(settings["visible"].toBool(true));
			transparency->setValue(settings["transparency"].toInt(70));
			transparencyValue->setValue(transparency->value());
		}
		canvas->setPortrait(portrait->image(), portraitVisible->isChecked(), transparency->value(),
			{settings["x"].toDouble(1), settings["y"].toDouble(1)});
		portraitDiagnostic->setText(portrait->diagnostic());
		portraitDiagnostic->setVisible(!portrait->diagnostic().isEmpty());
	};
	portrait->changed = applyPortrait;
	auto refreshPortrait = [track = QPointer<SVSTrack>(track), canvas, portrait, applyPortrait] {
		if (!track)
			return;
		const auto path = track->portraitPath();
		portrait->request(track->portraitSettings().contains("portraitPath") ? QFileInfo(path).absolutePath()
																			 : track->voice().package,
			path, canvas->portraitTargetSize());
		applyPortrait();
	};
	connect(track, &Track::dataChanged, this, refreshPortrait);
	connect(canvas, &SVSCanvas::portraitSizeChanged, this, refreshPortrait);
	refreshPortrait();
	connect(portraitVisible, &QCheckBox::toggled, this, [track](bool visible) {
		auto settings = track->portraitSettings();
		settings["visible"] = visible;
		track->setPortraitSettings(settings);
	});
	auto setTransparency = [track](int value) {
		auto settings = track->portraitSettings();
		settings["transparency"] = value;
		track->setPortraitSettings(settings);
	};
	connect(transparency, &QSlider::valueChanged, this, setTransparency);
	connect(transparencyValue, qOverload<int>(&QSpinBox::valueChanged), this, setTransparency);
	auto* portraitButtons = new QHBoxLayout;
	controls->addLayout(portraitButtons);
	auto* resetPortrait = new QPushButton(tr("Reset portrait"), side);
	resetPortrait->setObjectName("svsPortraitReset");
	portraitButtons->addWidget(resetPortrait);
	connect(resetPortrait, &QPushButton::clicked, this,
		[track] { track->setPortraitSettings({{"visible", true}, {"transparency", 70}, {"x", 1.}, {"y", 1.}}); });
	auto* defaultPortrait = new QPushButton(tr("Use for new tracks"), side);
	defaultPortrait->setObjectName("svsPortraitDefaults");
	portraitButtons->addWidget(defaultPortrait);
	connect(defaultPortrait, &QPushButton::clicked, this, [track] {
		const auto settings = track->portraitSettings();
		ConfigManager::inst()->setValue("svs", "portraitVisible", settings["visible"].toBool() ? "1" : "0");
		ConfigManager::inst()->setValue(
			"svs", "portraitTransparency", QString::number(settings["transparency"].toInt(70)));
	});
	auto* moveCurves = new QCheckBox(tr("Move curves with notes"), side);
	moveCurves->setObjectName("svsMoveCurvesWithNotes");
	moveCurves->setChecked(clip->editorState()["moveCurves"].toBool());
	canvas->setMoveCurves(moveCurves->isChecked());
	controls->addWidget(moveCurves);
	connect(moveCurves, &QCheckBox::toggled, this, [canvas, clip](bool value) {
		canvas->setMoveCurves(value);
		auto state = clip->editorState();
		state["moveCurves"] = value;
		clip->setEditorState(state);
	});
	auto* language = new QComboBox(side);
	language->setObjectName("svsLanguage");
	controls->addWidget(language);
	connect(language, qOverload<int>(&QComboBox::activated), this,
		[track, language](int index) { track->setLanguage(language->itemData(index).toString()); });
	auto* diagnostics = new QLabel(side);
	diagnostics->setWordWrap(true);
	controls->addWidget(diagnostics);
	auto* import = new QPushButton(tr("Import project dictionary"), side);
	controls->addWidget(import);
	connect(import, &QPushButton::clicked, this, [this, clip, diagnostics] {
		auto path = QFileDialog::getOpenFileName(this, tr("Import dictionary"), {}, tr("JSON dictionary (*.json)"));
		if (path.isEmpty())
			return;
		QFile file(path);
		if (!file.open(QIODevice::ReadOnly))
		{
			diagnostics->setText(file.errorString());
			return;
		}
		QString error;
		if (!clip->importDictionary(file.read(4 * 1024 * 1024 + 1), error))
			diagnostics->setText(error);
	});
	auto* tabs = new QTabWidget(side);
	controls->addWidget(tabs);
	auto* trackPanel = new SVSParameterPanel(tabs);
	tabs->addTab(trackPanel, tr("Track"));
	auto* clipPanel = new SVSParameterPanel(tabs);
	tabs->addTab(clipPanel, tr("Clip"));
	auto* notePanel = new SVSParameterPanel(tabs);
	tabs->addTab(notePanel, tr("Notes"));
	auto* phonemePanel = new SVSParameterPanel(tabs);
	tabs->addTab(phonemePanel, tr("Phoneme"));
	auto refreshPhoneme = [clip, track, strip, phonemePanel] {
		auto context = track->parameters();
		for (auto i = clip->parameters().begin(); i != clip->parameters().end(); ++i)
			context[i.key()] = i.value();
		phonemePanel->refresh(track->capabilities().parameters, "phoneme", {strip->selectedParameters()}, context,
			[strip](const QString& id, const QJsonValue& value) { strip->setSelectedParameter(id, value); });
		phonemePanel->setEnabled(strip->selectedPhoneme() >= 0
			&& track->capabilities().original["phonemes"].toObject()["attributesEditable"].toBool());
	};
	connect(strip, &SVSResultStrip::selectionChanged, this, refreshPhoneme);
	connect(clip, &Clip::dataChanged, this, refreshPhoneme);
	connect(track, &Track::dataChanged, this, refreshPhoneme);
	refreshPhoneme();
	auto* feedbackPanel = new SVSParameterPanel(tabs);
	tabs->addTab(feedbackPanel, tr("Result"));
	auto refresh = [this, clip, track, canvas, lanes, parameterCanvas, tabBody, parameterTabs, selectedParameter,
					   updateParameterDisplay, resultDivider, singer, globalControls, language, diagnostics, trackPanel,
					   clipPanel, notePanel, feedbackPanel] {
		auto context = track->parameters();
		for (const auto& p : track->capabilities().parameters)
			if (p.scope == "track" && !context.contains(p.id))
				context[p.id] = p.defaultValue;
		for (auto i = clip->parameters().begin(); i != clip->parameters().end(); ++i)
			context[i.key()] = i.value();
		singer->setCurrentIndex(std::max(0, singer->findData(track->pluginId() + "\n" + track->voiceId())));
		singer->setEnabled(!track->readOnly());
		globalControls->refresh(track->capabilities().parameters, track->parameters(), clip->parameters(),
			clip->globalParameters(), context,
			[clip, selectedParameter, updateParameterDisplay](const svs::Parameter& p, const QJsonValue& value) {
				if (!clip->setGlobalParameter(p.id, value))
					return;
				*selectedParameter = "input:" + p.id;
				auto state = clip->editorState();
				state["selectedParameter"] = *selectedParameter;
				auto lanes = state["lanes"].toObject();
				auto lane = lanes[*selectedParameter].toObject();
				lane["visible"] = true;
				lanes[*selectedParameter] = lane;
				state["lanes"] = lanes;
				clip->setEditorState(state);
				updateParameterDisplay();
			});
		globalControls->setEnabled(!clip->readOnly() && !track->readOnly());
		trackPanel->refresh(track->capabilities().parameters, "track", {track->parameters()}, context,
			[track](const QString& id, const QJsonValue& value) { track->setParameter(id, value); });
		clipPanel->refresh(track->capabilities().parameters, "clip", {clip->parameters()}, context,
			[clip](const QString& id, const QJsonValue& value) { clip->setParameter(id, value); });
		QVector<QJsonObject> selected;
		QStringList selectedIds;
		for (const auto& note : clip->notes())
			if (canvas->selectedNotes().contains(note.id))
			{
				selected << note.parameters;
				selectedIds << note.id;
			}
		notePanel->refresh(track->capabilities().parameters, "note", selected, context,
			[clip, selectedIds](
				const QString& id, const QJsonValue& value) { clip->setNoteParameter(selectedIds, id, value); });
		notePanel->setEnabled(!selected.isEmpty());
		auto audio = clip->audio();
		feedbackPanel->refresh(track->capabilities().feedbackParameters, "clip",
			audio ? QVector<QJsonObject>{audio->feedback["parameters"].toObject()} : QVector<QJsonObject>{}, context,
			{});
		QStringList current;
		for (int i = 0; i < language->count(); ++i)
			current << language->itemData(i).toString();
		if (current != track->capabilities().languages)
		{
			language->clear();
			for (const auto& value : track->capabilities().languages)
			{
				auto label = QLocale(value).nativeLanguageName();
				language->addItem(label.isEmpty() ? value : label, value);
			}
		}
		language->setCurrentIndex(language->findData(track->language()));
		diagnostics->setText(track->capabilityDiagnostics().join('\n'));
		for (auto& lane : *lanes)
			lane.available = false;
		QStringList available;
		auto configureLane = [&](const svs::Parameter& parameter, bool feedback) {
			if ((!parameter.curve && !svs::globalParameter(parameter)) || parameter.type == "string")
				return;
			const auto key = (feedback ? "feedback:" : "input:") + parameter.id;
			if (!lanes->contains(key))
			{
				auto* tab = new SVSParameterTab(tabBody);
				tab->setObjectName("svsParameterTab." + key);
				tab->setCheckable(true);
				tab->setAutoRaise(true);
				tab->setToolButtonStyle(Qt::ToolButtonTextOnly);
				tab->setFocusPolicy(Qt::NoFocus);
				parameterTabs->insertWidget(feedback || !parameter.writable ? parameterTabs->count() - 1
																			: parameterTabs->indexOf(resultDivider),
					tab);
				lanes->insert(key, {tab, parameter, feedback});
				connect(tab, &QToolButton::clicked, this,
					[clip, key, selectedParameter, updateParameterDisplay, parameterCanvas] {
						parameterCanvas->cancelOperation();
						*selectedParameter = key;
						auto state = clip->editorState();
						auto states = state["lanes"].toObject();
						auto settings = states[key].toObject();
						settings["visible"] = true;
						states[key] = settings;
						state["lanes"] = states;
						state["selectedParameter"] = *selectedParameter;
						clip->setEditorState(state);
						updateParameterDisplay();
						parameterCanvas->setFocus();
					});
				tab->toggleVisibility = [clip, key, selectedParameter, updateParameterDisplay, parameter] {
					if (*selectedParameter == key && parameter.writable)
						return;
					auto state = clip->editorState();
					auto states = state["lanes"].toObject();
					auto settings = states[key].toObject();
					settings["visible"] = !settings["visible"].toBool(true);
					states[key] = settings;
					state["lanes"] = states;
					clip->setEditorState(state);
					updateParameterDisplay();
				};
			}
			auto& lane = (*lanes)[key];
			lane.parameter = parameter;
			lane.feedback = feedback;
			lane.available = parameter.isVisible(context);
			lane.tab->setText((feedback ? tr("Result: ") : QString{}) + parameter.name);
			const QColor declared(parameter.color);
			lane.tab->curveColor = declared.isValid() ? declared : palette().highlight().color();
			lane.tab->setToolTip((feedback || !parameter.writable ? tr("Read-only result. ")
										 : parameter.curve		  ? QString{}
																  : tr("Base value: adjust the sidebar control. "))
				+ tr("Left click: select; right click: show/hide overlay.") + "\n" + parameter.disabledReason);
			parameterTabs->removeWidget(lane.tab);
			parameterTabs->insertWidget(
				feedback || !parameter.writable ? parameterTabs->count() - 1 : parameterTabs->indexOf(resultDivider),
				lane.tab);
			if (lane.available)
				available.append(key);
		};
		for (const auto& parameter : track->capabilities().parameters)
			configureLane(parameter, false);
		for (const auto& parameter : track->capabilities().feedbackParameters)
			configureLane(parameter, true);
		// Select a declared curve on first opening; retain unavailable selections for voice restoration.
		if (!clip->editorState().contains("selectedParameter") && !available.isEmpty())
		{
			*selectedParameter = available.first();
			auto state = clip->editorState();
			state["selectedParameter"] = *selectedParameter;
			clip->setEditorState(state);
		}
		bool readOnly = false;
		for (const auto& lane : *lanes)
			if (lane.available && (lane.feedback || !lane.parameter.writable))
				readOnly = true;
		resultDivider->setVisible(readOnly);
		updateParameterDisplay();
	};
	connect(clip, &Clip::dataChanged, this, refresh);
	connect(track, &Track::dataChanged, this, refresh);
	connect(canvas, &SVSCanvas::selectionChanged, this, refresh);
	refresh();
	auto migrationState = [clip, track, canvas, strip, parameterBody, tabs, lyrics, language, import] {
		const bool editable = !clip->readOnly();
		canvas->setEnabled(editable);
		strip->setEnabled(editable);
		parameterBody->setEnabled(editable);
		tabs->setEnabled(editable);
		lyrics->setEnabled(editable);
		language->setEnabled(!track->readOnly());
		import->setEnabled(editable);
	};
	connect(clip, &Clip::dataChanged, this, migrationState);
	connect(track, &Track::dataChanged, this, migrationState);
	migrationState();
}
bool SVSPianoRoll::eventFilter(QObject* target, QEvent* event)
{
	if (target == this && event->type() == QEvent::Close && m_subWindow && m_subWindow->isDetached())
	{
		// Reuse LMMS geometry/flags restoration, without hiding the returned editor.
		const bool detachable = m_subWindow->isDetachable();
		m_subWindow->setDetachable(false);
		m_subWindow->attach();
		m_subWindow->show();
		m_subWindow->raise();
		m_subWindow->setDetachable(detachable);
		event->ignore();
		return true;
	}
	return QWidget::eventFilter(target, event);
}
void SVSPianoRoll::setThemeColor(const QString& name, const QColor& value)
{
	if (value.isValid())
		m_colors[name] = value;
	else
		m_colors.remove(name);
	for (auto* canvas : findChildren<SVSCanvas*>())
		canvas->setThemeColors(m_colors);
	for (auto* strip : findChildren<SVSResultStrip*>())
		strip->setThemeColors(m_colors);
}
void SVSPianoRoll::changeEvent(QEvent* event)
{
	QWidget::changeEvent(event);
	if (event->type() == QEvent::StyleChange && !m_polishingTheme)
	{
		m_polishingTheme = true;
		m_colors.clear();
		style()->unpolish(this);
		style()->polish(this);
		m_polishingTheme = false;
	}
	if (event->type() == QEvent::PaletteChange || event->type() == QEvent::StyleChange)
		for (auto* canvas : findChildren<SVSCanvas*>())
			canvas->setThemeColors(m_colors);
	if (event->type() == QEvent::PaletteChange || event->type() == QEvent::StyleChange)
		for (auto* strip : findChildren<SVSResultStrip*>())
			strip->setThemeColors(m_colors);
}
}
