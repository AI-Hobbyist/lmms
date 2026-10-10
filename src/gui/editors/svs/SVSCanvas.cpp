#include "SVSCanvas.h"
#include "NoteLabelDisplay.h"
#include "GuiApplication.h"
#include "PianoRoll.h"
#include "SVSTrack.h"
#include "SVSPitchRanges.h"
#include "SVSLyricEditor.h"
#include "SVSNoteOperations.h"
#include "ConfigManager.h"
#include "Engine.h"
#include "Song.h"
#include "ProjectJournal.h"
#include "Timeline.h"
#include "TimeLineWidget.h"
#include "operations/SVSEditTransaction.h"
#include "operations/SVSCurveGesture.h"
#include "operations/SVSFeedbackPitch.h"
#include <QApplication>
#include <QClipboard>
#include <QCursor>
#include <QMimeData>
#include <QJsonDocument>
#include <QPainter>
#include <QPainterPath>
#include <QLineF>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QWheelEvent>
#include <QResizeEvent>
#include "operations/SVSStretchOperations.h"
#include "SVSTempoSource.h"
#include <QInputMethodEvent>
#include <QInputMethod>
#include <QHelpEvent>
#include <QLineEdit>
#include <QTimer>
#include <QMenu>
#include <QInputDialog>
#include <QUuid>
#include <algorithm>
#include <cmath>

