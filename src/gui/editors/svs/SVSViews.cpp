#include "SVSViews.h"
#include "SVSTrack.h"
#include "SVSClip.h"
#include "TrackLabelButton.h"
#include "StringPairDrag.h"
#include "Knob.h"
#include "MixerChannelLcdSpinBox.h"
#include "EffectRackView.h"
#include "embed.h"
#include "Engine.h"
#include "Song.h"
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QComboBox>
#include <QLabel>
#include <QPushButton>
#include <QPainter>
#include <QMouseEvent>
#include <QDropEvent>
#include <QScrollArea>
#include <QInputDialog>
#include <QLineEdit>
#include <QUuid>
#include <cmath>
namespace lmms::gui {
SVSTrackView::SVSTrackView(SVSTrack* track,TrackContainerView* container):TrackView(track,container) {
 auto* label=new TrackLabelButton(this,getTrackSettingsWidget()); label->setObjectName("svsTrackAvatar");
 auto refresh=[label,track]{ auto image=QPixmap(track->voice().avatar); label->setIcon(image.isNull()?embed::getIconPixmap("sample_track"):image); }; refresh(); connect(track,&Track::dataChanged,this,refresh);
 auto* voices=new QComboBox(getTrackSettingsWidget()); voices->setObjectName("svsVoiceSelector"); voices->addItem(tr("Select voice"));
 for(const auto& voice:svs::Registry::instance().voices()) voices->addItem(voice.name,voice.pluginId+"/"+voice.id);
 auto refreshVoice=[voices,track] { auto value=track->pluginId()+"/"+track->voiceId(); voices->setCurrentIndex(std::max(0,voices->findData(value))); }; refreshVoice(); connect(track,&Track::dataChanged,this,refreshVoice);
 connect(voices,qOverload<int>(&QComboBox::activated),this,[track,voices](int index){ auto value=voices->itemData(index).toString(); if(value.isEmpty()) return; auto split=value.lastIndexOf('/'); track->bindVoice(value.left(split),value.mid(split+1)); });
 auto* volume=new VolumeKnob(KnobType::Small17,tr("VOL"),getTrackSettingsWidget(),Knob::LabelRendering::LegacyFixedFontSize,tr("Track volume")); volume->setModel(track->volumeModel());
 auto* pan=new Knob(KnobType::Small17,tr("PAN"),getTrackSettingsWidget(),Knob::LabelRendering::LegacyFixedFontSize,tr("Panning")); pan->setModel(track->panningModel());
 auto* mix=new MixerChannelLcdSpinBox(2,getTrackSettingsWidget(),tr("Mixer channel"),this); mix->setModel(track->mixerChannelModel());
 auto* layout=new QHBoxLayout(getTrackSettingsWidget()); layout->setContentsMargins(0,0,0,0); layout->setSpacing(1); layout->addWidget(label); layout->addWidget(voices); layout->addWidget(mix); layout->addWidget(volume); layout->addWidget(pan);
 connect(label,&QToolButton::clicked,this,[this,track]{ auto* dialog=new QDialog(this); dialog->setAttribute(Qt::WA_DeleteOnClose); dialog->setWindowTitle(tr("SVS voice and effects")); auto* body=new QVBoxLayout(dialog); auto* restore=new QPushButton(tr("Restore voice name"),dialog); body->addWidget(restore); connect(restore,&QPushButton::clicked,track,&SVSTrack::restoreVoiceName); body->addWidget(new EffectRackView(track->audioBusHandle()->effects(),dialog)); dialog->show(); });
 setAcceptDrops(true);
}
void SVSTrackView::dragEnterEvent(QDragEnterEvent* event) { if(!StringPairDrag::processDragEnterEvent(event,"svsvoice")) TrackView::dragEnterEvent(event); }
void SVSTrackView::dropEvent(QDropEvent* event) { if(StringPairDrag::decodeKey(event)=="svsvoice") { auto value=StringPairDrag::decodeValue(event); auto split=value.lastIndexOf('/'); static_cast<SVSTrack*>(getTrack())->bindVoice(value.left(split),value.mid(split+1)); event->accept(); } else TrackView::dropEvent(event); }
SVSClipView::SVSClipView(SVSClip* clip,TrackView* view):ClipView(clip,view),m_clip(clip) { connect(clip,&Clip::dataChanged,this,[this]{setToolTip(m_clip->status()); update();}); }
void SVSClipView::paintEvent(QPaintEvent*) {
 QPainter p(this); p.fillRect(rect(),isSelected()?palette().highlight():palette().button()); p.setClipRect(rect().adjusted(1,1,-1,-1));
 for(const auto& note:m_clip->notes()) { double x=(note.tick+int(m_clip->startTimeOffset()))/int(m_clip->length())*width(); double w=note.duration/int(m_clip->length())*width(); double y=height()-5-(note.pitch-36)/60*(height()-10); p.fillRect(QRectF(x,y,std::max(1.,w),2),palette().highlight().color()); }
 p.setPen(palette().text().color()); p.drawText(3,12,m_clip->name()); p.drawText(3,height()-3,m_clip->status());
}
void SVSClipView::mouseDoubleClickEvent(QMouseEvent*) { auto* editor=new SVSPianoRoll(m_clip,this); editor->setAttribute(Qt::WA_DeleteOnClose); editor->show(); }
namespace {
// M1 canvas. M3 extends this independent editor with the specified operation layers.
class NoteCanvas : public QWidget {
public:
 explicit NoteCanvas(SVSClip* clip,QWidget* parent):QWidget(parent),m_clip(clip) { setObjectName("svsNoteCanvas"); setMinimumSize(1000,600); setFocusPolicy(Qt::StrongFocus); connect(clip,&Clip::dataChanged,this,qOverload<>(&QWidget::update)); }
 void paintEvent(QPaintEvent*) override {
  QPainter p(this); p.fillRect(rect(),palette().base()); p.setPen(palette().mid().color());
  for(int tick=0;tick<1920;tick+=12) p.drawLine(60+tick*2,0,60+tick*2,height());
  for(int pitch=36;pitch<=84;++pitch) { auto y=(84-pitch)*12+24; p.drawLine(0,y,width(),y); if(pitch%12==0) { p.setPen(palette().text().color()); p.drawText(2,y+10,QString("C%1").arg(pitch/12-1)); p.setPen(palette().mid().color()); } }
  if(!m_clip) return;
  for(const auto& n:m_clip->notes()) { QRectF r(60+n.tick*2,(84-n.pitch)*12+24,n.duration*2,12); p.fillRect(r,palette().highlight()); p.setPen(palette().highlightedText().color()); p.drawText(r,Qt::AlignLeft,n.lyric); }
 }
 void mouseDoubleClickEvent(QMouseEvent* e) override {
  if(!m_clip||e->position().x()<60) return; auto notes=m_clip->notes();
  auto x=e->position().x(),y=e->position().y();
  for(auto& n:notes) if(QRectF(60+n.tick*2,(84-n.pitch)*12+24,n.duration*2,12).contains(e->position())) { bool ok=false; auto lyric=QInputDialog::getText(this,tr("Lyric"),tr("Lyric"),QLineEdit::Normal,n.lyric,&ok); if(ok) { n.lyric=lyric; m_clip->setNotes(notes); } return; }
  svs::Note n; n.id=QUuid::createUuid().toString(QUuid::WithoutBraces); n.tick=std::max(0.,std::floor((x-60)/24)*12); n.duration=48; n.pitch=std::clamp(84-std::floor((y-24)/12),0.,127.); auto* track=static_cast<SVSTrack*>(m_clip->getTrack()); if(!track->voice().defaultLyric.isEmpty()) n.lyric=track->voice().defaultLyric; notes.push_back(n); m_clip->setNotes(notes);
 }
private: QPointer<SVSClip> m_clip;
};
}
SVSPianoRoll::SVSPianoRoll(SVSClip* clip,QWidget* parent):QDialog(parent) {
 setWindowTitle(tr("SVS Piano Roll — LMMS")); resize(1100,740); auto* layout=new QVBoxLayout(this); auto* toolbar=new QHBoxLayout; layout->addLayout(toolbar);
 auto* play=new QPushButton(tr("Play"),this); toolbar->addWidget(play); connect(play,&QPushButton::clicked,Engine::getSong(),&Song::playSong);
 auto* stop=new QPushButton(tr("Stop"),this); toolbar->addWidget(stop); connect(stop,&QPushButton::clicked,Engine::getSong(),&Song::stop);
 auto* render=new QPushButton(tr("Synthesize"),this); toolbar->addWidget(render); connect(render,&QPushButton::clicked,clip,&SVSClip::synthesize);
 auto* status=new QLabel(clip->status(),this); toolbar->addWidget(status); connect(clip,&Clip::dataChanged,this,[clip,status]{status->setText(clip->status());}); connect(clip,&QObject::destroyed,this,&QDialog::close);
 auto* scroll=new QScrollArea(this); scroll->setWidget(new NoteCanvas(clip,scroll)); layout->addWidget(scroll); toolbar->addStretch();
}
}
