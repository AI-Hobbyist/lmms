#include "SVCViews.h"

#include <QContextMenuEvent>
#include <QDragEnterEvent>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QMenu>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <algorithm>

#include "Knob.h"
#include "MixerChannelLcdSpinBox.h"
#include "SVCClip.h"
#include "SVCTrack.h"
#include "SVCWindow.h"
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
		menu.addAction(tr("Open SVC plugin"), m_view, [this] { m_view->openWindow(); });
		menu.addAction(tr("Re-render"), static_cast<SVCTrack*>(m_view->getTrack()), &SVCTrack::renderRequested);
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
	auto* layout = new QHBoxLayout(getTrackSettingsWidget());
	layout->setContentsMargins(0, 0, 0, 0);
	layout->addWidget(label);
	layout->addWidget(mix);
	layout->addWidget(volume);
	layout->addWidget(pan);
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
			painter.setPen(m_sourceColor);
			for (int x = 0; x < width(); ++x)
			{
				const auto frame = std::min<uint64_t>(frames - 1, uint64_t(x) * frames / width());
				bool available = false;
				snapshot->renderedSample(frame, available);
				painter.setPen(available ? m_renderedColor : m_sourceColor);
				const auto value = std::clamp(snapshot->trackSample(frame, 0), -1.0f, 1.0f);
				painter.drawLine(QPointF(x, height() * .5), QPointF(x, height() * .5 - value * (height() - 4) * .5));
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
{ menu->addAction(tr("Import audio"), this, &SVCClipView::importAudio); }
} // namespace lmms::gui
