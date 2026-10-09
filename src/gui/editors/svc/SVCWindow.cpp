#include "SVCWindow.h"

#include <QCheckBox>
#include <QComboBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QScrollArea>
#include <QSlider>
#include <QStandardItemModel>
#include <QTimer>
#include <algorithm>
#include <cmath>

#include "AudioEngine.h"
#include "Engine.h"
#include "Knob.h"
#include "PlayHandle.h"
#include "SVCCatalog.h"
#include "SVCClip.h"
#include "SVCConversion.h"
#include "SVCTrack.h"
#include "Song.h"
#include "embed.h"

namespace lmms::gui {
namespace {
class AuditionHandle final : public PlayHandle
{
public:
	AuditionHandle(SVCTrack* track, std::shared_ptr<svc::AuditionState> state)
		: PlayHandle(Type::SVCPlayHandle)
		, m_track(track)
		, m_state(std::move(state))
	{ setAudioBusHandle(new AudioBusHandle("SVC comparison", false)); }
	~AuditionHandle() override { delete audioBusHandle(); }
	bool isFinished() const override { return !m_state->alive; }
	bool isFromTrack(const Track* track) const override { return track == m_track; }
	void play(SampleFrame* buffer) override
	{
		const auto count = Engine::audioEngine()->framesPerPeriod();
		zeroSampleFrames(buffer, count);
		if (!m_state->playing || !m_state->playback || Engine::getSong()->isExporting()) { return; }
		const auto snapshot = m_state->playback->snapshot();
		const auto frames = snapshot->source->frames();
		if (!frames) { return; }
		const auto serial = m_state->seekSerial.load();
		if (serial != m_serial)
		{
			m_position = std::min(m_state->seekPosition.load(), frames);
			m_serial = serial;
		}
		const auto rate = Engine::audioEngine()->outputSampleRate();
		const auto step = double(snapshot->source->rate) / rate;
		const auto mode = m_state->mode.load();
		const double targetA = mode == svc::AuditionMode::Rendered ? 0 : std::pow(10., m_state->sourceGainDb / 20.);
		const double targetB = mode == svc::AuditionMode::Source ? 0 : std::pow(10., m_state->renderedGainDb / 20.);
		const auto smoothing = std::max(1., rate * .005);
		const auto gainStepA = (targetA - m_gainA) / smoothing;
		const auto gainStepB = (targetB - m_gainB) / smoothing;
		const auto loopStart = std::min(m_state->loopStart.load(), frames);
		const auto loopEnd = std::min(m_state->loopEnd.load(), frames);
		if (m_snapshot != snapshot)
		{
			m_previous = m_snapshot;
			m_snapshot = snapshot;
			m_fade = 0;
		}
		bool overload = false;
		for (f_cnt_t index = 0; index < count; ++index)
		{
			if (m_state->loop && loopEnd > loopStart && m_position >= loopEnd) { m_position = loopStart; }
			if (m_position >= frames)
			{
				m_state->playing = false;
				break;
			}
			if (index < smoothing)
			{
				m_gainA += gainStepA;
				m_gainB += gainStepB;
			}
			auto sample = svc::auditionSampleLinear(*snapshot, m_position, m_gainA, m_gainB);
			if (m_previous && m_fade < smoothing)
			{
				const auto old = svc::auditionSampleLinear(*m_previous, m_position, m_gainA, m_gainB);
				const auto mix = ++m_fade / smoothing;
				for (int channel = 0; channel < 2; ++channel)
				{
					sample[channel] = old[channel] * (1 - mix) + sample[channel] * mix;
				}
			}
			for (int channel = 0; channel < 2; ++channel)
			{
				buffer[index][channel] = sample[channel];
				overload |= std::abs(sample[channel]) > 1;
			}
			m_position += step;
		}
		if (m_fade >= smoothing) { m_previous.reset(); }
		m_state->position = std::min<uint64_t>(m_position, frames);
		m_state->overload = overload;
	}

private:
	SVCTrack* m_track;
	std::shared_ptr<svc::AuditionState> m_state;
	std::shared_ptr<const svc::PlaybackSnapshot> m_snapshot, m_previous;
	uint64_t m_serial = UINT64_MAX;
	double m_position = 0, m_gainA = 0, m_gainB = 0, m_fade = 0;
};

void choices(QComboBox* combo, const QJsonArray& entries, const QString& selected)
{
	combo->clear();
	for (const auto& entry : entries)
	{
		const auto option = entry.toObject();
		combo->addItem(option.value("name").toString(option.value("id").toString()), option.value("id").toVariant());
		const auto available = option.value("available").toBool(true);
		if (!available)
		{
			auto* item = static_cast<QStandardItemModel*>(combo->model())->item(combo->count() - 1);
			item->setEnabled(false);
			item->setToolTip(option.value("reason").toString(QObject::tr("Unavailable")));
		}
	}
	auto index = combo->findData(selected);
	if (index < 0 && !selected.isEmpty())
	{
		combo->addItem(QObject::tr("Unavailable: %1").arg(selected), selected);
		index = combo->count() - 1;
		static_cast<QStandardItemModel*>(combo->model())->item(index)->setEnabled(false);
	}
	combo->setCurrentIndex(index < 0 ? 0 : index);
}

QJsonObject selectedModel(SVCTrack* track)
{
	const auto selection = track->selection();
	const auto engine = svc::Catalog::instance().engine(selection.value("engine_id").toString());
	for (const auto& entry : engine.capabilities.value("models").toArray())
	{
		if (entry.toObject().value("id") == selection.value("model_id")) { return entry.toObject(); }
	}
	return {};
}
} // namespace

SVCWaveform::SVCWaveform(QWidget* parent)
	: QWidget(parent)
{
	setMinimumHeight(160);
	setToolTip(tr("Click to seek; Shift-drag to select a loop"));
}
void SVCWaveform::bind(std::shared_ptr<svc::AuditionState> state)
{
	m_audition = std::move(state);
	update();
}
uint64_t SVCWaveform::frameAt(int x) const
{
	if (!m_audition || !m_audition->playback) { return 0; }
	return uint64_t(
		std::clamp(x, 0, width()) * double(m_audition->playback->snapshot()->source->frames()) / std::max(1, width()));
}
void SVCWaveform::mousePressEvent(QMouseEvent* event)
{
	if (event->button() != Qt::LeftButton) { return; }
	m_loopAnchor = frameAt(event->pos().x());
	m_selecting = event->modifiers().testFlag(Qt::ShiftModifier);
	if (!m_selecting) { emit seek(m_loopAnchor); }
}
void SVCWaveform::mouseReleaseEvent(QMouseEvent* event)
{
	if (m_selecting)
	{
		const auto end = frameAt(event->pos().x());
		emit loopRange(std::min(end, m_loopAnchor), std::max(end, m_loopAnchor));
		m_selecting = false;
	}
}
void SVCWaveform::paintEvent(QPaintEvent*)
{
	QPainter painter(this);
	painter.fillRect(rect(), palette().base());
	if (!m_audition || !m_audition->playback) { return; }
	const auto snapshot = m_audition->playback->snapshot();
	const auto frames = snapshot->source->frames();
	if (!frames) { return; }
	const auto scale = double(width()) / frames;
	if (m_audition->loop)
	{
		auto color = m_playheadColor;
		color.setAlpha(35);
		painter.fillRect(
			QRectF(m_audition->loopStart * scale, 0, (m_audition->loopEnd - m_audition->loopStart) * scale, height()),
			color);
	}
	for (int x = 0; x < width(); ++x)
	{
		const auto frame = std::min<uint64_t>(frames - 1, uint64_t(x) * frames / width());
		const auto a = std::clamp(snapshot->source->stereo[frame * 2], -1.f, 1.f);
		bool available = false;
		const auto b = std::clamp(snapshot->renderedSample(frame, available), -1.f, 1.f);
		painter.setPen(m_sourceColor);
		painter.drawLine(QPointF(x, height() * .25), QPointF(x, height() * (.25 - a * .22)));
		painter.setPen(available ? m_renderedColor : m_pendingColor);
		painter.drawLine(QPointF(x, height() * .75), QPointF(x, height() * (.75 - b * .22)));
	}
	painter.setPen(m_sourceColor);
	painter.drawText(8, 18, tr("A · Original"));
	painter.setPen(m_renderedColor);
	painter.drawText(8, height() / 2 + 18, tr("B · Converted (pending = silence)"));
	painter.setPen(m_playheadColor);
	painter.drawLine(QPointF(m_audition->position * scale, 0), QPointF(m_audition->position * scale, height()));
	if (m_audition->failed)
	{
		painter.setPen(m_errorColor);
		painter.drawRect(rect().adjusted(0, 0, -1, -1));
	}
}

SVCWindow::SVCWindow(SVCTrack* track, QWidget* parent)
	: QDialog(parent)
	, m_track(track)
{
	setWindowTitle(tr("Singing Voice Conversion — %1").arg(track->name()));
	resize(760, 640);
	auto* layout = new QVBoxLayout(this);
	auto* form = new QFormLayout;
	m_clips = new QComboBox(this);
	m_clips->setObjectName("svcClips");
	m_engines = new QComboBox(this);
	m_engines->setObjectName("svcEngine");
	m_models = new QComboBox(this);
	m_models->setObjectName("svcModel");
	m_weights = new QComboBox(this);
	m_weights->setObjectName("svcWeight");
	m_speakers = new QComboBox(this);
	m_speakers->setObjectName("svcSpeaker");
	form->addRow(tr("Audio clip"), m_clips);
	form->addRow(tr("Engine"), m_engines);
	form->addRow(tr("Model"), m_models);
	form->addRow(tr("Weights"), m_weights);
	form->addRow(tr("Speaker"), m_speakers);
	layout->addLayout(form);
	auto* buttons = new QHBoxLayout;
	auto* import = new QPushButton(tr("Import audio"), this);
	auto* render = new QPushButton(tr("Re-render"), this);
	render->setObjectName("svcReRender");
	render->setIcon(embed::getIconPixmap("svc_render.svg"));
	auto* cancel = new QPushButton(tr("Cancel"), this);
	buttons->addWidget(import);
	buttons->addWidget(render);
	buttons->addWidget(cancel);
	layout->addLayout(buttons);
	connect(import, &QPushButton::clicked, this, &SVCWindow::importAudio);
	connect(render, &QPushButton::clicked, this, [this] {
		if (m_track && m_chunkError.isEmpty()) { emit m_track->renderRequested(); }
	});
	connect(cancel, &QPushButton::clicked, this, [track] { svc::ConversionService::instance().cancel(track); });
	auto* chunks = new QHBoxLayout;
	const auto config = track->chunkConfig();
	const std::array<double, 3> values{
		config.silenceThresholdDbfs, config.lengthThresholdSeconds, config.forcedChunkSeconds};
	const QStringList labels{tr("Silence"), tr("Length threshold"), tr("Forced length")};
	for (int index = 0; index < 3; ++index)
	{
		auto model = std::make_unique<FloatModel>(values[index], index == 0 ? -120 : .001, index == 0 ? 0 : 86400,
			index == 0 ? 1 : .001, nullptr, labels[index]);
		auto* knob = new Knob(KnobType::Small17, labels[index], this, Knob::LabelRendering::WidgetFont);
		knob->setModel(model.get());
		const auto unit = index == 0 ? QString("dBFS") : QString("s");
		knob->setUnit(unit);
		auto* value = new QLabel(this);
		const auto update = [model = model.get(), value, unit] {
			value->setText(QString::number(model->value(), 'g', 7) + " " + unit);
		};
		connect(model.get(), &FloatModel::dataChanged, this, update);
		update();
		chunks->addWidget(knob);
		chunks->addWidget(value);
		connect(model.get(), &FloatModel::dataChanged, this, [this] {
			if (m_modelsOwned.size() < 3) { return; }
			auto config = m_track->chunkConfig();
			config.silenceThresholdDbfs = m_modelsOwned[0]->value();
			config.lengthThresholdSeconds = m_modelsOwned[1]->value();
			config.forcedChunkSeconds = m_modelsOwned[2]->value();
			m_chunkError = config.validate();
			findChild<QPushButton*>("svcReRender")->setEnabled(m_chunkError.isEmpty());
			if (m_chunkError.isEmpty()) { m_track->setChunkConfig(config); }
		});
		m_modelsOwned.push_back(std::move(model));
	}
	layout->addLayout(chunks);
	auto* scroll = new QScrollArea(this);
	scroll->setWidgetResizable(true);
	m_parameterBody = new QWidget(scroll);
	scroll->setWidget(m_parameterBody);
	layout->addWidget(scroll);
	m_waveform = new SVCWaveform(this);
	layout->addWidget(m_waveform);
	m_position = new QSlider(Qt::Horizontal, this);
	m_position->setRange(0, 10000);
	layout->addWidget(m_position);
	auto* comparison = new QHBoxLayout;
	auto* play = new QPushButton(tr("Play / Pause"), this);
	play->setObjectName("svcAuditionPlay");
	comparison->addWidget(play);
	play->setIcon(embed::getIconPixmap("svc_compare.svg"));
	auto* mode = new QComboBox(this);
	mode->addItems({tr("A · Original"), tr("B · Converted"), tr("A+B · Overlay")});
	mode->setObjectName("svcAuditionMode");
	comparison->addWidget(mode);
	auto* loop = new QCheckBox(tr("Loop"), this);
	loop->setObjectName("svcAuditionLoop");
	comparison->addWidget(loop);
	for (int index = 0; index < 2; ++index)
	{
		auto model = std::make_unique<FloatModel>(0, -60, 24, .1, nullptr, index == 0 ? tr("A gain") : tr("B gain"));
		auto* knob = new Knob(KnobType::Small17, model->displayName(), this, Knob::LabelRendering::WidgetFont);
		knob->setModel(model.get());
		knob->setUnit("dB");
		comparison->addWidget(knob);
		auto* value = new QLabel(tr("0 dB"), this);
		comparison->addWidget(value);
		connect(model.get(), &FloatModel::dataChanged, this, [this, index, model = model.get(), value] {
			value->setText(QString::number(model->value(), 'g', 4) + " dB");
			if (m_audition) { (index == 0 ? m_audition->sourceGainDb : m_audition->renderedGainDb) = model->value(); }
		});
		m_modelsOwned.push_back(std::move(model));
	}
	layout->addLayout(comparison);
	m_overload = new QLabel(this);
	m_overload->setObjectName("svcOverload");
	layout->addWidget(m_overload);
	m_status = new QLabel(this);
	m_status->setWordWrap(true);
	layout->addWidget(m_status);
	connect(play, &QPushButton::clicked, this, &SVCWindow::togglePlayback);
	connect(mode, &QComboBox::currentIndexChanged, this, [this](int index) {
		if (m_audition) { m_audition->mode = static_cast<svc::AuditionMode>(index); }
	});
	connect(loop, &QCheckBox::toggled, this, [this](bool enabled) {
		if (m_audition) { m_audition->loop = enabled; }
	});
	const auto seek = [this](uint64_t frame) {
		if (m_audition)
		{
			m_audition->seekPosition = frame;
			++m_audition->seekSerial;
			m_audition->position = frame;
		}
	};
	connect(m_waveform, &SVCWaveform::seek, this, seek);
	connect(m_position, &QSlider::sliderMoved, this, [this, seek](int position) {
		if (m_audition && m_audition->playback)
		{
			seek(uint64_t(position * double(m_audition->playback->snapshot()->source->frames()) / 10000));
		}
	});
	connect(m_waveform, &SVCWaveform::loopRange, this, [this, loop](uint64_t start, uint64_t end) {
		if (m_audition)
		{
			m_audition->loopStart = start;
			m_audition->loopEnd = end;
			loop->setChecked(end > start);
		}
	});
	connect(m_clips, &QComboBox::currentIndexChanged, this, [this](int index) {
		if (!m_updating && index >= 0 && index < int(m_track->getClips().size()))
		{
			selectClip(static_cast<SVCClip*>(m_track->getClip(index)));
		}
	});
	connect(m_engines, &QComboBox::currentIndexChanged, this, [this] {
		if (m_updating) { return; }
		auto selection = m_track->selection();
		selection.insert("engine_id", m_engines->currentData().toString());
		for (const auto& key : {"model_id", "weight_id", "speaker_id"})
		{
			selection.remove(key);
		}
		m_track->setSelection(selection);
		refreshModels();
	});
	connect(m_models, &QComboBox::currentIndexChanged, this, [this] {
		if (m_updating) { return; }
		auto selection = m_track->selection();
		selection.insert("model_id", m_models->currentData().toString());
		selection.remove("weight_id");
		selection.remove("speaker_id");
		m_track->setSelection(selection);
		refreshChoices();
	});
	for (auto* combo : {m_weights, m_speakers})
	{
		connect(combo, &QComboBox::currentIndexChanged, this, [this] {
			if (!m_updating)
			{
				saveSelection();
				refreshParameterAvailability();
			}
		});
	}
	connect(&svc::Catalog::instance(), &svc::Catalog::changed, this, &SVCWindow::refreshEngines);
	connect(this, &QDialog::finished, this, [this] {
		if (m_audition) { m_audition->playing = false; }
	});
	connect(track, &QObject::destroyed, this, [this] {
		m_track = nullptr;
		if (m_audition) { m_audition->alive = false; }
		hide();
	});
	refreshClips();
	refreshEngines();
	m_timer = new QTimer(this);
	m_timer->setInterval(50);
	connect(m_timer, &QTimer::timeout, this, &SVCWindow::refreshStatus);
	m_timer->start();
}
SVCWindow::~SVCWindow()
{
	if (m_audition)
	{
		m_audition->alive = false;
		m_audition->playing = false;
	}
}
void SVCWindow::refreshClips()
{
	m_updating = true;
	m_clips->clear();
	for (auto* clip : m_track->getClips())
	{
		m_clips->addItem(clip->name());
	}
	m_updating = false;
	if (!m_clip && !m_track->getClips().empty()) { selectClip(static_cast<SVCClip*>(m_track->getClip(0))); }
}
void SVCWindow::selectClip(SVCClip* clip)
{
	if (clip == m_clip && m_audition && m_audition->playback == clip->playback()) { return; }
	if (m_audition) { m_audition->alive = false; }
	m_clip = clip;
	m_handleAdded = false;
	if (clip)
	{
		connect(clip, &QObject::destroyed, this, [this] {
			if (!m_clip && m_audition)
			{
				m_audition->playing = false;
				m_audition->alive = false;
			}
		});
	}
	m_audition = std::make_shared<svc::AuditionState>();
	m_audition->playback = clip ? clip->playback() : nullptr;
	if (m_audition->playback) { m_audition->loopEnd = m_audition->playback->snapshot()->source->frames(); }
	m_audition->sourceGainDb = m_modelsOwned[3]->value();
	m_audition->renderedGainDb = m_modelsOwned[4]->value();
	m_audition->mode = static_cast<svc::AuditionMode>(findChild<QComboBox*>("svcAuditionMode")->currentIndex());
	m_audition->loop = findChild<QCheckBox*>("svcAuditionLoop")->isChecked();
	m_waveform->bind(m_audition);
	const auto& clips = m_track->getClips();
	const auto found = std::find(clips.begin(), clips.end(), clip);
	const auto index = found == clips.end() ? -1 : int(std::distance(clips.begin(), found));
	m_updating = true;
	m_clips->setCurrentIndex(index);
	m_updating = false;
}
void SVCWindow::refreshEngines()
{
	if (!m_track) { return; }
	m_updating = true;
	QJsonArray engines;
	for (const auto& engine : svc::Catalog::instance().engines())
	{
		engines.append(QJsonObject{{"id", engine.id}, {"name", engine.name}});
	}
	choices(m_engines, engines, m_track->selection().value("engine_id").toString());
	m_updating = false;
	saveSelection();
	refreshModels();
}
void SVCWindow::refreshModels()
{
	m_updating = true;
	choices(m_models,
		svc::Catalog::instance().engine(m_engines->currentData().toString()).capabilities.value("models").toArray(),
		m_track->selection().value("model_id").toString());
	m_updating = false;
	saveSelection();
	refreshChoices();
}
void SVCWindow::refreshChoices()
{
	m_updating = true;
	const auto model = selectedModel(m_track);
	choices(m_weights, model.value("weights").toArray(), m_track->selection().value("weight_id").toString());
	choices(m_speakers, model.value("speakers").toArray(), m_track->selection().value("speaker_id").toString());
	auto* form = qobject_cast<QFormLayout*>(m_weights->parentWidget()->layout()->itemAt(0)->layout());
	if (form)
	{
		form->setRowVisible(m_weights, m_weights->count() > 1);
		form->setRowVisible(m_speakers, m_speakers->count() > 1);
	}
	m_updating = false;
	saveSelection();
	refreshParameters();
}
void SVCWindow::saveSelection()
{
	auto selection = m_track->selection();
	selection.insert("engine_id", m_engines->currentData().toString());
	if (m_models->currentIndex() >= 0) { selection.insert("model_id", m_models->currentData().toString()); }
	if (m_weights->currentIndex() >= 0) { selection.insert("weight_id", m_weights->currentData().toString()); }
	if (m_speakers->currentIndex() >= 0) { selection.insert("speaker_id", m_speakers->currentData().toString()); }
	selection.insert("parameters", m_parameters.isEmpty() ? selection.value("parameters").toObject() : m_parameters);
	m_track->setSelection(selection);
}
void SVCWindow::refreshParameters()
{
	delete m_parameterBody->layout();
	qDeleteAll(m_parameterBody->findChildren<QWidget*>(QString{}, Qt::FindDirectChildrenOnly));
	m_parametersOwned.clear();
	auto* form = new QFormLayout(m_parameterBody);
	m_parameters = m_track->selection().value("parameters").toObject();
	const auto profile = svc::Catalog::instance().engine(m_engines->currentData().toString());
	for (const auto& entry : profile.capabilities.value("parameters").toArray())
	{
		const auto parameter = entry.toObject();
		const auto id = parameter.value("id").toString();
		if (!m_parameters.contains(id)) { m_parameters.insert(id, parameter.value("default")); }
		auto* row = new QWidget(m_parameterBody);
		row->setObjectName("svcParameter_" + id);
		row->setProperty("capability", parameter);
		auto* body = new QHBoxLayout(row);
		body->setContentsMargins(0, 0, 0, 0);
		if (parameter.value("type") == "enum")
		{
			auto* combo = new QComboBox(row);
			choices(combo, parameter.value("options").toArray(), m_parameters.value(id).toString());
			body->addWidget(combo);
			connect(combo, &QComboBox::currentIndexChanged, this, [this, combo, id] {
				m_parameters.insert(id, QJsonValue::fromVariant(combo->currentData()));
				saveSelection();
				refreshParameterAvailability();
			});
		}
		else
		{
			if (!parameter.contains("minimum") || !parameter.contains("maximum"))
			{
				body->addWidget(new QLabel(
					tr("%1 %2 — numeric range metadata unavailable")
						.arg(m_parameters.value(id).toVariant().toString(), parameter.value("unit").toString()),
					row));
				form->addRow(parameter.value("name").toString(), row);
				continue;
			}
			auto model = std::make_unique<FloatModel>(m_parameters.value(id).toDouble(),
				parameter.value("minimum").toDouble(), parameter.value("maximum").toDouble(),
				parameter.value("step").toDouble(1), nullptr, parameter.value("name").toString());
			model->setObjectName("svcParameterModel_" + id);
			auto* knob = new Knob(KnobType::Small17, {}, row, Knob::LabelRendering::WidgetFont);
			knob->setModel(model.get());
			const auto unit = parameter.value("unit").toString();
			knob->setUnit(unit);
			body->addWidget(knob);
			auto* value = new QLabel(row);
			body->addWidget(value);
			const auto update = [model = model.get(), value, unit] {
				value->setText(QString::number(model->value(), 'g', 7) + " " + unit);
			};
			update();
			connect(model.get(), &FloatModel::dataChanged, this, [this, model = model.get(), id, update] {
				update();
				m_parameters.insert(id, model->value());
				saveSelection();
				refreshParameterAvailability();
			});
			m_parametersOwned.push_back(std::move(model));
		}
		form->addRow(parameter.value("name").toString(), row);
	}
	saveSelection();
	refreshParameterAvailability();
}
void SVCWindow::refreshParameterAvailability()
{
	const auto context = svc::selectionContext(m_track->selection(), selectedModel(m_track));
	auto* form = qobject_cast<QFormLayout*>(m_parameterBody->layout());
	for (auto* row : m_parameterBody->findChildren<QWidget*>(QString{}, Qt::FindDirectChildrenOnly))
	{
		const auto metadata = row->property("capability").toJsonObject();
		if (metadata.isEmpty()) { continue; }
		form->setRowVisible(row, svc::conditionsMatch(metadata.value("visible_when").toObject(), context));
		row->setEnabled(metadata.value("available").toBool(true)
			&& svc::conditionsMatch(metadata.value("enabled_when").toObject(), context));
		row->setToolTip(metadata.value("reason").toString(tr("Unavailable for the selected model or parameters")));
	}
}
void SVCWindow::refreshStatus()
{
	if (!m_track) { return; }
	if (m_clips->count() != int(m_track->getClips().size())) { refreshClips(); }
	if (m_clip && (!m_audition || m_audition->playback != m_clip->playback())) { selectClip(m_clip); }
	m_status->setText(!m_chunkError.isEmpty() ? m_chunkError : m_clip ? m_clip->status() : tr("Import audio to begin"));
	m_overload->setText(m_audition && m_audition->overload ? tr("A+B overload: lower A or B gain") : QString{});
	if (m_audition) { m_audition->failed = m_clip && m_clip->conversionFailed(); }
	if (m_audition && m_audition->playback && !m_position->isSliderDown())
	{
		const auto frames = m_audition->playback->snapshot()->source->frames();
		m_position->setValue(frames ? int(m_audition->position * 10000. / frames) : 0);
	}
	m_waveform->update();
}
void SVCWindow::importAudio()
{
	const auto file = QFileDialog::getOpenFileName(
		this, tr("Import SVC audio"), {}, tr("Audio (*.wav *.flac *.ogg *.mp3);;All files (*)"));
	if (file.isEmpty()) { return; }
	auto* clip = m_clip ? m_clip.data() : static_cast<SVCClip*>(m_track->createClip(0));
	clip->setSourceFile(file);
	refreshClips();
	selectClip(clip);
}
void SVCWindow::togglePlayback()
{
	if (!m_audition || !m_audition->playback) { return; }
	if (!m_handleAdded)
	{
		m_handleAdded = Engine::audioEngine()->addPlayHandle(new AuditionHandle(m_track, m_audition));
		if (!m_handleAdded) { return; }
	}
	if (m_audition->position >= m_audition->playback->snapshot()->source->frames())
	{
		m_audition->seekPosition = 0;
		++m_audition->seekSerial;
	}
	m_audition->playing = !m_audition->playing;
}
} // namespace lmms::gui