namespace lmms::gui {
namespace {
constexpr int KeyboardWidth = 60, TimelineHeight = 24;
void fillReferenceCurve(QPainter& painter, const QPainterPath& path, QColor color, double baseline)
{
	QPainterPath area;
	QPointF first, last;
	bool connected = false;
	auto finish = [&] {
		if (!connected)
			return;
		area.lineTo(last.x(), baseline);
		area.lineTo(first.x(), baseline);
		area.closeSubpath();
	};
	for (int i = 0; i < path.elementCount(); ++i)
	{
		const auto point = path.elementAt(i);
		if (point.isMoveTo())
		{
			finish();
			first = last = QPointF(point.x, point.y);
			area.moveTo(first);
			connected = true;
		}
		else if (point.isLineTo())
		{
			last = QPointF(point.x, point.y);
			area.lineTo(last);
		}
	}
	finish();
	color.setAlpha(64);
	painter.fillPath(area, color);
}
QString noteLabel(int pitch)
{
	static const QStringList names{"C", "C♯", "D", "D♯", "E", "F", "F♯", "G", "G♯", "A", "A♯", "B"};
	return names[(pitch % 12 + 12) % 12] + QString::number(pitch / 12 - 1);
}
constexpr const char* NoteMime = "application/x-lmms-svs-notes";
constexpr const char* CurveMime = "application/x-lmms-svs-curve";
QJsonObject noteJson(const svs::Note& note)
{
	return {{"id", note.id}, {"tick", note.tick}, {"duration", note.duration}, {"pitch", note.pitch},
		{"lyric", note.lyric}, {"language", note.language}, {"pronunciation", note.pronunciation},
		{"parameters", note.parameters}, {"phonemes", note.phonemes}, {"xmlExtras", note.xmlExtras}};
}
svs::Note noteFromJson(const QJsonObject& object)
{
	svs::Note note;
	note.id = object["id"].toString();
	note.tick = object["tick"].toDouble();
	note.duration = object["duration"].toDouble(48);
	note.pitch = object["pitch"].toDouble(60);
	note.lyric = object["lyric"].toString();
	note.language = object["language"].toString();
	note.pronunciation = object["pronunciation"].toString();
	note.parameters = object["parameters"].toObject();
	note.phonemes = object["phonemes"].toObject();
	note.xmlExtras = object["xmlExtras"].toObject();
	return note;
}
}
SVSCanvas::SVSCanvas(SVSClip* clip, QWidget* parent)
	: QWidget(parent)
	, m_clip(clip)
	, m_transaction(std::make_unique<SVSEditTransaction>(clip))
	, m_curveGesture(std::make_unique<SVSCurveGesture>())
{
	setObjectName("svsNoteCanvas");
	setMinimumSize(240, 180);
	setFocusPolicy(Qt::StrongFocus);
	setMouseTracking(true);
	const auto state = clip->editorState();
	m_scrollTick = state["scrollTick"].toDouble();
	m_topPitch = state["topPitch"].toDouble(72);
	m_pixelsPerTick = 2 * state["horizontalZoom"].toDouble(1);
	m_rowHeight = 12 * state["verticalZoom"].toDouble(2);
	m_quantization = state["quantization"].toDouble(12);
	m_noteLength = state["noteLength"].toDouble();
	m_lastNoteLength = std::max(1., state["lastNoteLength"].toDouble(m_quantization));
	m_timelineBegin = TimePos(int(m_scrollTick));
	m_timeLine = new TimeLineWidget(KeyboardWidth, 0, m_pixelsPerTick * TimePos::ticksPerBar(),
		Engine::getSong()->getTimeline(Song::PlayMode::MidiClip), m_timelineBegin, this);
	m_timeLine->setObjectName("svsClipTimeline");
	m_timeLine->setFixedHeight(TimelineHeight);
	m_timeLine->setSnapSize(m_quantization / TimePos::ticksPerBar());
	connect(this, &SVSCanvas::viewportChanged, this, [this] {
		m_timelineBegin = TimePos(int(m_scrollTick));
		m_timeLine->setPixelsPerBar(m_pixelsPerTick * TimePos::ticksPerBar());
		m_timeLine->setFixedWidth(width());
	});
	connect(&Engine::getSong()->getTimeline(Song::PlayMode::MidiClip), &Timeline::positionChanged, this,
		qOverload<>(&SVSCanvas::update));
	connect(Engine::getSong(), &Song::playbackStateChanged, this, [this] {
		m_timeLine->setPlayheadVisible(!Engine::getSong()->isPlaying() || Engine::getSong()->previewClip() == m_clip);
		update();
	});
	m_lyric = new QLineEdit(this);
	m_lyric->setObjectName("svsInlineLyric");
	m_lyric->hide();
	m_lyric->installEventFilter(this);
	m_autoScroll = new QTimer(this);
	m_autoScroll->setInterval(25);
	connect(m_autoScroll, &QTimer::timeout, this, [this] {
		if (m_action == Action::None || m_action == Action::Pan)
			return;
		const double horizontal = m_pointer.x() > width() - 24 ? 8 / m_pixelsPerTick
			: m_pointer.x() < KeyboardWidth + 24			   ? -8 / m_pixelsPerTick
															   : 0;
		const double vertical = m_pointer.y() > height() - 24 ? -0.4 : m_pointer.y() < TimelineHeight + 24 ? 0.4 : 0;
		if (horizontal || vertical)
		{
			setScroll(m_scrollTick + horizontal, m_topPitch + vertical);
			updateOperation(m_pointer, QApplication::keyboardModifiers());
		}
	});
	connect(clip, &Clip::dataChanged, this, [this] {
		if (m_transaction->active() && m_clip
			&& (m_clip->notes() != m_transaction->originalNotes || m_clip->curves() != m_transaction->originalCurves
				|| parameterOffset() != m_operationOffset))
			cancelOperation();
		QSet<QString> valid;
		if (m_clip)
			for (const auto& note : m_clip->notes())
				if (m_selected.contains(note.id))
					valid.insert(note.id);
		if (m_action == Action::None && valid != m_selected)
		{
			m_selected = valid;
			emit selectionChanged();
		}
		update();
	});
	connect(ConfigManager::inst(), &ConfigManager::valueChanged, this,
		[this](const QString& group, const QString& key, const QString&) {
			if (noteLabels::isSetting(group, key) || (group == "svs" && key == "showVoicePitchRanges")) update();
		});
	connect(clip, &QObject::destroyed, this, [this] {
		cancelOperation();
		m_lyric->hide();
		setEnabled(false);
	});
	connect(&Engine::getSong()->getTimeline(Song::PlayMode::Song), &Timeline::positionChanged, this,
		qOverload<>(&SVSCanvas::update));
	connect(Engine::getSong(), &Song::timeSignatureChanged, this, [this](int, int) { update(); });
	if (auto* piano = getGUI()->pianoRoll()->findChild<PianoRoll*>())
	{
		connect(piano, &PianoRoll::ghostClipSet, this, [this](bool) { update(); });
	}
}
SVSCanvas::~SVSCanvas() = default;

void SVSCanvas::setNoteLength(double ticks)
{
	m_noteLength = std::max(0., ticks);
	if (m_clip)
	{
		auto state = m_clip->editorState();
		state["noteLength"] = m_noteLength;
		m_clip->setEditorState(state);
	}
}
const QVector<svs::Note>& SVSCanvas::displayedNotes() const
{
	static const QVector<svs::Note> empty;
	return m_transaction->active() ? m_transaction->notes
		: m_hasExternalPreview	   ? m_externalPreview
		: m_clip				   ? m_clip->notes()
								   : empty;
}
QPointF SVSCanvas::pointAt(double tick, double pitch) const
{
	return {
		KeyboardWidth + (tick - m_scrollTick) * m_pixelsPerTick, TimelineHeight + (m_topPitch - pitch) * m_rowHeight};
}
QRectF SVSCanvas::noteRect(const svs::Note& note) const
{
	return {pointAt(note.tick, note.pitch), QSizeF(note.duration * m_pixelsPerTick, m_rowHeight)};
}
double SVSCanvas::tickAt(double x) const
{
	return m_scrollTick + (x - KeyboardWidth) / m_pixelsPerTick;
}
double SVSCanvas::pitchAt(double y) const
{
	return m_topPitch - (y - TimelineHeight) / m_rowHeight;
}
QPointF SVSCanvas::curvePointAt(double tick, double value) const
{
	if (!m_parameter)
		return pointAt(tick, value) + QPointF(0, m_rowHeight * .5);
	value = std::clamp(value + parameterOffset(), curveMinimum(), curveMaximum());
	const auto minimum = curveMinimum(), maximum = curveMaximum();
	const auto normalized = m_parameter->scale == "log" && minimum > 0
		? (std::log(std::max(minimum, value)) - std::log(minimum)) / (std::log(maximum) - std::log(minimum))
		: (value - minimum) / std::max(1e-12, maximum - minimum);
	return {pointAt(tick, 0).x(),
		TimelineHeight + 3 + (1 - normalized) * std::max(1., double(height() - TimelineHeight - 6))};
}
void SVSCanvas::setParameterLane(const svs::Parameter& parameter, bool feedback)
{
	if (m_parameter && m_feedback == feedback && m_parameter->id == parameter.id && m_parameter->type == parameter.type
		&& m_parameter->minimum == parameter.minimum && m_parameter->maximum == parameter.maximum
		&& m_parameter->step == parameter.step && m_parameter->scale == parameter.scale
		&& m_parameter->interpolation == parameter.interpolation && m_parameter->choices == parameter.choices
		&& m_parameter->writable == parameter.writable && m_parameter->enabled == parameter.enabled)
	{
		m_parameter = parameter;
		update();
		return;
	}
	cancelOperation();
	m_selectedAnchors.clear();
	m_parameter = parameter;
	m_feedback = feedback;
	m_curveId = parameter.id;
	setObjectName("svsParameterLane." + parameter.id + (feedback ? ".feedback" : ".input"));
	setToolTip(tr("Left drag: edit curve. Right drag: reset to baseline. Shift+right click: curve menu."));
	setMinimumHeight(80);
	update();
}
double SVSCanvas::parameterOffset() const
{
	if (!m_clip || !m_parameter || m_feedback || (m_parameter->type != "float" && m_parameter->type != "int"))
		return 0;
	return m_clip->parameterBase(*m_parameter).toDouble() - m_parameter->defaultValue.toDouble();
}
double SVSCanvas::curveMinimum() const
{
	return !m_parameter ? 0 : m_parameter->type == "enum" || m_parameter->type == "bool" ? 0 : m_parameter->minimum;
}
double SVSCanvas::curveMaximum() const
{
	return !m_parameter				  ? 127
		: m_parameter->type == "enum" ? std::max(0, int(m_parameter->choices.size()) - 1)
		: m_parameter->type == "bool" ? 1
									  : m_parameter->maximum;
}
double SVSCanvas::curveNumber(const QJsonValue& value) const
{
	if (value.isBool())
		return value.toBool() ? 1 : 0;
	if (value.isString() && m_parameter)
	{
		for (int i = 0; i < m_parameter->choices.size(); ++i)
			if (m_parameter->choices[i].toObject()["id"] == value)
				return i;
	}
	return value.toDouble();
}
QJsonValue SVSCanvas::curveValue(double value) const
{
	value = std::clamp(value, curveMinimum(), curveMaximum());
	if (!m_parameter)
		return value;
	if (m_parameter->type == "bool")
		return value >= .5;
	if (m_parameter->type == "enum")
	{
		if (m_parameter->choices.isEmpty())
			return {};
		return m_parameter->choices[std::clamp(int(std::round(value)), 0, int(m_parameter->choices.size()) - 1)]
			.toObject()["id"];
	}
	if (m_parameter->type == "int")
		value = std::clamp(
			m_parameter->minimum + std::round((value - m_parameter->minimum) / m_parameter->step) * m_parameter->step,
			m_parameter->minimum, m_parameter->maximum);
	return value;
}
double SVSCanvas::curveValueAtY(double y) const
{
	if (!m_parameter)
		return std::clamp(pitchAt(y) + .5, 0., 127.);
	const auto normalized
		= std::clamp(1 - (y - TimelineHeight - 3) / std::max(1., double(height() - TimelineHeight - 6)), 0., 1.);
	const auto value = m_parameter->scale == "log" && curveMinimum() > 0
		? std::exp(std::log(curveMinimum()) + normalized * (std::log(curveMaximum()) - std::log(curveMinimum())))
		: curveMinimum() + normalized * (curveMaximum() - curveMinimum());
	return std::clamp(value - parameterOffset(), curveMinimum(), curveMaximum());
}
bool SVSCanvas::curveEditable() const
{
	if (!m_clip || m_feedback || (m_parameter && !m_parameterActive))
		return false;
	const auto* track = static_cast<SVSTrack*>(m_clip->getTrack());
	if (!m_parameter)
		return track->capabilities().pitchInput == "absolute" || track->capabilities().pitchInput == "offset";
	const auto* parameter = track->capabilities().parameter(m_parameter->id, m_parameter->scope);
	return m_parameter->writable && m_parameter->enabled && parameter && parameter->curve && parameter->writable
		&& parameter->enabled;
}
bool SVSCanvas::parameterUsesReference(const svs::Parameter& parameter) const
{
	const auto& capabilities = static_cast<SVSTrack*>(m_clip->getTrack())->capabilities();
	const auto parameters = capabilities.original["parameters"].toArray();
	const bool declared = std::any_of(parameters.begin(), parameters.end(), [&](const auto& entry) {
		const auto object = entry.toObject();
		return object["id"].toString() == parameter.id && object["scope"].toString() == parameter.scope
			&& object["mode"].toString() == "absolute";
	});
	return declared
		&& std::any_of(
			capabilities.feedbackParameters.begin(), capabilities.feedbackParameters.end(), [&](const auto& reference) {
				return reference.id == parameter.id && reference.scope == parameter.scope
					&& reference.type == parameter.type && reference.unit == parameter.unit;
			});
}
svs::Curve SVSCanvas::parameterCurve(const svs::Parameter& parameter, bool feedback) const
{
	svs::Curve curve;
	curve.id = parameter.id;
	curve.scope = parameter.scope;
	curve.type = parameter.type;
	curve.unit = parameter.unit;
	curve.interpolation = parameter.interpolation;
	curve.evaluator.interpolation = curve.interpolation == "step" ? svs_sdk::Interpolation::Step
		: curve.interpolation == "hermite"						  ? svs_sdk::Interpolation::Hermite
																  : svs_sdk::Interpolation::Linear;
	if (feedback)
	{
		if (const auto audio = m_clip->audio())
		{
			QString error;
			if (svs::Curve::fromJson(
					audio->feedback["curves"].toObject()[parameter.id].toObject(), curve, error, &parameter))
				return curve;
			const auto value = audio->feedback["parameters"].toObject()[parameter.id];
			if (parameter.accepts(value))
			{
				curve.insert(0, value);
				curve.insert(int(m_clip->length()), value);
			}
		}
	}
	else
	{
		const auto& curves = m_transaction->active() ? m_transaction->curves : m_clip->curves();
		if (curves.contains(parameter.id))
			return curves[parameter.id];
		// Predicted absolute values are display defaults, not authored overrides.
		// Keep an empty source so drawing only stores the edited interval.
		if (parameterUsesReference(parameter))
			return curve;
		const auto base = parameter.type == "float" || parameter.type == "int" ? parameter.defaultValue
																			   : m_clip->parameterBase(parameter);
		curve.insert(0, base);
		curve.insert(std::max(int(m_clip->length()), int(std::ceil(tickAt(width())))), base);
	}
	return curve;
}
svs::Curve SVSCanvas::pitchCurve() const
{
	if (m_parameter)
		return parameterCurve(*m_parameter, m_feedback);
	const auto& curves = m_transaction->active() ? m_transaction->curves : m_clip->curves();
	if (curves.contains(m_curveId))
		return curves[m_curveId];
	svs::Curve curve;
	curve.id = m_curveId;
	curve.unit = "semitone";
	curve.mode = "absolute";
	curve.type = "float";
	curve.interpolation = "hermite";
	curve.evaluator.interpolation = svs_sdk::Interpolation::Hermite;
	return curve;
}
void SVSCanvas::paintParameterOverlays(QPainter& painter)
{
	if (!m_clip || !m_parameter)
		return;
	for (const auto& entry : m_parameterOverlays)
	{
		const auto& parameter = entry.first;
		const bool readOnly = entry.second || !parameter.writable;
		auto curve = parameterCurve(parameter, entry.second);
		if (!entry.second)
			curve = svs::withParameterBase(curve, parameter, m_clip->parameterBase(parameter));
		const auto reference
			= !entry.second && parameterUsesReference(parameter) ? parameterCurve(parameter, true) : svs::Curve{};
		const double minimum = parameter.type == "enum" || parameter.type == "bool" ? 0 : parameter.minimum;
		const double maximum = parameter.type == "enum" ? std::max(0, int(parameter.choices.size()) - 1)
			: parameter.type == "bool"					? 1
														: parameter.maximum;
		QPainterPath path;
		bool connected = false;
		for (double tick = std::max(0., tickAt(KeyboardWidth)); tick <= tickAt(width()); tick += 1 / m_pixelsPerTick)
		{
			auto value = curve.valueAt(tick);
			if (!value)
				value = reference.valueAt(tick);
			if (!value)
			{
				connected = false;
				continue;
			}
			double number = value->isBool() ? (value->toBool() ? 1 : 0) : value->toDouble();
			if (parameter.type == "enum")
				for (int i = 0; i < parameter.choices.size(); ++i)
					if (parameter.choices[i].toObject()["id"] == *value)
					{
						number = i;
						break;
					}
			const auto normalized = parameter.scale == "log" && minimum > 0
				? (std::log(std::max(minimum, number)) - std::log(minimum))
					/ std::max(1e-12, std::log(maximum) - std::log(minimum))
				: (number - minimum) / std::max(1e-12, maximum - minimum);
			const QPointF point(pointAt(tick, 0).x(),
				TimelineHeight + 3 + (1 - normalized) * std::max(1., double(height() - TimelineHeight - 6)));
			if (connected)
				path.lineTo(point);
			else
				path.moveTo(point);
			connected = true;
		}
		const QColor declared(parameter.color);
		const auto lineColor = declared.isValid() ? declared : color("userPitchColor", QPalette::Highlight);
		if (readOnly)
			fillReferenceCurve(painter, path, lineColor, height() - 3);
		painter.setPen(QPen(lineColor, 1));
		painter.drawPath(path);
	}
}
void SVSCanvas::paintPitch(QPainter& painter)
{
	if (!m_clip || (m_parameter && !m_parameterActive))
		return;
	const bool readOnly = m_parameter && (m_feedback || !m_parameter->writable);
	if (readOnly && !m_referenceVisible)
		return;
	const auto curve = pitchCurve();
	const auto displayed = m_parameter && !m_feedback
		? svs::withParameterBase(curve, *m_parameter, m_clip->parameterBase(*m_parameter))
		: curve;
	const auto reference = m_parameter && !m_feedback && parameterUsesReference(*m_parameter)
		? parameterCurve(*m_parameter, true)
		: svs::Curve{};
	QPainterPath path;
	bool connected = false;
	const double from = std::max(0., tickAt(KeyboardWidth)), to = tickAt(width());
	// The visible interval alone determines rendering cost, independent of song length.
	for (double tick = from; tick <= to; tick += 1 / m_pixelsPerTick)
	{
		auto value = displayed.valueAt(tick);
		if (!value)
			value = reference.valueAt(tick);
		if (!value)
		{
			connected = false;
			continue;
		}
		auto point = curvePointAt(tick, curveNumber(*value) - parameterOffset());
		if (connected)
			path.lineTo(point);
		else
			path.moveTo(point);
		connected = true;
	}
	const QColor declared(m_parameter ? m_parameter->color : QString{});
	auto curveColor = declared.isValid() ? declared : color("userPitchColor", QPalette::Highlight);
	if (!m_parameter && noteTool())
		curveColor.setAlphaF(.5);
	if (readOnly)
		fillReferenceCurve(painter, path, curveColor, height() - 3);
	painter.setPen(QPen(curveColor, 2));
	painter.drawPath(path);
	if (effectiveTool() == Tool::Anchor)
	{
		for (const auto& anchor : curve.evaluator.points)
		{
			auto point = curvePointAt(
				anchor.tick, curve.type == "enum" ? curveNumber(QString::fromStdString(anchor.valueId)) : anchor.value);
			if (!rect().contains(point.toPoint()))
				continue;
			painter.setBrush(m_selectedAnchors.contains(anchor.tick) ? color("selectedNoteColor", QPalette::Highlight)
																	 : palette().base().color());
			painter.drawEllipse(point, 4, 4);
			if (m_selectedAnchors.contains(anchor.tick) && curve.type == "float")
			{
				const auto index = size_t(&anchor - curve.evaluator.points.data());
				const auto in = anchor.automatic ? curve.evaluator.automaticTangent(index) : anchor.tangentIn,
						   out = anchor.automatic ? curve.evaluator.automaticTangent(index) : anchor.tangentOut;
				const auto dt = 30 / m_pixelsPerTick;
				auto left = curvePointAt(anchor.tick - dt, anchor.value - in * dt),
					 right = curvePointAt(anchor.tick + dt, anchor.value + out * dt);
				painter.drawLine(left, point);
				painter.drawLine(point, right);
				painter.drawRect(QRectF(left - QPointF(3, 3), QSizeF(6, 6)));
				painter.drawRect(QRectF(right - QPointF(3, 3), QSizeF(6, 6)));
			}
		}
	}
	if (const auto audio = m_clip->audio(); audio && !m_parameter && m_referenceVisible)
	{
		painter.setPen(QPen(color("synthesizedPitchColor", QPalette::Text), 1));
		for (const auto& feedback : feedbackPitchCurves(audio->feedback["pitch"].toArray(), audio->mapping))
		{
			QPainterPath feedbackPath;
			bool have = false;
			const auto first = std::max(from, feedback.evaluator.points.front().tick),
					   last = std::min(to, feedback.evaluator.points.back().tick);
			for (double tick = first; tick <= last; tick += 1 / m_pixelsPerTick)
			{
				const auto value = feedback.valueAt(tick);
				if (!value)
				{
					have = false;
					continue;
				}
				const auto point = curvePointAt(tick, value->toDouble());
				if (have)
					feedbackPath.lineTo(point);
				else
					feedbackPath.moveTo(point);
				have = true;
			}
			if (have)
				feedbackPath.lineTo(curvePointAt(last, feedback.valueAt(last)->toDouble()));
			painter.drawPath(feedbackPath);
		}
	}
}
void SVSCanvas::beginCurveStroke(const QPointF& point)
{
	m_transaction->begin();
	const auto tool = effectiveTool();
	const auto kind = tool == Tool::Freehand ? SVSCurveGesture::Kind::Freehand
		: tool == Tool::Line				 ? SVSCurveGesture::Kind::Line
		: tool == Tool::Smooth				 ? SVSCurveGesture::Kind::Smooth
											 : SVSCurveGesture::Kind::Erase;
	m_curveGesture->begin(pitchCurve(), std::max(0., tickAt(point.x())), curveValue(curveValueAtY(point.y())), kind);
	m_action = Action::CurveStroke;
	updateOperation(point, Qt::NoModifier);
}
QColor SVSCanvas::color(const QString& key, QPalette::ColorRole role) const
{
	return m_colors.contains(key) && m_colors[key].isValid() ? m_colors[key] : palette().color(role);
}
double SVSCanvas::snap(double tick, Qt::KeyboardModifiers modifiers) const
{
	if (modifiers.testFlag(Qt::AltModifier) || m_quantization <= 0)
		return tick;
	return std::round(tick / m_quantization) * m_quantization;
}
QString SVSCanvas::hitNote(const QPointF& point) const
{
	if (!m_clip || point.x() < KeyboardWidth || point.y() < TimelineHeight)
		return {};
	const auto& notes = displayedNotes();
	for (auto i = notes.crbegin(); i != notes.crend(); ++i)
		if (noteRect(*i).contains(point))
			return i->id;
	return {};
}
void SVSCanvas::setTool(Tool value)
{
	if (m_tool == value)
		return;
	cancelOperation();
	finishLyric(true);
	m_tool = value;
	setCursor(noteTool()		   ? (value == Tool::Pencil ? Qt::CrossCursor : Qt::ArrowCursor)
			: value == Tool::Erase ? Qt::ForbiddenCursor
								   : Qt::CrossCursor);
	emit toolChanged();
	update();
}
void SVSCanvas::setQuantization(double value)
{
	m_quantization = std::max(0., value);
	m_timeLine->setSnapSize(value / TimePos::ticksPerBar());
	rememberViewport();
	update();
}
void SVSCanvas::setZoom(double horizontal, double vertical)
{
	m_pixelsPerTick = 2 * std::clamp(horizontal, .015625, 16.);
	m_rowHeight = 12 * std::clamp(vertical, .5, 4.);
	rememberViewport();
	emit viewportChanged();
	update();
}
void SVSCanvas::setScroll(double tick, double topPitch)
{
	m_scrollTick = std::max(0., tick);
	m_topPitch = std::clamp(topPitch, 0., 127.);
	rememberViewport();
	emit viewportChanged();
	update();
}
void SVSCanvas::rememberViewport()
{
	if (!m_clip)
		return;
	auto state = m_clip->editorState();
	state["scrollTick"] = m_scrollTick;
	if (!m_parameter)
		state["topPitch"] = m_topPitch;
	state["horizontalZoom"] = horizontalZoom();
	if (!m_parameter)
	{
		state["verticalZoom"] = verticalZoom();
		state["quantization"] = m_quantization;
	}
	m_clip->setEditorState(state);
}
void SVSCanvas::setPortrait(const QImage& image, bool visible, int transparency, QPointF position)
{
	m_portrait = image;
	m_showPortrait = visible;
	m_transparency = std::clamp(transparency, 0, 100);
	m_portraitPosition = {std::clamp(position.x(), 0., 1.), std::clamp(position.y(), 0., 1.)};
	update();
}
QSize SVSCanvas::portraitTargetSize() const
{
	return QSize(std::max(1, int((width() - KeyboardWidth) * .55 * devicePixelRatioF())),
		std::max(1, int((height() - TimelineHeight) * .9 * devicePixelRatioF())));
}
void SVSCanvas::resizeEvent(QResizeEvent* event)
{
	QWidget::resizeEvent(event);
	emit portraitSizeChanged();
	emit viewportChanged();
}
double SVSCanvas::playbackTick() const
{
	auto* song = Engine::getSong();
	if (song->isPlaying() && song->playMode() == Song::PlayMode::Song && m_clip)
	{
		return song->getTimeline(Song::PlayMode::Song).ticks() - int(m_clip->startPosition())
			- int(m_clip->startTimeOffset());
	}
	return song->getTimeline(Song::PlayMode::MidiClip).ticks();
}
void SVSCanvas::paintEvent(QPaintEvent*)
{
	QPainter painter(this);
	painter.fillRect(rect(), color("backgroundColor", QPalette::Base));
	const bool allNoteLabels = ConfigManager::inst()->value("ui", "printnotelabels").toInt() != 0;
	const QRectF grid(KeyboardWidth, TimelineHeight, width() - KeyboardWidth, height() - TimelineHeight);
	painter.save();
	painter.setClipRect(grid);
	painter.setPen(color("gridLineColor", QPalette::Mid));
	const double step = std::max(1., m_quantization);
	const double first = std::floor(m_scrollTick / step) * step;
	for (double tick = first; tick <= tickAt(width()); tick += step)
	{
		auto x = pointAt(tick, 0).x();
		painter.drawLine(QPointF(x, TimelineHeight), QPointF(x, height()));
	}
	for (int row = 0; row < (height() - TimelineHeight) / m_rowHeight + 2; ++row)
	{
		auto y = TimelineHeight + row * m_rowHeight;
		painter.drawLine(QPointF(KeyboardWidth, y), QPointF(width(), y));
	}
	// Like the instrument piano roll, the grid uses clip content time.
	auto drawTimeGrid = [&](double ticks, const QColor& lineColor) {
		if (ticks <= 0)
			return;
		painter.setPen(lineColor);
		for (double projectTick = std::floor(m_scrollTick / ticks) * ticks; projectTick <= tickAt(width());
			projectTick += ticks)
		{
			const auto x = pointAt(projectTick, 0).x();
			painter.drawLine(QPointF(x, TimelineHeight), QPointF(x, height()));
		}
	};
	drawTimeGrid(double(DefaultTicksPerBar) / Engine::getSong()->getTimeSigModel().getDenominator(),
		color("beatLineColor", QPalette::Midlight));
	drawTimeGrid(TimePos::ticksPerBar(), color("barLineColor", QPalette::Highlight));
	if (!m_parameter && m_showPortrait && !m_portrait.isNull() && m_transparency < 100)
	{
		const auto size
			= m_portrait.size().scaled(QSize(int(grid.width() * .55), int(grid.height() * .9)), Qt::KeepAspectRatio);
		painter.setOpacity(1. - m_transparency / 100.);
		painter.drawImage(
			QRectF(grid.left() + (grid.width() - size.width()) * m_portraitPosition.x(),
				grid.top() + (grid.height() - size.height()) * m_portraitPosition.y(), size.width(), size.height()),
			m_portrait);
		painter.setOpacity(1);
	}
	if (m_clip && !m_parameter)
	{
		if (auto* piano = getGUI()->pianoRoll()->findChild<PianoRoll*>())
		{
			painter.save();
			painter.setClipRect(grid);
			auto ghostColor = piano->property("ghostNoteColor").value<QColor>();
			ghostColor.setAlpha(piano->property("ghostNoteOpacity").toInt());
			for (const auto* ghost : piano->ghostNotes())
			{
				svs::Note reference;
				reference.tick = int(ghost->pos());
				reference.duration = int(ghost->length());
				reference.pitch = ghost->key();
				const auto rectangle = noteRect(reference).adjusted(1, 1, -1, -1);
				painter.fillRect(rectangle, ghostColor);
				if (allNoteLabels)
				{
					painter.setPen(piano->property("ghostNoteTextColor").value<QColor>());
					painter.drawText(rectangle.adjusted(3, 0, -3, 0), Qt::AlignVCenter | Qt::AlignLeft,
						noteLabel(ghost->key()));
				}
			}
			painter.restore();
		}
		const auto audio = m_clip->audio();
		const auto readings = audio ? audio->feedback["pronunciations"].toObject() : QJsonObject{};
		for (const auto& note : displayedNotes())
		{
			auto rectangle = noteRect(note);
			// The cached synthesis waveform follows the note's time span and pitch row.
			const double waveHeight = std::clamp(m_rowHeight, 8., 32.);
			const QRectF wave(rectangle.left(), rectangle.bottom() + 2, rectangle.width(), waveHeight);
			if (!rectangle.united(wave).intersects(grid))
				continue;
			if (audio && wave.intersects(grid))
			{
				painter.save();
				painter.setClipRect(wave, Qt::IntersectClip);
				painter.setPen(QPen(color("waveformColor", QPalette::Highlight), 2));
				const int left = std::max(KeyboardWidth, int(std::ceil(wave.left()))),
						  right = std::min(width(), int(std::ceil(wave.right())));
				for (int x = left; x < right; ++x)
				{
					const auto from
						= audio->mapping.samplePosition(std::max(note.tick, tickAt(x)), audio->startTick, audio->rate);
					const auto to = audio->mapping.samplePosition(
						std::min(note.tick + note.duration, tickAt(x + 1)), audio->startTick, audio->rate);
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
							peak.minimum
								= std::min({peak.minimum, audio->samples[frame * 2], audio->samples[frame * 2 + 1]});
							peak.maximum
								= std::max({peak.maximum, audio->samples[frame * 2], audio->samples[frame * 2 + 1]});
						}
					}
					const double center = wave.center().y(), amplitude = wave.height() * .5;
					painter.drawLine(QPointF(x, center - std::clamp(double(peak.maximum), -1., 1.) * amplitude),
						QPointF(x, center - std::clamp(double(peak.minimum), -1., 1.) * amplitude));
				}
				painter.restore();
			}
			auto noteColor = m_selected.contains(note.id) ? color("selectedNoteColor", QPalette::Highlight).lighter(125)
														  : color("noteColor", QPalette::Highlight);
			if (!noteTool())
				noteColor.setAlphaF(.5);
			const auto body = rectangle.adjusted(0, 1, -1, -1);
			if (m_noteCornerRadius > 0 && body.width() > 0 && body.height() > 0)
			{
				painter.save();
				painter.setRenderHint(QPainter::Antialiasing);
				painter.setPen(Qt::NoPen);
				painter.setBrush(noteColor);
				const auto radius = std::min(m_noteCornerRadius, std::min(body.width(), body.height()) / 2);
				painter.drawRoundedRect(body, radius, radius);
				painter.restore();
			}
			else
			{
				painter.fillRect(body, noteColor);
			}
			painter.setPen(color("lyricColor", QPalette::HighlightedText));
			painter.drawText(rectangle.adjusted(3, 0, -3, 0), Qt::AlignVCenter | Qt::AlignLeft,
				fontMetrics().elidedText(note.lyric, Qt::ElideRight, int(rectangle.width() - 6)));
			const auto reading
				= note.pronunciation.isEmpty() ? readings[note.id].toObject()["text"].toString() : note.pronunciation;
			const auto recording = m_clip->editorState()["pitchPredictionRequests"].toObject()[note.id].toObject();
			const auto takeLabel
				= recording.contains("take") ? tr("Take %1").arg(recording["take"].toInt()) : QString{};
			const auto labelRectangle = rectangle.translated(0, -m_rowHeight).adjusted(3, 0, -3, 0);
			const int takeWidth = takeLabel.isEmpty() ? 0 : fontMetrics().horizontalAdvance(takeLabel) + 6;
			painter.setPen(color("pronunciationColor", QPalette::Text));
			if (!takeLabel.isEmpty())
			{
				painter.drawText(labelRectangle, Qt::AlignBottom | Qt::AlignRight,
					fontMetrics().elidedText(takeLabel, Qt::ElideLeft, std::max(0, int(labelRectangle.width()))));
			}
			if (!reading.isEmpty())
			{
				painter.setPen(color("pronunciationColor", QPalette::Text));
				painter.drawText(labelRectangle.adjusted(0, 0, -takeWidth, 0), Qt::AlignBottom | Qt::AlignLeft,
					fontMetrics().elidedText(
						reading, Qt::ElideRight, std::max(0, int(labelRectangle.width()) - takeWidth)));
			}
		}
	}
	if (m_action == Action::Frame)
	{
		painter.setPen(color("selectedNoteColor", QPalette::Highlight));
		auto fill = color("selectedNoteColor", QPalette::Highlight);
		fill.setAlpha(40);
		painter.fillRect(m_frame, fill);
		painter.drawRect(m_frame);
	}
	painter.save();
	painter.setRenderHint(QPainter::Antialiasing);
	paintParameterOverlays(painter);
	paintPitch(painter);
	painter.restore();
	if (m_action == Action::CurveFrame)
	{
		painter.setPen(color("selectedNoteColor", QPalette::Highlight));
		painter.drawRect(m_frame);
	}
	painter.restore();
	// Keyboard and ruler remain fixed while the shared content coordinates scroll.
	painter.fillRect(QRect(0, TimelineHeight, KeyboardWidth, height() - TimelineHeight), palette().window());
	if (m_parameter)
	{
		painter.setPen(palette().windowText().color());
		if (m_parameterActive)
		{
			painter.drawText(QRect(2, TimelineHeight, KeyboardWidth - 4, 20), Qt::AlignLeft,
				QString::number(curveMaximum(), 'g', 4));
			painter.drawText(
				QRect(2, height() - 22, KeyboardWidth - 4, 20), Qt::AlignLeft, QString::number(curveMinimum(), 'g', 4));
		}
	}
	else
	{
		painter.save();
		painter.setClipRect(QRect(0, TimelineHeight, KeyboardWidth, height() - TimelineHeight));
		QFont font = painter.font();
		font.setPixelSize(std::max(1, int(m_rowHeight * .8)));
		painter.setFont(font);
		const auto white = color("whiteKeyInactiveBackground", QPalette::Light),
				   black = color("blackKeyInactiveBackground", QPalette::Dark);
		const auto ranges = SVSPitchRanges::fromMetadata(
			m_clip && ConfigManager::inst()->value("svs", "showVoicePitchRanges", "1").toInt() != 0
				? static_cast<SVSTrack*>(m_clip->getTrack())->voice().metadata
				: QJsonObject{});
		const auto text = m_colors.contains("whiteKeyInactiveTextColor")
			? m_colors["whiteKeyInactiveTextColor"]
			: QColor(white.lightnessF() > .5 ? Qt::black : Qt::white);
		// Native PianoRoll geometry: small white keys span 1.5 rows, D/G/A
		// span two rows. Draw white keys first, then the shorter black keys.
		for (bool drawBlack : {false, true})
			for (int row = -1; row < (height() - TimelineHeight) / m_rowHeight + 2; ++row)
			{
				const int pitch = int(std::floor(m_topPitch)) - row;
				if (pitch < 0 || pitch > 127)
					continue;
				const int key = pitch % 12;
				const bool isBlack = key == 1 || key == 3 || key == 6 || key == 8 || key == 10;
				if (isBlack != drawBlack)
					continue;
				const auto y = TimelineHeight + (m_topPitch - pitch) * m_rowHeight;
				const bool big = key == 2 || key == 7 || key == 9;
				const auto smallHeight = std::floor(m_rowHeight * 1.5);
				const auto correction = isBlack		? m_rowHeight
					: (big || key == 0 || key == 5) ? smallHeight
													: m_rowHeight;
				const auto keyHeight = isBlack ? m_rowHeight : big ? 2 * m_rowHeight : smallHeight;
				painter.setPen(Qt::black);
				painter.setBrush(ranges.keyColor(isBlack ? black : white, pitch));
				painter.drawRect(QRectF(0, y + m_rowHeight - 1 - correction,
					isBlack ? KeyboardWidth * .75 - 1 : KeyboardWidth - 1, keyHeight));
				if (allNoteLabels)
				{
					painter.setPen(isBlack ? m_colors.value("blackKeyTextColor", QColor(Qt::white)) : text);
					painter.drawText(
						QRectF(0, y, (isBlack ? KeyboardWidth * .75 : KeyboardWidth) - 3, m_rowHeight),
						Qt::AlignRight | Qt::AlignVCenter, noteLabel(pitch));
				}
				else if (!isBlack && key == 0)
				{
					painter.setPen(text);
					painter.drawText(QRectF(0, y, KeyboardWidth - 3, m_rowHeight), Qt::AlignRight | Qt::AlignVCenter,
						noteLabel(pitch));
				}
			}
		painter.restore();
	}

	painter.fillRect(QRect(0, 0, width(), TimelineHeight), palette().window());
	painter.setPen(palette().windowText().color());
	const auto bar = TimePos::ticksPerBar();
	for (int index = int(std::floor(m_scrollTick / bar)); index <= tickAt(width()) / bar; ++index)
	{
		const auto x = pointAt(index * bar, 0).x();
		if (x < KeyboardWidth)
			continue;
		painter.drawText(QRectF(x + 3, 0, 80, TimelineHeight), Qt::AlignVCenter, QString::number(index + 1));
	}
	if (m_clip)
	{
		const double local = playbackTick();
		const auto x = pointAt(local, 0).x();
		const auto projectTick = Engine::getSong()->getTimeline(Song::PlayMode::Song).ticks();
		const bool inClip = projectTick >= int(m_clip->startPosition()) && projectTick < int(m_clip->endPosition());
		if (x >= KeyboardWidth && x <= width()
			&& (!Engine::getSong()->isPlaying() || Engine::getSong()->previewClip() == m_clip
				|| (Engine::getSong()->playMode() == Song::PlayMode::Song && inClip)))
		{
			painter.setPen(color("userPitchColor", QPalette::Highlight));
			painter.drawLine(QPointF(x, 0), QPointF(x, height()));
		}
	}
	if (m_clip && !m_parameter && underMouse())
	{
		const QPoint pointer = mapFromGlobal(QCursor::pos());
		noteLabels::drawAlignment(painter, grid, pointer, int(std::ceil(pitchAt(pointer.y()))),
			tickAt(pointer.x()), TimePos::ticksPerBar(),
			double(DefaultTicksPerBar) / Engine::getSong()->getTimeSigModel().getDenominator(), palette(),
			m_colors.value("pitchAlignmentLineColor"), m_colors.value("timeAlignmentLineColor"));
	}
}
void SVSCanvas::mousePressEvent(QMouseEvent* event)
{
	if (!m_clip)
		return;
	setFocus();
	m_pointer = event->position();
	if (event->button() == Qt::MiddleButton)
	{
		cancelOperation();
		m_action = Action::Pan;
		m_begin = event->position();
		m_panTick = m_scrollTick;
		m_panPitch = m_topPitch;
		grabMouse();
		m_mouseCaptured = true;
		return;
	}
	if (event->button() == Qt::RightButton && m_parameter && !event->modifiers().testFlag(Qt::ShiftModifier)
		&& event->position().x() >= KeyboardWidth && event->position().y() >= TimelineHeight)
	{
		cancelOperation();
		finishLyric(true);
		if (!curveEditable())
			return;
		m_initialAnchors = m_selectedAnchors;
		m_initialSelection = m_selected;
		m_operationOffset = parameterOffset();
		m_transaction->begin();
		m_curveGesture->begin(pitchCurve(), std::max(0., tickAt(event->position().x())), m_parameter->defaultValue,
			parameterUsesReference(*m_parameter) ? SVSCurveGesture::Kind::Erase : SVSCurveGesture::Kind::Reset);
		m_action = Action::CurveReset;
		updateOperation(event->position(), event->modifiers());
		grabMouse();
		m_mouseCaptured = true;
		m_autoScroll->start();
		return;
	}
	if (event->button() != Qt::LeftButton)
		return;
	finishLyric(true);
	if (event->position().y() < TimelineHeight && event->position().x() >= KeyboardWidth)
	{
		const double begin = std::max(0, -int(m_clip->startTimeOffset()));
		const double end = int(m_clip->length()) - int(m_clip->startTimeOffset());
		const auto local = std::clamp(snap(tickAt(event->position().x()), event->modifiers()), begin, end);
		Engine::getSong()->getTimeline(Song::PlayMode::MidiClip).setTicks(int(local));
		update();
		return;
	}
	if (event->position().x() < KeyboardWidth)
		return;
	if (m_parameter || !noteTool())
	{
		if (!curveEditable())
			return;
		m_operationOffset = parameterOffset();
		m_initialAnchors = m_selectedAnchors;
		m_initialSelection = m_selected;
		m_begin = event->position();
		m_beginTick = tickAt(m_begin.x());
		m_beginPitch = curveValueAtY(m_begin.y());
		if (effectiveTool() == Tool::Anchor)
		{
			const auto curve = pitchCurve();
			bool hit = false;
			for (size_t index = 0; index < curve.evaluator.points.size() && !hit; ++index)
			{
				const auto& anchor = curve.evaluator.points[index];
				auto point = curvePointAt(anchor.tick,
					curve.type == "enum" ? curveNumber(QString::fromStdString(anchor.valueId)) : anchor.value);
				const auto dt = 30 / m_pixelsPerTick;
				if (m_selectedAnchors.contains(anchor.tick) && curve.type == "float")
				{
					auto in = anchor.automatic ? curve.evaluator.automaticTangent(index) : anchor.tangentIn,
						 out = anchor.automatic ? curve.evaluator.automaticTangent(index) : anchor.tangentOut;
					for (const auto& handle : QVector<QPair<QPointF, Action>>{
							 {curvePointAt(anchor.tick - dt, anchor.value - in * dt), Action::TangentIn},
							 {curvePointAt(anchor.tick + dt, anchor.value + out * dt), Action::TangentOut}})
						if (QLineF(handle.first, m_begin).length() <= 6)
						{
							m_anchorTick = anchor.tick;
							m_action = handle.second;
							hit = true;
							break;
						}
				}
				if (!hit && QLineF(point, m_begin).length() <= 6)
				{
					if (!event->modifiers().testFlag(Qt::ControlModifier) && !m_selectedAnchors.contains(anchor.tick))
						m_selectedAnchors.clear();
					m_selectedAnchors.insert(anchor.tick);
					m_anchorTick = anchor.tick;
					m_action = Action::CurveMove;
					hit = true;
				}
			}
			if (hit)
			{
				m_dragAnchors = m_selectedAnchors;
				m_transaction->begin();
			}
			else
			{
				if (!event->modifiers().testFlag(Qt::ControlModifier))
					m_selectedAnchors.clear();
				m_action = Action::CurveFrame;
				m_frame = QRectF(m_begin, m_begin);
			}
		}
		else
			beginCurveStroke(event->position());
		grabMouse();
		m_mouseCaptured = true;
		m_autoScroll->start();
		update();
		return;
	}
	m_begin = event->position();
	m_beginTick = snap(tickAt(m_begin.x()), event->modifiers());
	m_beginPitch = std::ceil(pitchAt(m_begin.y()));
	m_initialSelection = m_selected;
	m_hitId = hitNote(event->position());
	if (m_hitId.isEmpty() && m_tool == Tool::Pencil && !event->modifiers().testFlag(Qt::ControlModifier))
	{
		beginNote(event->position(), event->modifiers());
		return;
	}
	if (m_hitId.isEmpty())
	{
		m_beginTick = tickAt(m_begin.x());
		m_beginPitch = pitchAt(m_begin.y());
		if (!event->modifiers().testFlag(Qt::ControlModifier))
			m_selected.clear();
		m_action = Action::Frame;
		m_frame = QRectF(m_begin, m_begin);
	}
	else
	{
		if (event->modifiers().testFlag(Qt::ControlModifier))
		{
			if (m_selected.contains(m_hitId))
			{
				m_selected.remove(m_hitId);
				emit selectionChanged();
				update();
				return;
			}
			m_selected.insert(m_hitId);
		}
		else if (!m_selected.contains(m_hitId))
			m_selected = {m_hitId};
		m_transaction->begin();
		m_action = Action::Move;
		for (const auto& note : m_transaction->originalNotes)
			if (note.id == m_hitId)
			{
				const auto r = noteRect(note);
				const auto edge = std::min(5., r.width() / 3);
				if (m_begin.x() - r.left() < edge)
					m_action = Action::LeftEdge;
				else if (r.right() - m_begin.x() < edge)
					m_action = Action::RightEdge;
				break;
			}
	}
	emit selectionChanged();
	grabMouse();
	m_mouseCaptured = true;
	m_autoScroll->start();
	update();
}
void SVSCanvas::updateOperation(const QPointF& point, Qt::KeyboardModifiers modifiers)
{
	if (m_action == Action::Pan)
	{
		setScroll(m_panTick - (point.x() - m_begin.x()) / m_pixelsPerTick,
			m_panPitch + (point.y() - m_begin.y()) / m_rowHeight);
		return;
	}
	if (m_action == Action::Frame)
	{
		m_frame = QRectF(pointAt(m_beginTick, m_beginPitch), point).normalized();
		m_selected = modifiers.testFlag(Qt::ControlModifier) ? m_initialSelection : QSet<QString>{};
		for (const auto& note : m_clip->notes())
			if (m_frame.intersects(noteRect(note)))
				m_selected.insert(note.id);
		emit selectionChanged();
		update();
		return;
	}
	if (m_action == Action::CurveFrame)
	{
		m_frame = QRectF(curvePointAt(m_beginTick, m_beginPitch), point).normalized();
		m_selectedAnchors = modifiers.testFlag(Qt::ControlModifier) ? m_initialAnchors : QSet<double>{};
		const auto curve = pitchCurve();
		for (const auto& anchor : curve.evaluator.points)
			if (m_frame.contains(curvePointAt(anchor.tick,
					curve.type == "enum" ? curveNumber(QString::fromStdString(anchor.valueId)) : anchor.value)))
				m_selectedAnchors.insert(anchor.tick);
		update();
		return;
	}
	if (m_action == Action::CurveStroke || m_action == Action::CurveReset)
	{
		m_curveGesture->update(std::max(0., tickAt(point.x())), curveValue(curveValueAtY(point.y())));
		m_transaction->curves[m_curveId] = m_curveGesture->preview;
		update();
		return;
	}
	if (m_action == Action::CurveMove || m_action == Action::TangentIn || m_action == Action::TangentOut)
	{
		auto curve = m_transaction->originalCurves.value(m_curveId);
		double timeDelta = tickAt(point.x()) - m_beginTick, valueDelta = curveValueAtY(point.y()) - m_beginPitch;
		double minimumTick = INFINITY, minimumValue = curveMaximum(), maximumValue = curveMinimum();
		for (const auto& anchor : curve.evaluator.points)
			if (m_dragAnchors.contains(anchor.tick))
			{
				minimumTick = std::min(minimumTick, anchor.tick);
				const auto value
					= curve.type == "enum" ? curveNumber(QString::fromStdString(anchor.valueId)) : anchor.value;
				minimumValue = std::min(minimumValue, value);
				maximumValue = std::max(maximumValue, value);
			}
		timeDelta = std::max(timeDelta, -minimumTick);
		valueDelta = std::clamp(valueDelta, curveMinimum() - minimumValue, curveMaximum() - maximumValue);
		if (m_action == Action::CurveMove)
		{
			double low = -minimumTick, high = INFINITY;
			for (const auto& anchor : curve.evaluator.points)
				if (m_dragAnchors.contains(anchor.tick))
					for (const auto& neighbor : curve.evaluator.points)
						if (!m_dragAnchors.contains(neighbor.tick))
						{
							if (neighbor.tick < anchor.tick)
								low = std::max(low, std::nextafter(neighbor.tick, INFINITY) - anchor.tick);
							else
								high = std::min(high, std::nextafter(neighbor.tick, -INFINITY) - anchor.tick);
						}
			timeDelta = std::clamp(timeDelta, low, high);
			m_selectedAnchors.clear();
			for (auto& anchor : curve.evaluator.points)
				if (m_dragAnchors.contains(anchor.tick))
				{
					anchor.tick += timeDelta;
					const auto value = curveValue(
						(curve.type == "enum" ? curveNumber(QString::fromStdString(anchor.valueId)) : anchor.value)
						+ valueDelta);
					if (value.isString())
						anchor.valueId = value.toString().toStdString();
					else
						anchor.value = curveNumber(value);
					m_selectedAnchors.insert(anchor.tick);
				}
			std::stable_sort(curve.evaluator.points.begin(), curve.evaluator.points.end(),
				[](const auto& a, const auto& b) { return a.tick < b.tick; });
			curve.evaluator.points.erase(std::unique(curve.evaluator.points.begin(), curve.evaluator.points.end(),
											 [](const auto& a, const auto& b) { return a.tick == b.tick; }),
				curve.evaluator.points.end());
		}
		else
			for (auto& anchor : curve.evaluator.points)
				if (anchor.tick == m_anchorTick)
				{
					auto dt = tickAt(point.x()) - anchor.tick;
					if ((m_action == Action::TangentIn && dt < 0) || (m_action == Action::TangentOut && dt > 0))
					{
						const auto slope = (curveValueAtY(point.y()) - curveNumber(curveValue(anchor.value))) / dt;
						anchor.automatic = false;
						if (modifiers.testFlag(Qt::AltModifier))
						{
							if (m_action == Action::TangentIn)
								anchor.tangentIn = slope;
							else
								anchor.tangentOut = slope;
						}
						else
							anchor.tangentIn = anchor.tangentOut = slope;
					}
				}
		m_transaction->curves[m_curveId] = curve;
		update();
		return;
	}
	if (!m_transaction->active())
		return;
	m_transaction->notes = m_transaction->originalNotes;
	if (m_action == Action::CreateTail)
	{
		auto note = m_createdNote;
		const double minimum = modifiers.testFlag(Qt::AltModifier) ? 1. : std::max(1., m_quantization);
		if (std::abs(point.x() - m_begin.x()) > 3)
		{
			note.duration = std::max(minimum, snap(tickAt(point.x()), modifiers) - note.tick);
		}
		m_transaction->notes.push_back(note);
		update();
		return;
	}
	if (m_action == Action::LeftEdge || m_action == Action::RightEdge)
	{
		for (const auto& note : m_transaction->originalNotes)
			if (note.id == m_hitId)
			{
				const auto edge = m_action == Action::LeftEdge ? note.tick : note.tick + note.duration;
				const auto boundary = snap(edge + tickAt(point.x()) - tickAt(m_begin.x()), modifiers);
				const auto minimum = modifiers.testFlag(Qt::AltModifier) ? 1. : std::max(1., m_quantization);
				const auto declaration
					= static_cast<SVSTrack*>(m_clip->getTrack())->capabilities().original["phonemes"].toObject();
				const svsedit::StretchLimits limits{svs::TempoSource::forSong(*Engine::getSong()).snapshot(),
					double(int(m_clip->startPosition()) + int(m_clip->startTimeOffset())),
					declaration["minimumDurationSeconds"].toDouble(.005),
					declaration["maximumLeadSeconds"].toDouble(0)};
				svsedit::stretchNote(
					m_transaction->notes, m_hitId, boundary, m_action == Action::LeftEdge, minimum, {}, &limits);
				break;
			}
		update();
		return;
	}
	double delta = snap(tickAt(point.x()), modifiers) - m_beginTick;
	double transpose = modifiers.testFlag(Qt::ShiftModifier) ? 0 : std::ceil(pitchAt(point.y())) - m_beginPitch;
	double earliest = INFINITY, minPitch = 127, maxPitch = 0;
	for (const auto& note : m_transaction->originalNotes)
		if (m_selected.contains(note.id))
		{
			earliest = std::min(earliest, note.tick);
			minPitch = std::min(minPitch, note.pitch);
			maxPitch = std::max(maxPitch, note.pitch);
		}
	delta = std::max(delta, -earliest);
	transpose = std::clamp(transpose, -minPitch, 127 - maxPitch);
	const double minimum = modifiers.testFlag(Qt::AltModifier) ? 1. : std::max(1., m_quantization);
	for (auto& note : m_transaction->notes)
		if (m_selected.contains(note.id))
		{
			if (m_action == Action::Move)
			{
				note.tick += delta;
				note.pitch += transpose;
			}
			else if (m_action == Action::LeftEdge)
			{
				const auto end = note.tick + note.duration;
				note.tick = std::clamp(note.tick + delta, 0., std::max(0., end - minimum));
				note.duration = end - note.tick;
			}
			else if (m_action == Action::RightEdge)
				note.duration = std::max(minimum, note.duration + delta);
			else if (m_action == Action::CreateTail)
				note.duration = std::max(minimum, snap(tickAt(point.x()), modifiers) - note.tick);
		}
	// Clip-level curves stay put unless the explicit option is enabled.
	m_transaction->curves = m_transaction->originalCurves;
	if (m_action == Action::Move && m_moveCurves && delta != 0)
	{
		double start = INFINITY, end = -INFINITY;
		for (const auto& note : m_transaction->originalNotes)
			if (m_selected.contains(note.id))
			{
				start = std::min(start, note.tick);
				end = std::max(end, note.tick + note.duration);
			}
		for (auto& curve : m_transaction->curves)
		{
			const auto selected = curve.slice(start, end);
			if (selected.evaluator.points.empty())
				continue;
			curve.erase(start, end);
			curve.replaceRange(start + delta, end + delta, selected);
		}
	}
	update();
}
void SVSCanvas::mouseMoveEvent(QMouseEvent* event)
{
	if (!m_parameter && noteLabels::alignmentEnabled())
	{
		update();
	}
	m_pointer = event->position();
	if (m_action != Action::None)
		updateOperation(event->position(), event->modifiers());
	else if (!m_parameter && noteTool())
	{
		const auto id = hitNote(event->position());
		bool edge = false;
		for (const auto& note : displayedNotes())
			if (note.id == id)
			{
				auto r = noteRect(note);
				auto margin = std::min(5., r.width() / 3);
				edge = event->position().x() - r.left() < margin || r.right() - event->position().x() < margin;
			}
		setCursor(edge ? Qt::SizeHorCursor : m_tool == Tool::Pencil ? Qt::CrossCursor : Qt::ArrowCursor);
	}
}
void SVSCanvas::leaveEvent(QEvent* event)
{
	QWidget::leaveEvent(event);
	update();
}

