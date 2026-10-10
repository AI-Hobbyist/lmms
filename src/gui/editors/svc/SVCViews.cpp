#include "SVCViews.h"

#include <QContextMenuEvent>
#include <QCoreApplication>
#include <QDragEnterEvent>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QJsonDocument>
#include <QMenu>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <algorithm>

#include "Engine.h"
#include "FadeButton.h"
#include "GuiApplication.h"
#include "Knob.h"
#include "MainWindow.h"
#include "Mixer.h"
#include "MixerChannelLcdSpinBox.h"
#include "MixerView.h"
#include "RenameDialog.h"
#include "SVCClip.h"
#include "SVCConversion.h"
#include "SVCTrack.h"
#include "SVCWindow.h"
#include "StringPairDrag.h"
#include "TrackLabelButton.h"
#include "embed.h"

namespace lmms::gui {
namespace {
class SVCLabelButton final : public TrackLabelButton
{
public:
	SVCLabelButton(SVCTrackView* view, QWidget* parent)
		: TrackLabelButton(view, parent)
		, m_view(view)
	{ setIcon(embed::getIconPixmap("svc_track.svg")); }

protected:
	void mousePressEvent(QMouseEvent* event) override
	{
		if (event->button() == Qt::RightButton) { event->accept(); }
		else
		{
			TrackLabelButton::mousePressEvent(event);
		}
	}
	void mouseReleaseEvent(QMouseEvent* event) override
	{
		if (event->button() == Qt::RightButton) { event->accept(); }
		else
		{
			TrackLabelButton::mouseReleaseEvent(event);
		}
	}
	void contextMenuEvent(QContextMenuEvent* event) override
	{
		QMenu menu(this);
		menu.addAction(QCoreApplication::translate("lmms::gui::SVCLabelButton", "Open SVC plugin"), m_view,
			[this] { m_view->openWindow(); });
		menu.addAction(QCoreApplication::translate("lmms::gui::SVCLabelButton", "Re-render"),
			static_cast<SVCTrack*>(m_view->getTrack()), &SVCTrack::renderRequested);
		menu.exec(event->globalPos());
	}

private:
	SVCTrackView* m_view;
};
} // namespace
SVCTrackView::SVCTrackView(SVCTrack* track, TrackContainerView* container)
	: TrackView(track, container)
{
	setModel(track);
	auto* label = new SVCLabelButton(this, getTrackSettingsWidget());
	label->setToolTip(tr("Singing Voice Conversion"));
	connect(label, &QToolButton::clicked, this, [this] { openWindow(); });
	auto* mix = new MixerChannelLcdSpinBox(2, getTrackSettingsWidget(), tr("Mixer channel"), this);
	mix->setModel(track->mixerChannelModel());
	auto* volume = new VolumeKnob(KnobType::Small17, tr("VOL"), getTrackSettingsWidget(),
		Knob::LabelRendering::LegacyFixedFontSize, tr("Track volume"));
	volume->setModel(track->volumeModel());
	auto* pan = new Knob(KnobType::Small17, tr("PAN"), getTrackSettingsWidget(),
		Knob::LabelRendering::LegacyFixedFontSize, tr("Panning"));
	pan->setModel(track->panningModel());

	auto* activity = new FadeButton(getTrackSettingsWidget());
	m_activityIndicator = activity;
	activity->setMuted(track->isMuted());
	activity->setObjectName("voiceTrackActivity");
	activity->setFixedSize(8, 28);
	connect(
		track, &SVCTrack::playbackActivity, activity,
		[activity] {
			activity->activateOnce();
			activity->noteEnd();
		},
		Qt::QueuedConnection);
	auto* layout = new QHBoxLayout(getTrackSettingsWidget());
	layout->setContentsMargins(0, 0, 0, 0);
	layout->addWidget(label);
	layout->addWidget(mix);
	layout->addWidget(activity);
	layout->addWidget(volume);
	layout->addWidget(pan);
}

QMenu* SVCTrackView::createMixerMenu(QString title, QString newMixerLabel)
{
	auto* track = static_cast<SVCTrack*>(getTrack());
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

void SVCTrackView::dragEnterEvent(QDragEnterEvent* event)
{
	if (StringPairDrag::decodeKey(event) == "svcselection") { event->acceptProposedAction(); }
	else
	{
		TrackView::dragEnterEvent(event);
	}
}

void SVCTrackView::dropEvent(QDropEvent* event)
{
	if (StringPairDrag::decodeKey(event) == "svcselection")
	{
		const auto selection = QJsonDocument::fromJson(StringPairDrag::decodeValue(event).toUtf8()).object();
		if (!selection.value("engine_id").toString().isEmpty() && !selection.value("model_id").toString().isEmpty()
			&& static_cast<SVCTrack*>(getTrack())->setSelection(selection))
		{
			if (m_window) { m_window->refreshSelection(); }
			event->acceptProposedAction();
		}
		else
		{
			event->ignore();
		}
	}
	else
	{
		TrackView::dropEvent(event);
	}
}

void SVCTrackView::openWindow(SVCClip* clip)
{
	if (!m_window) { m_window = new SVCWindow(static_cast<SVCTrack*>(getTrack()), this); }
	if (clip) { m_window->selectClip(clip); }
	m_window->show();
	m_window->raise();
	m_window->activateWindow();
}

SVCClipView::SVCClipView(SVCClip* clip, TrackView* view)
	: ClipView(clip, view)
	, m_clip(clip)
{
	connect(clip, &Clip::dataChanged, this, [this]() {
		setToolTip(m_clip->status());
		update();
	});
}

void SVCClipView::paintEvent(QPaintEvent*)
{
	QPainter painter(this);
	painter.fillRect(rect(), m_clip->color().value_or(m_trackColor).darker(180));
	const auto state = m_clip->playback();
	if (state && width() > 0)
	{
		const auto snapshot = state->snapshot();
		const auto frames = snapshot->source->frames();
		if (frames)
		{
			for (const auto& region : snapshot->rendered)
			{
				painter.fillRect(QRectF(double(region.start) * width() / frames, 0,
									 double(region.end - region.start) * width() / frames, height()),
					m_renderedColor.darker(200));
			}
			if (m_waveformSnapshot != snapshot || m_waveformPeaks.size() != static_cast<size_t>(width()))
			{
				m_waveformPeaks.assign(width(), {});
				for (int x = 0; x < width(); ++x)
				{
					auto& peak = m_waveformPeaks[x];
					const auto begin = uint64_t(x) * frames / width();
					const auto end = std::min(frames, std::max(begin + 1, uint64_t(x + 1) * frames / width()));
					// Preserve both channels' extrema across the entire pixel interval.
					// Sampling just one frame aliases the waveform when zoomed out.
					for (auto frame = begin; frame < end; ++frame)
					{
						if (snapshot->renderedRegion(frame)) { continue; }
						for (unsigned channel = 0; channel < 2; ++channel)
						{
							const auto value = snapshot->sourceSample(frame, channel);
							peak.minimum = std::min(peak.minimum, value);
							peak.maximum = std::max(peak.maximum, value);
						}
					}
					for (const auto& region : snapshot->rendered)
					{
						const auto first = std::max(begin, region.start);
						const auto last = std::min(end, region.end);
						if (first >= last) { continue; }
						peak.rendered = true;
						const auto& audio = *region.audio;
						const auto nativeFirst
							= uint64_t(double(first - audio.inputStart) * audio.rate / snapshot->source->rate);
						const auto nativeLast = std::min(audio.frames,
							uint64_t(std::ceil(double(last - audio.inputStart) * audio.rate / snapshot->source->rate)));
						for (auto frame = nativeFirst; frame < nativeLast; ++frame)
						{
							const auto value = audio.sample(frame);
							peak.minimum = std::min(peak.minimum, value);
							peak.maximum = std::max(peak.maximum, value);
						}
					}
				}
				m_waveformSnapshot = snapshot;
			}
			const auto center = height() * .5;
			const auto scale = (height() - 4) * .5;
			for (int x = 0; x < width(); ++x)
			{
				const auto& peak = m_waveformPeaks[x];
				painter.setPen(peak.rendered ? m_renderedColor : m_sourceColor);
				painter.drawLine(QPointF(x, center - std::clamp(peak.maximum, -1.f, 1.f) * scale),
					QPointF(x, center - std::clamp(peak.minimum, -1.f, 1.f) * scale));
			}
		}
	}
	painter.setPen(palette().text().color());
	painter.drawText(rect().adjusted(4, 1, -4, -1), Qt::AlignTop | Qt::AlignLeft, m_clip->name());
	if (!m_clip->conversionComplete())
	{
		painter.setPen(m_clip->conversionFailed() ? m_errorColor : m_pendingColor);
		painter.drawLine(1, height() - 2, width() - 2, height() - 2);
	}
	paintFlatBorder(painter);
}

void SVCClipView::importAudio()
{
	const auto file = QFileDialog::getOpenFileName(
		this, tr("Import SVC audio"), {}, tr("Audio (*.wav *.flac *.ogg *.mp3);;All files (*)"));
	if (!file.isEmpty()) { m_clip->setSourceFile(file); }
}

void SVCClipView::mouseDoubleClickEvent(QMouseEvent*)
{
	if (auto* view = dynamic_cast<SVCTrackView*>(getTrackView())) { view->openWindow(m_clip); }
}

void SVCClipView::dragEnterEvent(QDragEnterEvent* event)
{
	if (event->mimeData()->hasUrls()) { event->acceptProposedAction(); }
	else
	{
		ClipView::dragEnterEvent(event);
	}
}

void SVCClipView::dropEvent(QDropEvent* event)
{
	if (event->mimeData()->hasUrls() && !event->mimeData()->urls().isEmpty())
	{
		if (m_clip->setSourceFile(event->mimeData()->urls().first().toLocalFile())) { event->acceptProposedAction(); }
	}
	else
	{
		ClipView::dropEvent(event);
	}
}

void SVCClipView::constructContextMenu(QMenu* menu)
{
	menu->addAction(tr("Import audio"), this, &SVCClipView::importAudio);
	menu->addAction(embed::getIconPixmap("svc_render.svg"), tr("Re-render"), this,
		[this] { svc::ConversionService::instance().renderClip(m_clip); });
	menu->addSeparator();
	menu->addAction(embed::getIconPixmap("edit_rename"), tr("Change name"), this, [this] {
		auto name = m_clip->name();
		RenameDialog dialog(name);
		if (dialog.exec() == QDialog::Accepted) { m_clip->setName(name); }
	});
}
} // namespace lmms::gui
