#include "SVSResultStrip.h"
#include "SVSTrack.h"
#include "ProjectJournal.h"
#include "Engine.h"
#include "Song.h"
#include <QPainter>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QTimer>
#include <QMenu>
#include <QInputDialog>
#include <QApplication>
#include <QSet>
#include "SVSTempoSource.h"
#include <algorithm>
#include <cmath>
namespace lmms::gui {
SVSResultStrip::SVSResultStrip(SVSClip* clip,QWidget* parent):QWidget(parent),m_clip(clip) {
 setObjectName("svsWaveformPhonemes"); setFixedHeight(80); setFocusPolicy(Qt::StrongFocus); setMouseTracking(true);
 m_autoScroll=new QTimer(this); m_autoScroll->setInterval(25);
 connect(m_autoScroll,&QTimer::timeout,this,[this]{if(!m_dragging) return; const double delta=m_pointer.x()<84?-8/m_pixelsPerTick:m_pointer.x()>width()-24?8/m_pixelsPerTick:0; if(delta) { emit scrollRequested(std::max(0.,m_scroll+delta)); updateBoundary(tickAt(m_pointer.x())); }});
 connect(clip,&Clip::dataChanged,this,[this]{if(m_dragging&&m_clip->notes()!=m_before) cancelOperation(); if(!m_dragging&&!m_selectedNote.isEmpty()) { bool exists=false; for(const auto& cell:cells()) if(cell.note==m_selectedNote&&cell.index==m_selectedIndex) exists=true; if(!exists) { m_selectedNote.clear(); m_selectedIndex=-1; emit selectionChanged(); } } update();});
 connect(clip,&QObject::destroyed,this,[this]{cancelOperation(); setEnabled(false);});
 connect(&svs::TempoSource::forSong(*Engine::getSong()),&svs::TempoSource::changed,this,[this]{cancelOperation();});
}
void SVSResultStrip::setViewport(double tick,double zoom) { m_scroll=tick; m_pixelsPerTick=2*zoom; update(); }
void SVSResultStrip::captureTiming() {m_timing=svs::TempoSource::forSong(*Engine::getSong()).snapshot();m_timingOrigin=int(m_clip->startPosition())+int(m_clip->startTimeOffset());}
double SVSResultStrip::shiftedTick(double tick,double seconds) const {return m_timing->tickAfterSeconds(m_timingOrigin+tick,seconds)-m_timingOrigin;}
QColor SVSResultStrip::color(const QString& key,QPalette::ColorRole role) const { return m_colors.value(key).isValid()?m_colors[key]:palette().color(role); }
QVector<SVSResultStrip::Cell> SVSResultStrip::cells() const {
 QVector<Cell> result; if(!m_clip) return result; const auto& notes=m_dragging?m_preview:m_clip->notes(); QSet<QString> manual;
 const bool timing=static_cast<SVSTrack*>(m_clip->getTrack())->capabilities().phonemeTiming;
 for(const auto& note:notes) if(timing&&note.phonemes["segments"].isArray()) { manual.insert(note.id); int index=0; for(const auto& item:note.phonemes["segments"].toArray()) { auto segment=item.toObject(); result.push_back({note.id,segment["symbol"].toString(),index++,note.tick+segment["startTick"].toDouble(),segment["durationTicks"].toDouble(),segment["parameters"].toObject()}); } }
 if(auto audio=m_clip->audio()) { QMap<QString,int> indices; for(const auto& item:audio->feedback["phonemes"].toArray()) { auto segment=item.toObject(); auto id=segment["noteId"].toString(); if(manual.contains(id)) continue;const auto seconds=segment["startSeconds"].toDouble(),tick=audio->mapping.tickAtLocalSeconds(seconds),end=audio->mapping.tickAtLocalSeconds(seconds+segment["durationSeconds"].toDouble()); result.push_back({id,segment["symbol"].toString(),indices[id]++,tick,end-tick,segment["parameters"].toObject()}); } }
 return result;
}
QJsonObject SVSResultStrip::manualPhonemes(const QString& id) const {
 QJsonObject result; for(const auto& note:m_clip->notes()) if(note.id==id) result=note.phonemes;
 double origin=0; for(const auto& note:m_clip->notes()) if(note.id==id) origin=note.tick;
 QJsonArray symbols,segments; for(const auto& cell:cells()) if(cell.note==id) { symbols.append(cell.symbol); segments.append(QJsonObject{{"symbol",cell.symbol},{"startTick",cell.tick-origin},{"durationTicks",cell.duration},{"parameters",cell.parameters}}); }
 result["symbols"]=symbols; result["segments"]=segments; result["phonemeSet"]=static_cast<SVSTrack*>(m_clip->getTrack())->capabilities().phonemeSetId; return result;
}
QJsonObject SVSResultStrip::selectedParameters() const { for(const auto& cell:cells()) if(cell.note==m_selectedNote&&cell.index==m_selectedIndex) return cell.parameters; return {}; }
bool SVSResultStrip::setSelectedParameter(const QString& id,const QJsonValue& value) {
 if(!m_clip||m_selectedIndex<0) return false; const auto& cap=static_cast<SVSTrack*>(m_clip->getTrack())->capabilities(); auto* parameter=cap.parameter(id,"phoneme");
 if(!cap.original["phonemes"].toObject()["attributesEditable"].toBool()||!parameter||!parameter->writable||!parameter->enabled||!parameter->accepts(value)) return false;
 auto phonemes=manualPhonemes(m_selectedNote); auto segments=phonemes["segments"].toArray(); if(m_selectedIndex>=segments.size()) return false;
 captureTiming();double origin=0;for(const auto& note:m_clip->notes()) if(note.id==m_selectedNote) origin=note.tick;
 for(const auto& item:segments) {const auto segment=item.toObject();const auto start=origin+segment["startTick"].toDouble();if(shiftedTick(start,cap.original["phonemes"].toObject()["minimumDurationSeconds"].toDouble(.005))>start+segment["durationTicks"].toDouble()+1e-8) {setToolTip(tr("Phoneme duration is below the voice's editable minimum"));return false;}}
 auto segment=segments[m_selectedIndex].toObject(); auto parameters=segment["parameters"].toObject(); if(parameters[id]==value) return true; parameters[id]=value; segment["parameters"]=parameters; segments[m_selectedIndex]=segment; phonemes["segments"]=segments;
 auto notes=m_clip->notes(); for(auto& note:notes) if(note.id==m_selectedNote) note.phonemes=phonemes; m_clip->setNotes(notes); emit selectionChanged(); return true;
}
void SVSResultStrip::paintEvent(QPaintEvent*) {
 QPainter painter(this); painter.fillRect(rect(),color("backgroundColor",QPalette::Base)); if(!m_clip) return;
 painter.save(); painter.setClipRect(QRect(60,0,width()-60,height()));
 painter.setPen(color("waveformColor",QPalette::Highlight));
 if(auto audio=m_clip->audio()) for(int x=60;x<width();++x) {
  const auto first=std::max(0.,audio->mapping.samplePosition(tickAt(x),audio->startTick,audio->rate)),end=std::max(0.,audio->mapping.samplePosition(tickAt(x+1),audio->startTick,audio->rate));
  if(first>=audio->samples.size()/2) break;
  auto peak=audio->waveform.peak(size_t(first),size_t(std::ceil(end)));
  if(end-first<64) { peak={}; for(auto frame=size_t(first);frame<std::min(audio->samples.size()/2,size_t(std::ceil(end)));++frame) { peak.minimum=std::min({peak.minimum,audio->samples[frame*2],audio->samples[frame*2+1]}); peak.maximum=std::max({peak.maximum,audio->samples[frame*2],audio->samples[frame*2+1]}); } }
  painter.drawLine(QPointF(x,27-std::clamp(double(peak.maximum),-1.,1.)*24),QPointF(x,27-std::clamp(double(peak.minimum),-1.,1.)*24));
 }
 for(const auto& cell:cells()) {
  QRectF rectangle(xAt(cell.tick),58,cell.duration*m_pixelsPerTick,height()-60); if(!rectangle.intersects(rect())) continue;
  painter.fillRect(rectangle.adjusted(1,1,-1,-1),cell.note==m_selectedNote&&cell.index==m_selectedIndex?palette().highlight():palette().button()); painter.setPen(color("phonemeColor",QPalette::Text)); painter.drawRect(rectangle); painter.drawText(rectangle.adjusted(3,0,-3,0),Qt::AlignVCenter,fontMetrics().elidedText(cell.symbol,Qt::ElideRight,int(rectangle.width()-6)));
 }
 painter.restore(); painter.fillRect(QRect(0,0,60,height()),palette().window()); painter.setPen(palette().windowText().color()); painter.drawText(QRect(2,0,56,52),Qt::AlignVCenter,tr("Wave")); painter.drawText(QRect(2,58,56,height()-58),Qt::AlignVCenter,tr("Phonemes"));
}
void SVSResultStrip::mousePressEvent(QMouseEvent* event) {
 if(!m_clip||event->button()!=Qt::LeftButton||event->position().x()<60||event->position().y()<58) return; setFocus(); const auto items=cells(); m_pointer=event->position();
 for(auto i=items.crbegin();i!=items.crend();++i) if(event->position().x()>=xAt(i->tick)-2&&event->position().x()<=xAt(i->tick+i->duration)+2) {
  m_selectedNote=i->note; m_selectedIndex=i->index; emit selectionChanged(); update();
  if(!static_cast<SVSTrack*>(m_clip->getTrack())->capabilities().phonemeTiming) return;
  const auto margin=std::min(5.,i->duration*m_pixelsPerTick/3); m_boundary=event->position().x()-xAt(i->tick)<margin?i->index:xAt(i->tick+i->duration)-event->position().x()<margin?i->index+1:-1;
  if(m_boundary<0) return;
  auto manual=manualPhonemes(i->note); const auto minimum=static_cast<SVSTrack*>(m_clip->getTrack())->capabilities().original["phonemes"].toObject()["minimumDurationSeconds"].toDouble(.005);
  captureTiming();double origin=0;for(const auto& note:m_clip->notes()) if(note.id==i->note) origin=note.tick;
  for(const auto& item:manual["segments"].toArray()) {const auto segment=item.toObject();const auto start=origin+segment["startTick"].toDouble();if(shiftedTick(start,minimum)>start+segment["durationTicks"].toDouble()+1e-8) {setToolTip(tr("Phoneme duration is below the voice's editable minimum"));return;}}
  m_before=m_preview=m_clip->notes(); for(auto& note:m_preview) if(note.id==i->note) note.phonemes=manual; m_dragging=true; grabMouse(); m_autoScroll->start(); return;
 }
}
void SVSResultStrip::updateBoundary(double tick) {
 if(!m_dragging||!m_clip) return;
 const auto& declaration=static_cast<SVSTrack*>(m_clip->getTrack())->capabilities().original["phonemes"].toObject(); const double minimum=declaration["minimumDurationSeconds"].toDouble(.005),lead=declaration["maximumLeadSeconds"].toDouble(0);
 for(auto& note:m_preview) if(note.id==m_selectedNote) {
  auto segments=note.phonemes["segments"].toArray(); if(m_boundary>segments.size()||segments.isEmpty()) return; const auto local=tick-note.tick;
  if(m_boundary==0) {auto first=segments[0].toObject();const auto end=first["startTick"].toDouble()+first["durationTicks"].toDouble();const auto lower=shiftedTick(note.tick,-lead)-note.tick,upper=shiftedTick(note.tick+end,-minimum)-note.tick;if(lower>upper) return;const auto start=std::clamp(local,lower,upper);first["startTick"]=start;first["durationTicks"]=end-start;segments[0]=first;}
  else if(m_boundary==segments.size()) {auto last=segments.last().toObject();const auto start=last["startTick"].toDouble(),lower=shiftedTick(note.tick+start,minimum)-note.tick;if(lower>note.duration) return;last["durationTicks"]=std::clamp(local,lower,note.duration)-start;segments[segments.size()-1]=last;}
  else {auto left=segments[m_boundary-1].toObject(),right=segments[m_boundary].toObject();const auto start=left["startTick"].toDouble(),end=right["startTick"].toDouble()+right["durationTicks"].toDouble();const auto lower=shiftedTick(note.tick+start,minimum)-note.tick,upper=shiftedTick(note.tick+end,-minimum)-note.tick;if(lower>upper) return;const auto boundary=std::clamp(local,lower,upper);left["durationTicks"]=boundary-start;right["startTick"]=boundary;right["durationTicks"]=end-boundary;segments[m_boundary-1]=left;segments[m_boundary]=right;}
  note.phonemes["segments"]=segments;
 } update();
}
void SVSResultStrip::mouseMoveEvent(QMouseEvent* event) { m_pointer=event->position(); if(m_dragging) updateBoundary(tickAt(m_pointer.x())); else { bool edge=false; for(const auto& cell:cells()) if(m_pointer.y()>=58&&(std::abs(m_pointer.x()-xAt(cell.tick))<5||std::abs(m_pointer.x()-xAt(cell.tick+cell.duration))<5)) edge=true; setCursor(edge&&m_clip&&static_cast<SVSTrack*>(m_clip->getTrack())->capabilities().phonemeTiming?Qt::SizeHorCursor:Qt::ArrowCursor); } }
void SVSResultStrip::mouseReleaseEvent(QMouseEvent* event) { if(event->button()==Qt::LeftButton&&m_dragging) { updateBoundary(tickAt(event->position().x())); m_finishing=true; m_dragging=false; m_autoScroll->stop(); releaseMouse(); if(m_clip&&m_preview!=m_before) m_clip->setNotes(m_preview); m_finishing=false; emit selectionChanged(); update(); } }
void SVSResultStrip::cancelOperation() { if(m_finishing) return; m_finishing=true; const bool active=m_dragging; m_dragging=false; m_autoScroll->stop(); if(active) releaseMouse(); m_before.clear(); m_preview.clear(); m_finishing=false; update(); }
bool SVSResultStrip::event(QEvent* event) { if(!m_finishing&&(event->type()==QEvent::UngrabMouse||event->type()==QEvent::WindowDeactivate)) cancelOperation(); return QWidget::event(event); }
void SVSResultStrip::keyPressEvent(QKeyEvent* event) { if(event->key()==Qt::Key_Escape) { cancelOperation(); return; } if(event->matches(QKeySequence::Undo)) {cancelOperation(); Engine::projectJournal()->undo();return;} if(event->matches(QKeySequence::Redo)) {cancelOperation(); Engine::projectJournal()->redo();return;} QWidget::keyPressEvent(event); }
void SVSResultStrip::contextMenuEvent(QContextMenuEvent* event) {
 if(!m_clip||m_selectedNote.isEmpty()) return; const auto& cap=static_cast<SVSTrack*>(m_clip->getTrack())->capabilities(); QMenu menu(this);
 auto* replace=menu.addAction(tr("Replace phoneme"),this,[this]{const auto& cap=static_cast<SVSTrack*>(m_clip->getTrack())->capabilities(); bool accepted=false; auto symbol=QInputDialog::getItem(this,tr("Phoneme"),tr("Symbol"),cap.phonemeSet,0,false,&accepted); if(!accepted) return; auto phonemes=manualPhonemes(m_selectedNote); auto segments=phonemes["segments"].toArray(),symbols=phonemes["symbols"].toArray(); if(m_selectedIndex<0||m_selectedIndex>=segments.size()) return; auto segment=segments[m_selectedIndex].toObject(); segment["symbol"]=symbol; segments[m_selectedIndex]=segment; symbols[m_selectedIndex]=symbol; phonemes["segments"]=segments; phonemes["symbols"]=symbols; auto notes=m_clip->notes(); for(auto& note:notes) if(note.id==m_selectedNote) note.phonemes=phonemes; m_clip->setNotes(notes); emit selectionChanged();}); replace->setEnabled(!cap.phonemeSet.isEmpty()&&cap.original["phonemes"].toObject()["attributesEditable"].toBool());
 menu.addAction(tr("Restore automatic phonemes"),this,[this]{auto notes=m_clip->notes(); for(auto& note:notes) if(note.id==m_selectedNote) note.phonemes={}; m_clip->setNotes(notes); emit selectionChanged();}); menu.exec(event->globalPos());
}
}