void SVSCanvas::commitOperation()
{
	m_finishing = true;
	if (m_action == Action::CreateTail && !m_transaction->notes.isEmpty())
	{
		m_lastNoteLength = m_transaction->notes.last().duration;
		auto state = m_clip->editorState();
		state["lastNoteLength"] = m_lastNoteLength;
		m_clip->setEditorState(state);
	}
	m_action = Action::None;
	m_autoScroll->stop();
	if (m_mouseCaptured)
	{
		m_mouseCaptured = false;
		releaseMouse();
	}
	m_transaction->commit();
	m_finishing = false;
	m_frame = {};
	update();
	emit selectionChanged();
}
void SVSCanvas::cancelOperation()
{
	if (m_finishing)
		return;
	m_finishing = true;
	const bool active = m_action != Action::None;
	m_action = Action::None;
	m_autoScroll->stop();
	if (m_mouseCaptured)
	{
		m_mouseCaptured = false;
		releaseMouse();
	}
	m_transaction->cancel();
	if (active)
	{
		m_selected = m_initialSelection;
		m_selectedAnchors = m_initialAnchors;
	}
	m_frame = {};
	m_finishing = false;
	update();
	if (active)
		emit selectionChanged();
}
void SVSCanvas::mouseReleaseEvent(QMouseEvent* event)
{
	const bool release = m_action == Action::CurveReset ? event->button() == Qt::RightButton
		: m_action == Action::Pan						? event->button() == Qt::MiddleButton
														: event->button() == Qt::LeftButton;
	if (release && m_action != Action::None)
	{
		updateOperation(event->position(), event->modifiers());
		commitOperation();
	}
}
void SVSCanvas::mouseDoubleClickEvent(QMouseEvent* event)
{
	if (m_clip && event->button() == Qt::LeftButton && effectiveTool() == Tool::Anchor
		&& event->position().x() >= KeyboardWidth && event->position().y() >= TimelineHeight)
	{
		if (!curveEditable())
			return;
		cancelOperation();
		const auto tick = std::max(0., tickAt(event->position().x()));
		auto curves = m_clip->curves();
		auto curve = pitchCurve();
		curve.insert(tick, curveValue(curveValueAtY(event->position().y())));
		curves[m_curveId] = curve;
		m_clip->setEditorData(m_clip->notes(), curves);
		m_selectedAnchors = {tick};
		update();
		return;
	}
	if (!m_clip || event->button() != Qt::LeftButton || m_parameter || !noteTool()
		|| event->position().x() < KeyboardWidth || event->position().y() < TimelineHeight)
		return;
	cancelOperation();
	const auto hit = hitNote(event->position());
	if (!hit.isEmpty())
	{
		beginLyric(hit);
		return;
	}
	beginNote(event->position(), event->modifiers());
}
void SVSCanvas::beginNote(const QPointF& point, Qt::KeyboardModifiers modifiers)
{
	m_initialSelection = m_selected;
	m_transaction->begin();
	svs::Note note;
	note.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
	note.tick = std::max(0., snap(tickAt(point.x()), modifiers));
	note.pitch = std::clamp(std::ceil(pitchAt(point.y())), 0., 127.);
	note.duration = m_noteLength > 0 ? m_noteLength : m_lastNoteLength;
	const auto* track = static_cast<SVSTrack*>(m_clip->getTrack());
	if (!track->voice().defaultLyric.isEmpty())
		note.lyric = track->voice().defaultLyric;
	m_createdNote = note;
	m_transaction->notes.push_back(note);
	m_selected = {note.id};
	m_action = Action::CreateTail;
	m_begin = point;
	m_pointer = m_begin;
	m_beginTick = note.tick;
	m_beginPitch = note.pitch;
	grabMouse();
	m_mouseCaptured = true;
	m_autoScroll->start();
	emit selectionChanged();
	update();
}
void SVSCanvas::beginLyric(const QString& id)
{
	if (!m_clip)
		return;
	cancelOperation();
	finishLyric(true);
	for (const auto& note : m_clip->notes())
		if (note.id == id)
		{
			auto r = noteRect(note);
			if (r.right() < KeyboardWidth || r.left() > width())
			{
				setScroll(std::max(0., note.tick - 24), m_topPitch);
				r = noteRect(note);
			}
			if (r.top() < TimelineHeight || r.bottom() > height())
			{
				setScroll(m_scrollTick, std::clamp(note.pitch + 4, 0., 127.));
				r = noteRect(note);
			}
			m_lyricId = id;
			m_selected = {id};
			m_composing = false;
			m_lyric->setGeometry(r.toRect().adjusted(0, 0, std::max(0, 100 - int(r.width())), 0));
			m_lyric->setText(note.lyric);
			m_lyric->show();
			m_lyric->setFocus();
			m_lyric->selectAll();
			emit selectionChanged();
			return;
		}
}
void SVSCanvas::finishLyric(bool commit, int navigate)
{
	if (m_lyricId.isEmpty() || m_finishing)
		return;
	const auto id = m_lyricId;
	const auto text = m_lyric->text();
	m_lyricId.clear();
	m_finishing = true;
	m_lyric->hide();
	if (m_composing)
	{
		QApplication::inputMethod()->reset();
		commit = false;
	}
	m_composing = false;
	if (commit && m_clip)
	{
		auto notes = m_clip->notes();
		for (auto& note : notes)
			if (note.id == id)
				note.lyric = text;
		m_clip->setNotes(notes);
	}
	m_finishing = false;
	setFocus();
	if (navigate && m_clip)
	{
		auto notes = m_clip->notes();
		std::stable_sort(notes.begin(), notes.end(), [](const auto& a, const auto& b) { return a.tick < b.tick; });
		int index = -1;
		for (int i = 0; i < notes.size(); ++i)
			if (notes[i].id == id)
				index = i;
		const auto* track = static_cast<SVSTrack*>(m_clip->getTrack());
		const auto marker = track->capabilities().continuation;
		for (index += navigate; index >= 0 && index < notes.size(); index += navigate)
		{
			const auto& note = notes[index];
			const bool continuation = !marker.isEmpty() && note.lyric == marker && note.pronunciation.isEmpty()
				&& !note.phonemes.contains("symbols");
			if (!continuation)
			{
				beginLyric(note.id);
				break;
			}
		}
	}
	update();
}
bool SVSCanvas::eventFilter(QObject* object, QEvent* event)
{
	if (object == m_lyric)
	{
		if (event->type() == QEvent::InputMethod)
			m_composing = !static_cast<QInputMethodEvent*>(event)->preeditString().isEmpty();
		if (event->type() == QEvent::KeyPress)
		{
			auto* key = static_cast<QKeyEvent*>(event);
			if (key->key() == Qt::Key_Escape)
			{
				finishLyric(false);
				return true;
			}
			if (!m_composing && (key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter))
			{
				finishLyric(true);
				return true;
			}
			if (!m_composing && (key->key() == Qt::Key_Tab || key->key() == Qt::Key_Backtab))
			{
				finishLyric(
					true, key->key() == Qt::Key_Backtab || key->modifiers().testFlag(Qt::ShiftModifier) ? -1 : 1);
				return true;
			}
		}
		if (event->type() == QEvent::FocusOut && !m_finishing)
			finishLyric(true);
	}
	return QWidget::eventFilter(object, event);
}
bool SVSCanvas::event(QEvent* event)
{
	if ((event->type() == QEvent::UngrabMouse || event->type() == QEvent::WindowDeactivate) && !m_finishing)
		cancelOperation();
	if (event->type() == QEvent::ToolTip && !m_parameter)
	{
		const auto id = hitNote(static_cast<QHelpEvent*>(event)->pos());
		setToolTip({});
		for (const auto& note : displayedNotes())
			if (note.id == id)
			{
				auto resolved = editorPronunciation(m_clip, note).toJson();
				if (m_clip)
					if (auto audio = m_clip->audio())
					{
						auto published = audio->feedback["pronunciations"].toObject()[id].toObject();
						if (!published.isEmpty())
							resolved = published;
					}
				QStringList symbols;
				for (const auto& symbol : resolved["phonemes"].toArray())
					symbols << symbol.toString();
				const auto details = tr("Lyric: %1\nReading: %2\nSource: %3\nPhonemes: %4\n%5")
										 .arg(note.lyric, resolved["text"].toString(), resolved["source"].toString(),
											 symbols.join(' '), resolved["diagnostic"].toString());
				setToolTip("<qt>" + details.toHtmlEscaped().replace('\n', "<br>") + "</qt>");
				break;
			}
	}
	return QWidget::event(event);
}
void SVSCanvas::copySelection()
{
	if (m_clip && (m_parameter || !noteTool()))
	{
		if (m_selectedAnchors.isEmpty())
			return;
		const auto from = *std::min_element(m_selectedAnchors.begin(), m_selectedAnchors.end()),
				   to = *std::max_element(m_selectedAnchors.begin(), m_selectedAnchors.end());
		auto* mime = new QMimeData;
		mime->setData(CurveMime,
			QJsonDocument(QJsonObject{{"curve", pitchCurve().slice(from, to).toJson()}, {"length", to - from}})
				.toJson(QJsonDocument::Compact));
		QApplication::clipboard()->setMimeData(mime);
		return;
	}
	if (!m_clip)
		return;
	QJsonArray notes;
	double earliest = INFINITY;
	for (const auto& note : m_clip->notes())
		if (m_selected.contains(note.id))
		{
			notes.append(noteJson(note));
			earliest = std::min(earliest, note.tick);
		}
	if (notes.isEmpty())
		return;
	QJsonObject object{{"schemaVersion", 1}, {"notes", notes}, {"anchor", earliest}};
	auto* mime = new QMimeData;
	mime->setData(NoteMime, QJsonDocument(object).toJson(QJsonDocument::Compact));
	QApplication::clipboard()->setMimeData(mime);
}
void SVSCanvas::pasteSelection(double tick)
{
	if (!m_clip || m_feedback)
		return;
	const auto* mime = QApplication::clipboard()->mimeData();
	if (mime->hasFormat(CurveMime))
	{
		if (!curveEditable())
			return;
		const auto bytes = mime->data(CurveMime);
		if (bytes.size() > 4 * 1024 * 1024)
			return;
		const auto object = QJsonDocument::fromJson(bytes).object();
		svs::Curve source;
		QString error;
		const auto length = object["length"].toDouble(NAN);
		if (!std::isfinite(length) || length < 0 || !svs::Curve::fromJson(object["curve"].toObject(), source, error))
			return;
		if (source.id != m_curveId)
		{
			setToolTip(tr("Copied curve belongs to a different parameter"));
			return;
		}
		tick = std::max(0., snap(tick, Qt::NoModifier));
		auto curves = m_clip->curves();
		auto curve = curves.value(source.id, source);
		if (!curves.contains(source.id))
		{
			curve.evaluator.points.clear();
			curve.evaluator.gaps.clear();
		}
		curve.replaceRange(tick, tick + length, source);
		curves[source.id] = curve;
		m_clip->setEditorData(m_clip->notes(), curves);
		m_selectedAnchors.clear();
		for (const auto& point : source.evaluator.points)
			m_selectedAnchors.insert(point.tick + tick);
		update();
		return;
	}
	if (!mime->hasFormat(NoteMime))
		return;
	const auto bytes = mime->data(NoteMime);
	if (bytes.size() > 4 * 1024 * 1024)
		return;
	auto object = QJsonDocument::fromJson(bytes).object();
	if (object["schemaVersion"].toInt() != 1)
		return;
	auto notes = m_clip->notes();
	QSet<QString> selection;
	tick = std::max(0., snap(tick, Qt::NoModifier));
	for (const auto& item : object["notes"].toArray())
	{
		auto note = noteFromJson(item.toObject());
		if (!std::isfinite(note.tick) || !std::isfinite(note.duration) || !std::isfinite(note.pitch)
			|| note.duration <= 0 || note.pitch < 0 || note.pitch > 127)
			return;
		note.tick += tick - object["anchor"].toDouble();
		note.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
		selection.insert(note.id);
		notes.push_back(note);
	}
	m_clip->setNotes(notes);
	m_selected = selection;
	emit selectionChanged();
	update();
}
void SVSCanvas::deleteSelection()
{
	if (!m_clip)
		return;
	if (m_parameter || !noteTool())
	{
		if (!curveEditable())
			return;
		auto curve = pitchCurve();
		auto& points = curve.evaluator.points;
		points.erase(std::remove_if(points.begin(), points.end(),
						 [this](const auto& point) { return m_selectedAnchors.contains(point.tick); }),
			points.end());
		auto curves = m_clip->curves();
		curves[m_curveId] = curve;
		m_clip->setEditorData(m_clip->notes(), curves);
		m_selectedAnchors.clear();
		update();
		return;
	}
	auto notes = m_clip->notes();
	notes.erase(
		std::remove_if(notes.begin(), notes.end(), [this](const auto& note) { return m_selected.contains(note.id); }),
		notes.end());
	m_clip->setNotes(notes);
	m_selected.clear();
	emit selectionChanged();
	update();
}
void SVSCanvas::transpose(int semitones)
{
	if (!m_clip)
		return;
	auto notes = m_clip->notes();
	double min = 127, max = 0;
	bool selected = false;
	for (const auto& note : notes)
		if (m_selected.contains(note.id))
		{
			selected = true;
			min = std::min(min, note.pitch);
			max = std::max(max, note.pitch);
		}
	if (!selected)
		return;
	const auto delta = std::clamp(double(semitones), -min, 127 - max);
	for (auto& note : notes)
		if (m_selected.contains(note.id))
			note.pitch += delta;
	m_clip->setNotes(notes);
}
void SVSCanvas::splitSelection(double tick)
{
	if (!m_clip)
		return;
	auto notes = m_clip->notes();
	QVector<svs::Note> split;
	tick = snap(tick, Qt::NoModifier);
	for (auto& note : notes)
		if (m_selected.contains(note.id))
			if (auto parts = svs::splitNoteForReparse(note, tick))
			{
				note = parts->first;
				split.push_back(parts->second);
			}
	notes += split;
	m_clip->setNotes(notes);
}
void SVSCanvas::keyPressEvent(QKeyEvent* event)
{
	if (!m_clip)
		return;
	if (event->key() == Qt::Key_Space
		&& !(event->modifiers() & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier)))
	{
		auto* song = Engine::getSong();
		if (song->previewClip() != m_clip) { song->playSVSClip(m_clip); }
		else if (event->modifiers().testFlag(Qt::ShiftModifier)) { song->togglePause(); }
		else
		{
			song->stop();
		}
		event->accept();
		return;
	}
	if (event->modifiers() == Qt::NoModifier && event->key() >= Qt::Key_1 && event->key() <= Qt::Key_5)
	{
		const Tool tools[]{Tool::Notes, Tool::Pencil, Tool::Freehand, Tool::Anchor, Tool::Smooth};
		setTool(tools[event->key() - Qt::Key_1]);
		event->accept();
		return;
	}
	if (event->key() == Qt::Key_Escape)
	{
		cancelOperation();
		return;
	}
	if (event->matches(QKeySequence::Undo))
	{
		cancelOperation();
		Engine::projectJournal()->undo();
		return;
	}
	if (event->matches(QKeySequence::Redo)
		|| (event->key() == Qt::Key_Z && event->modifiers().testFlag(Qt::ControlModifier)
			&& event->modifiers().testFlag(Qt::ShiftModifier)))
	{
		cancelOperation();
		Engine::projectJournal()->redo();
		return;
	}
	if (event->matches(QKeySequence::Copy))
	{
		copySelection();
		return;
	}
	if (event->matches(QKeySequence::Cut))
	{
		copySelection();
		deleteSelection();
		return;
	}
	if (event->matches(QKeySequence::Paste))
	{
		const auto local = playbackTick();
		pasteSelection(local);
		return;
	}
	if (event->matches(QKeySequence::SelectAll))
	{
		if (!m_parameter && noteTool())
			for (const auto& note : m_clip->notes())
				m_selected.insert(note.id);
		else
			for (const auto& point : pitchCurve().evaluator.points)
				m_selectedAnchors.insert(point.tick);
		emit selectionChanged();
		update();
		return;
	}
	if (event->key() == Qt::Key_Delete || event->key() == Qt::Key_Backspace)
	{
		deleteSelection();
		return;
	}
	if (event->key() == Qt::Key_Up || event->key() == Qt::Key_Down)
	{
		transpose((event->key() == Qt::Key_Up ? 1 : -1) * (event->modifiers().testFlag(Qt::ShiftModifier) ? 12 : 1));
		return;
	}
	QWidget::keyPressEvent(event);
}
void SVSCanvas::wheelEvent(QWheelEvent* event)
{
	if (m_parameter && event->position().y() >= TimelineHeight && !event->modifiers().testFlag(Qt::ControlModifier)
		&& !event->modifiers().testFlag(Qt::ShiftModifier) && event->angleDelta().x() == 0
		&& event->pixelDelta().x() == 0)
	{
		event->ignore();
		return;
	}
	const auto point = event->position();
	const double delta = event->pixelDelta().isNull() ? event->angleDelta().y() / 120. : event->pixelDelta().y() / 60.;
	const bool keyboard = !m_parameter && point.x() < KeyboardWidth && point.y() >= TimelineHeight;
	const bool ruler = point.x() >= KeyboardWidth && point.y() < TimelineHeight;
	if (keyboard || ruler || event->modifiers().testFlag(Qt::ControlModifier))
	{
		const auto tick = tickAt(point.x()), pitch = pitchAt(point.y());
		if (keyboard || (!ruler && event->modifiers().testFlag(Qt::ShiftModifier)))
		{
			setZoom(horizontalZoom(), verticalZoom() * std::pow(1.15, delta));
			setScroll(m_scrollTick, pitch + (point.y() - TimelineHeight) / m_rowHeight);
		}
		else
		{
			setZoom(horizontalZoom() * std::pow(1.15, delta), verticalZoom());
			setScroll(tick - (point.x() - KeyboardWidth) / m_pixelsPerTick, m_topPitch);
		}
	}
	else if (event->modifiers().testFlag(Qt::ShiftModifier))
		setScroll(m_scrollTick - delta * 48, m_topPitch);
	else
	{
		if (event->pixelDelta().x())
			setScroll(m_scrollTick - event->pixelDelta().x() / m_pixelsPerTick, m_topPitch);
		else if (event->angleDelta().x())
			setScroll(m_scrollTick - event->angleDelta().x() / 120. * 48, m_topPitch);
		setScroll(m_scrollTick, m_topPitch + delta * 3);
	}
	event->accept();
}
void SVSCanvas::setPronunciation(const QString& id, const QString& reading)
{
	if (!m_clip)
		return;
	auto notes = m_clip->notes();
	for (auto& note : notes)
		if (note.id == id)
			note.pronunciation = reading;
	m_clip->setNotes(notes);
}
void SVSCanvas::addPitchActions(QMenu& menu)
{
	if (!m_clip || m_parameter) { return; }
	QVector<QPair<double, double>> ranges;
	const bool anchorSelection = !noteTool() && !m_selectedAnchors.isEmpty();
	if (!noteTool() && m_selectedAnchors.size() >= 2)
	{
		ranges.append({*std::min_element(m_selectedAnchors.begin(), m_selectedAnchors.end()),
			*std::max_element(m_selectedAnchors.begin(), m_selectedAnchors.end())});
	}
	else if (anchorSelection)
	{
		const auto tick = *m_selectedAnchors.begin();
		for (const auto& note : m_clip->notes())
		{
			if (note.tick <= tick && note.tick + note.duration >= tick)
			{
				ranges.append({note.tick, note.tick + note.duration});
			}
		}
	}
	else
	{
		for (const auto& note : m_clip->notes())
		{
			if (m_selected.contains(note.id)) { ranges.append({note.tick, note.tick + note.duration}); }
		}
	}
	const bool wholeClip = !anchorSelection && m_selected.isEmpty();
	menu.addSeparator();
	auto* clear = menu.addAction(tr("Clear hand-drawn pitch"), this, [this, ranges, wholeClip] {
		cancelOperation();
		auto curves = m_clip->curves();
		if (wholeClip) { curves.remove("svs.pitch"); }
		else
		{
			for (const auto& range : ranges)
			{
				curves["svs.pitch"].replaceRange(range.first, range.second, svs::Curve{});
			}
		}
		m_clip->setEditorData(m_clip->notes(), curves);
	});
	clear->setObjectName("svsClearHandDrawnPitch");
	clear->setEnabled(
		!m_clip->readOnly() && m_clip->curves().contains("svs.pitch") && (wholeClip || !ranges.isEmpty()));

	if (wholeClip)
	{
		for (const auto& note : m_clip->notes())
		{
			ranges.append({note.tick, note.tick + note.duration});
		}
	}
	auto* predict = menu.addAction(tr("Re-record pitch"), this, [this, ranges] {
		cancelOperation();
		m_clip->regeneratePitch(ranges);
	});
	predict->setObjectName("svsRepredictSelectedPitch");
	predict->setEnabled(!m_clip->readOnly() && m_clip->supportsPitchRecording() && !ranges.isEmpty());
	const auto scope = wholeClip ? tr("Applies to the whole SVS clip when nothing is selected.")
								 : tr("Applies only to the selection; other pitch and render segments are retained.");
	clear->setToolTip(scope);
	predict->setToolTip(scope);
}
void SVSCanvas::contextMenuEvent(QContextMenuEvent* event)
{
	// Right dragging parameters resets them; it must never open a blocking menu.
	if ((m_parameter && event->reason() == QContextMenuEvent::Mouse && !event->modifiers().testFlag(Qt::ShiftModifier))
		|| (!m_parameter && m_tool == Tool::Freehand && event->reason() == QContextMenuEvent::Mouse
			&& !event->modifiers().testFlag(Qt::ShiftModifier)))
	{
		event->accept();
		return;
	}
	if (m_clip && (m_parameter || !noteTool()))
	{
		QMenu menu(this);
		menu.addAction(tr("Copy curve selection"), this, &SVSCanvas::copySelection);
		menu.addAction(tr("Paste curve here"), this, [this, event] { pasteSelection(tickAt(event->pos().x())); });
		menu.addAction(tr("Delete selected anchors"), this, &SVSCanvas::deleteSelection);
		auto operation = [this](bool connect) {
			if (!curveEditable() || m_selectedAnchors.size() < 2)
				return;
			const auto from = *std::min_element(m_selectedAnchors.begin(), m_selectedAnchors.end()),
					   to = *std::max_element(m_selectedAnchors.begin(), m_selectedAnchors.end());
			auto curve = pitchCurve();
			if (connect)
				curve.connect(from, to);
			else
				curve.erase(from, to);
			auto curves = m_clip->curves();
			curves[m_curveId] = curve;
			m_clip->setEditorData(m_clip->notes(), curves);
		};
		menu.addAction(tr("Connect selection"), this, [operation] { operation(true); });
		menu.addAction(tr("Disconnect selection"), this, [operation] { operation(false); });
		menu.addAction(tr("Anchor value"), this, [this] {
			if (!curveEditable() || m_selectedAnchors.size() != 1)
				return;
			const auto tick = *m_selectedAnchors.begin();
			auto curve = pitchCurve();
			auto value = curve.valueAt(tick);
			if (!value)
				return;
			bool accepted = false;
			QJsonValue result;
			if (m_parameter && (m_parameter->type == "enum" || m_parameter->type == "bool"))
			{
				QStringList labels;
				if (m_parameter->type == "bool")
					labels << tr("Off") << tr("On");
				else
					for (const auto& choice : m_parameter->choices)
						labels << choice.toObject()["name"].toString(choice.toObject()["id"].toString());
				const auto label = QInputDialog::getItem(
					this, tr("Anchor value"), m_parameter->name, labels, int(curveNumber(*value)), false, &accepted);
				result = curveValue(labels.indexOf(label));
			}
			else
				result = curveValue(
					QInputDialog::getDouble(this, tr("Anchor value"), m_parameter ? m_parameter->unit : tr("Semitones"),
						curveNumber(*value), curveMinimum(), curveMaximum(), 3, &accepted));
			if (accepted)
			{
				curve.insert(tick, result);
				auto curves = m_clip->curves();
				curves[m_curveId] = curve;
				m_clip->setEditorData(m_clip->notes(), curves);
			}
		});
		menu.addAction(m_parameter ? tr("Reset curve to default") : tr("Restore automatic pitch"), this, [this] {
			if (!curveEditable())
				return;
			auto curves = m_clip->curves();
			curves.remove(m_curveId);
			m_clip->setEditorData(m_clip->notes(), curves);
		});
		addPitchActions(menu);
		menu.exec(event->globalPos());
		return;
	}
	if (!m_clip)
		return;
	const auto hit = hitNote(event->pos());
	if (!hit.isEmpty() && !m_selected.contains(hit))
	{
		m_selected = {hit};
		emit selectionChanged();
	}
	const auto tick = tickAt(event->pos().x());
	QMenu menu(this);
	auto* copy = menu.addAction(tr("Copy"), this, &SVSCanvas::copySelection);
	copy->setEnabled(!m_selected.isEmpty());
	auto* cut = menu.addAction(tr("Cut"), this, [this] {
		copySelection();
		deleteSelection();
	});
	cut->setEnabled(!m_selected.isEmpty());
	menu.addAction(tr("Paste here"), this, [this, tick] { pasteSelection(tick); });
	auto* split = menu.addAction(tr("Split at cursor"), this, [this, tick] { splitSelection(tick); });
	split->setToolTip(
		tr("Creates two editable notes and re-parses phonemes. Undo restores the complete original note."));
	menu.addAction(tr("Delete"), this, &SVSCanvas::deleteSelection);
	menu.addAction(tr("Transpose up octave"), this, [this] { transpose(12); });
	menu.addAction(tr("Transpose down octave"), this, [this] { transpose(-12); });
	addPitchActions(menu);
	if (!hit.isEmpty())
	{
		auto found = std::find_if(
			m_clip->notes().begin(), m_clip->notes().end(), [&](const auto& note) { return note.id == hit; });
		if (found != m_clip->notes().end())
		{
			const auto note = *found;
			auto* pronunciation = menu.addMenu(tr("Pronunciation"));
			if (static_cast<SVSTrack*>(m_clip->getTrack())
					->capabilities()
					.original["pronunciation"]
					.toObject()["candidates"]
					.toBool())
				for (const auto& candidate : editorPronunciation(m_clip, note, true).candidates)
				{
					const auto reading = candidate.toObject()["reading"].toString();
					pronunciation->addAction(reading, this, [this, hit, reading] { setPronunciation(hit, reading); });
				}
			pronunciation->addAction(tr("Manual reading…"), this, [this, note] {
				bool accepted = false;
				auto reading = QInputDialog::getText(this, tr("Manual reading"),
					tr("Reading (manual phonemes keep precedence)"), QLineEdit::Normal, note.pronunciation, &accepted);
				if (accepted)
					setPronunciation(note.id, reading);
			});
			pronunciation->addAction(tr("Restore automatic reading"), this, [this, hit] { setPronunciation(hit, {}); });
			auto* track = static_cast<SVSTrack*>(m_clip->getTrack());
			if (track->capabilities().noteLanguage)
			{
				auto* languages = menu.addMenu(tr("Note language"));
				auto setLanguage = [this, hit](QString value) {
					if (!m_clip)
						return;
					auto notes = m_clip->notes();
					for (auto& n : notes)
						if (n.id == hit)
							n.language = value;
					m_clip->setNotes(notes);
				};
				languages->addAction(tr("Track default"), this, [setLanguage] { setLanguage({}); });
				for (const auto& value : track->capabilities().languages)
					languages->addAction(value, this, [setLanguage, value] { setLanguage(value); });
			}
		}
	}
	menu.addAction(tr("Input lyrics"), this, [this] {
		if (batchLyricsRequested)
			batchLyricsRequested();
	});
	menu.exec(event->globalPos());
}
}
