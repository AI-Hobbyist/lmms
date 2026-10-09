#include "SVCViews.h"

#include <QDragEnterEvent>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QMenu>
#include <QMimeData>
#include <QPainter>
#include <algorithm>

#include "Knob.h"
#include "MixerChannelLcdSpinBox.h"
#include "SVCClip.h"
#include "SVCTrack.h"
#include "TrackLabelButton.h"

namespace lmms::gui {
SVCTrackView::SVCTrackView(SVCTrack* track, TrackContainerView* container)
	: TrackView(track, container)
{
	setModel(track);
	auto* label = new TrackLabelButton(this, getTrackSettingsWidget());
	label->setToolTip(tr("Singing Voice Conversion"));
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
	painter.fillRect(rect(), palette().window());
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
					palette().highlight());
			}
			painter.setPen(palette().text().color());
			for (int x = 0; x < width(); ++x)
			{
				const auto frame = std::min<uint64_t>(frames - 1, uint64_t(x) * frames / width());
				const auto value = std::clamp(snapshot->trackSample(frame, 0), -1.0f, 1.0f);
				painter.drawLine(QPointF(x, height() * .5), QPointF(x, height() * .5 - value * (height() - 4) * .5));
			}
		}
	}
	painter.setPen(palette().text().color());
	painter.drawText(rect().adjusted(4, 1, -4, -1), Qt::AlignTop | Qt::AlignLeft, m_clip->name());
	paintFlatBorder(painter);
}

void SVCClipView::importAudio()
{
	const auto file = QFileDialog::getOpenFileName(
		this, tr("Import SVC audio"), {}, tr("Audio (*.wav *.flac *.ogg *.mp3);;All files (*)"));
	if (!file.isEmpty()) { m_clip->setSourceFile(file); }
}

void SVCClipView::mouseDoubleClickEvent(QMouseEvent*)
{ importAudio(); }

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
