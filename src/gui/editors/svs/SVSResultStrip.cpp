#include "SVSResultStrip.h"
#include "SVSTrack.h"
#include "ConfigManager.h"
#include <QJsonDocument>
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
#include "operations/SVSStretchOperations.h"
#include <algorithm>
#include <cmath>
namespace lmms::gui {
SVSResultStrip::SVSResultStrip(SVSClip* clip,QWidget* parent):QWidget(parent),m_clip(clip) {
 setObjectName("svsWaveformPhonemes"); setFixedHeight(36); setFocusPolicy(Qt::StrongFocus); setMouseTracking(true);
 m_autoScroll=new QTimer(this); m_autoScroll->setInterval(25);
 connect(m_autoScroll,&QTimer::timeout,this,[this]{if(!m_dragging) return; const double delta=m_pointer.x()<84?-8/m_pixelsPerTick:m_pointer.x()>width()-24?8/m_pixelsPerTick:0; if(delta) { emit scrollRequested(std::max(0.,m_scroll+delta)); updateBoundary(tickAt(m_pointer.x()),QApplication::keyboardModifiers()); }});
 connect(clip,&Clip::dataChanged,this,[this]{if(m_dragging&&m_clip->notes()!=m_before) cancelOperation(); if(!m_dragging&&!m_selectedNote.isEmpty()) { bool exists=false; for(const auto& cell:cells()) if(cell.note==m_selectedNote&&cell.index==m_selectedIndex) exists=true; if(!exists) { m_selectedNote.clear(); m_selectedIndex=-1; emit selectionChanged(); } } update();});
 connect(clip,&QObject::destroyed,this,[this]{cancelOperation(); setEnabled(false);});
 connect(&svs::TempoSource::forSong(*Engine::getSong()),&svs::TempoSource::changed,this,[this]{cancelOperation();});
 connect(ConfigManager::inst(),&ConfigManager::valueChanged,this,[this](const QString& group,const QString&,const QString&){if(group=="svsEngineSettings") update();});
}
void SVSResultStrip::setViewport(double tick,double zoom) { m_scroll=tick; m_pixelsPerTick=2*zoom; update(); }
void SVSResultStrip::captureTiming() {m_timing=svs::TempoSource::forSong(*Engine::getSong()).snapshot();m_timingOrigin=int(m_clip->startPosition())+int(m_clip->startTimeOffset());}
double SVSResultStrip::shiftedTick(double tick,double seconds) const {return m_timing->tickAfterSeconds(m_timingOrigin+tick,seconds)-m_timingOrigin;}
QColor SVSResultStrip::color(const QString& key,QPalette::ColorRole role) const { return m_colors.value(key).isValid()?m_colors[key]:palette().color(role); }
QVector<SVSResultStrip::Cell> SVSResultStrip::cells() const {
 QVector<Cell> result; if(!m_clip) return result; const auto& notes=m_dragging?m_preview:m_clip->notes(); QSet<QString> manual;
 const bool timing=static_cast<SVSTrack*>(m_clip->getTrack())->capabilities().phonemeTiming;
 for(const auto& note:notes) if(timing&&note.phonemes["segments"].isArray()) { manual.insert(note.id); int index=0; for(const auto& item:note.phonemes["segments"].toArray()) { auto segment=item.toObject(); result.push_back({note.id,segment["symbol"].toString(),index++,note.tick+segment["startTick"].toDouble(),segment["durationTicks"].toDouble(),segment["parameters"].toObject(),segment}); } }
 if(auto audio=m_clip->audio()) { QMap<QString,int> indices; for(const auto& item:audio->feedback["phonemes"].toArray()) { auto segment=item.toObject(); auto id=segment["noteId"].toString(); if(manual.contains(id)) continue;const auto seconds=segment["startSeconds"].toDouble(),tick=audio->mapping.tickAtLocalSeconds(seconds),end=audio->mapping.tickAtLocalSeconds(seconds+segment["durationSeconds"].toDouble()); result.push_back({id,segment["symbol"].toString(),indices[id]++,tick,end-tick,segment["parameters"].toObject(),segment}); } }
 return result;
}
QJsonObject SVSResultStrip::manualPhonemes(const QString& id) const {
 QJsonObject result; for(const auto& note:m_clip->notes()) if(note.id==id) result=note.phonemes;
 double origin=0; for(const auto& note:m_clip->notes()) if(note.id==id) origin=note.tick;
 QJsonArray symbols,segments; for(const auto& cell:cells()) if(cell.note==id) { symbols.append(cell.symbol); auto segment=cell.metadata; segment.remove("startSeconds");segment.remove("durationSeconds");segment.remove("noteId");segment["symbol"]=cell.symbol;segment["startTick"]=cell.tick-origin;segment["durationTicks"]=cell.duration;segment["parameters"]=cell.parameters;segments.append(segment); }
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
 const auto* track=static_cast<SVSTrack*>(m_clip->getTrack());bool showPrefix=true;
 if(track->pluginId()=="org.lmms.svs.diffsinger") {const auto key="engine_"+QString::fromLatin1(track->pluginId().toUtf8().toHex());showPrefix=QJsonDocument::fromJson(ConfigManager::inst()->value("svsEngineSettings",key).toUtf8()).object()["diffsinger.showPhonemeLanguagePrefix"].toBool(true);}
 painter.save(); painter.setClipRect(QRect(60,0,width()-60,height()));
 for(const auto& cell:cells()) {
  QRectF rectangle(xAt(cell.tick),height()/2.,cell.duration*m_pixelsPerTick,height()/2.-1); if(!rectangle.intersects(rect())) continue;
  const auto slash=cell.symbol.indexOf('/');const auto label=!showPrefix&&slash>0&&track->capabilities().languages.contains(cell.symbol.left(slash))?cell.symbol.mid(slash+1):cell.symbol;
  painter.fillRect(rectangle.adjusted(1,1,-1,-1),cell.note==m_selectedNote&&cell.index==m_selectedIndex?palette().highlight():palette().button()); painter.setPen(color("phonemeColor",QPalette::Text)); painter.drawRect(rectangle); painter.drawText(rectangle.adjusted(3,0,-3,0),Qt::AlignVCenter,fontMetrics().elidedText(label,Qt::ElideRight,int(rectangle.width()-6)));
 }
 for(const auto& note:m_dragging?m_preview:m_clip->notes()) {painter.setPen(color("phonemeColor",QPalette::Text));painter.drawLine(QPointF(xAt(note.tick),0),QPointF(xAt(note.tick),height()/2.));painter.drawLine(QPointF(xAt(note.tick+note.duration),0),QPointF(xAt(note.tick+note.duration),height()/2.));}
 painter.restore(); painter.fillRect(QRect(0,0,60,height()),palette().window()); painter.setPen(palette().windowText().color()); painter.drawText(QRect(2,2,56,height()-4),Qt::AlignVCenter,tr("Phonemes"));
}
void SVSResultStrip::mousePressEvent(QMouseEvent* event) {
 if(!m_clip||event->button()!=Qt::LeftButton||event->position().x()<60||event->position().y()<0||event->position().y()>=height()) return;
 setFocus();m_pointer=event->position();const auto items=cells();const bool lower=m_pointer.y()>=height()/2.;
 const auto& cap=static_cast<SVSTrack*>(m_clip->getTrack())->capabilities();
 m_boundary=-1;m_coupled.clear();m_drag=Drag::Phoneme;double edge=0;bool hit=false;
 // Lower half: phoneme starts only. A shared x never steals the upper note rod.
 if(lower) {
  for(auto i=items.crbegin();i!=items.crend();++i) if(std::abs(m_pointer.x()-xAt(i->tick))<=8) {
   m_selectedNote=i->note;m_selectedIndex=i->index;m_boundary=i->index;edge=i->tick;hit=cap.phonemeTiming;break;
  }
  if(!hit&&m_boundary<0) for(auto i=items.crbegin();i!=items.crend();++i) if(m_pointer.x()>=xAt(i->tick)&&m_pointer.x()<=xAt(i->tick+i->duration)) {m_selectedNote=i->note;m_selectedIndex=i->index;break;}
 }
 const auto& notes=m_clip->notes();
 // Upper half: all note heads; unshared note tails also reach the lower half.
 if(!lower||m_boundary<0) for(const auto& note:notes) {
  const svs::Note* previous=nullptr;const svs::Note* next=nullptr;
  for(const auto& other:notes) {if(other.tick<note.tick&&(!previous||other.tick>previous->tick)) previous=&other;if(other.tick>note.tick&&(!next||other.tick<next->tick)) next=&other;}
  if(!lower&&std::abs(m_pointer.x()-xAt(note.tick))<=8) {
   m_selectedNote=note.id;m_selectedIndex=-1;m_drag=Drag::NoteHead;edge=note.tick;hit=true;
   if(previous&&previous->tick+previous->duration>=note.tick-1e-8) m_coupled=previous->id;
   break;
  }
  bool hasPhonemes=false;for(const auto& cell:items) if(cell.note==note.id) hasPhonemes=true;
  if((!lower||(hasPhonemes&&cap.phonemeTiming))&&(!next||next->tick>note.tick+note.duration+1e-8)&&std::abs(m_pointer.x()-xAt(note.tick+note.duration))<=8) {
   m_selectedNote=note.id;m_selectedIndex=-1;m_drag=Drag::NoteTail;edge=note.tick+note.duration;hit=true;break;
  }
 }
 emit selectionChanged();update();if(!hit) return;
 captureTiming();m_before=m_seed=m_clip->notes();m_offset=tickAt(m_pointer.x())-edge;
 if(m_drag==Drag::Phoneme) {
  // Materialize the selected note and the connected previous content neighbor;
  // untouched notes retain their automatic data and unknown fields.
  int selected=-1,previous=-1;for(int i=0;i<m_seed.size();++i) if(m_seed[i].id==m_selectedNote) selected=i;
  if(selected<0) return;
  for(int i=0;i<m_seed.size();++i) if(m_seed[i].tick<m_seed[selected].tick&&(! (previous>=0)||m_seed[i].tick>m_seed[previous].tick)) previous=i;
  if(previous>=0&&m_seed[previous].tick+m_seed[previous].duration>=m_seed[selected].tick-1e-8) m_coupled=m_seed[previous].id;
  for(auto& note:m_seed) if(note.id==m_selectedNote||note.id==m_coupled) {auto manual=manualPhonemes(note.id);if(!manual["segments"].toArray().isEmpty()) note.phonemes=manual;}
  const double minimum=cap.original["phonemes"].toObject()["minimumDurationSeconds"].toDouble(.005);
  for(const auto& note:m_seed) if(note.id==m_selectedNote||note.id==m_coupled) for(const auto& item:note.phonemes["segments"].toArray()) {
   const auto segment=item.toObject();const auto start=note.tick+segment["startTick"].toDouble();
   if(shiftedTick(start,minimum)>start+segment["durationTicks"].toDouble()+1e-8) {setToolTip(tr("Phoneme duration is below the voice's editable minimum"));return;}
  }
 }
 m_preview=m_seed;m_dragging=true;grabMouse();m_autoScroll->start();
}
void SVSResultStrip::updateBoundary(double tick,Qt::KeyboardModifiers modifiers) {
 if(!m_dragging||!m_clip) return;
 tick-=m_offset;if(modifiers.testFlag(Qt::AltModifier)&&m_quantization>0) tick=std::round(tick/m_quantization)*m_quantization;
 m_preview=m_seed;
 if(m_drag!=Drag::Phoneme) {
  const auto declaration=static_cast<SVSTrack*>(m_clip->getTrack())->capabilities().original["phonemes"].toObject();const svsedit::StretchLimits limits{m_timing,m_timingOrigin,declaration["minimumDurationSeconds"].toDouble(.005),declaration["maximumLeadSeconds"].toDouble(0)};
  svsedit::stretchNote(m_preview,m_selectedNote,tick,m_drag==Drag::NoteHead,modifiers.testFlag(Qt::AltModifier)?std::max(1.,m_quantization):1.,m_coupled,&limits);
  emit notePreviewChanged(m_preview,true);update();return;
 }
 const auto declaration=static_cast<SVSTrack*>(m_clip->getTrack())->capabilities().original["phonemes"].toObject();
 const double minimum=declaration["minimumDurationSeconds"].toDouble(.005),lead=declaration["maximumLeadSeconds"].toDouble(0);
 for(auto& note:m_preview) if(note.id==m_selectedNote) {
  auto segments=note.phonemes["segments"].toArray();if(m_boundary<0||m_boundary>=segments.size()) return;
  auto right=segments[m_boundary].toObject();const double current=note.tick+right["startTick"].toDouble(),end=current+right["durationTicks"].toDouble();
  const double upper=shiftedTick(end,-minimum);double lower=shiftedTick(note.tick,-lead);
  int previous=-1;for(int i=0;i<m_preview.size();++i) if(m_preview[i].id==m_coupled) previous=i;
  auto previousSegments=previous>=0?m_preview[previous].phonemes["segments"].toArray():QJsonArray{};
  if(m_boundary==0) {
   if(!previousSegments.isEmpty()) {auto left=previousSegments.last().toObject();lower=std::max(lower,shiftedTick(m_preview[previous].tick+left["startTick"].toDouble(),minimum));}
   if(lower>upper) return;const auto boundary=std::clamp(tick,lower,upper);
   right["startTick"]=boundary-note.tick;right["durationTicks"]=end-boundary;segments[0]=right;
   if(!previousSegments.isEmpty()) {auto left=previousSegments.last().toObject();left["durationTicks"]=boundary-m_preview[previous].tick-left["startTick"].toDouble();previousSegments[previousSegments.size()-1]=left;m_preview[previous].phonemes["segments"]=previousSegments;}
  } else {
   // Prefix ending before the note anchor is rigid unless explicitly declared
   // elastic. Moving its junction translates that prefix, not each duration.
   bool rigid=true;for(int i=0;i<m_boundary;++i) {const auto segment=segments[i].toObject();const auto finish=segment["startTick"].toDouble()+segment["durationTicks"].toDouble();if(segment["stretchWeight"].toDouble(finish<=0?0:1)>0) rigid=false;}
   if(rigid) {
    const auto first=segments[0].toObject();const auto prefixStart=note.tick+first["startTick"].toDouble();lower+=current-prefixStart;
    if(!previousSegments.isEmpty()) {auto left=previousSegments.last().toObject();lower=std::max(lower,shiftedTick(m_preview[previous].tick+left["startTick"].toDouble(),minimum)+current-prefixStart);}
    if(lower>upper) return;const auto boundary=std::clamp(tick,lower,upper),delta=boundary-current;
    for(int i=0;i<m_boundary;++i) {auto segment=segments[i].toObject();segment["startTick"]=segment["startTick"].toDouble()+delta;segments[i]=segment;}
    right["startTick"]=boundary-note.tick;right["durationTicks"]=end-boundary;segments[m_boundary]=right;
    if(!previousSegments.isEmpty()) {auto left=previousSegments.last().toObject();left["durationTicks"]=prefixStart+delta-m_preview[previous].tick-left["startTick"].toDouble();previousSegments[previousSegments.size()-1]=left;m_preview[previous].phonemes["segments"]=previousSegments;}
   } else {
    auto left=segments[m_boundary-1].toObject();const auto start=note.tick+left["startTick"].toDouble();lower=shiftedTick(start,minimum);
    if(lower>upper) return;const auto boundary=std::clamp(tick,lower,upper);left["durationTicks"]=boundary-start;right["startTick"]=boundary-note.tick;right["durationTicks"]=end-boundary;segments[m_boundary-1]=left;segments[m_boundary]=right;
   }
  }
  note.phonemes["segments"]=segments;break;
 }
 update();
}
void SVSResultStrip::mouseMoveEvent(QMouseEvent* event) {
 m_pointer=event->position();if(m_dragging) {updateBoundary(tickAt(m_pointer.x()),event->modifiers());return;}
 bool edge=false;const bool lower=m_pointer.y()>=height()/2.;
 if(m_clip&&m_pointer.x()>=60&&m_pointer.y()>=0&&m_pointer.y()<height()) {
  const auto items=cells();const auto timing=static_cast<SVSTrack*>(m_clip->getTrack())->capabilities().phonemeTiming;
  if(lower&&timing) for(const auto& cell:items) if(std::abs(m_pointer.x()-xAt(cell.tick))<=8) edge=true;
  for(const auto& note:m_clip->notes()) {
   if(!lower&&std::abs(m_pointer.x()-xAt(note.tick))<=8) edge=true;
   bool shared=false,hasPhonemes=false;for(const auto& other:m_clip->notes()) if(other.tick>note.tick&&other.tick<=note.tick+note.duration+1e-8) shared=true;
   for(const auto& cell:items) if(cell.note==note.id) hasPhonemes=true;
   if(!shared&&(!lower||(timing&&hasPhonemes))&&std::abs(m_pointer.x()-xAt(note.tick+note.duration))<=8) edge=true;
  }
 }
 setCursor(edge?Qt::SizeHorCursor:Qt::ArrowCursor);
}
void SVSResultStrip::mouseReleaseEvent(QMouseEvent* event) { if(event->button()==Qt::LeftButton&&m_dragging) { updateBoundary(tickAt(event->position().x()),event->modifiers()); m_finishing=true; m_dragging=false; m_autoScroll->stop(); releaseMouse(); if(m_clip&&m_preview!=m_before&&(m_drag!=Drag::Phoneme||m_preview!=m_seed)) m_clip->setNotes(m_preview); m_finishing=false;emit notePreviewChanged({},false); emit selectionChanged(); update(); } }
void SVSResultStrip::cancelOperation() { if(m_finishing) return; m_finishing=true; const bool active=m_dragging; m_dragging=false; m_autoScroll->stop(); if(active) releaseMouse(); m_before.clear(); m_preview.clear();m_seed.clear(); m_finishing=false;emit notePreviewChanged({},false); update(); }
bool SVSResultStrip::event(QEvent* event) { if(!m_finishing&&(event->type()==QEvent::UngrabMouse||event->type()==QEvent::WindowDeactivate)) cancelOperation(); return QWidget::event(event); }
void SVSResultStrip::keyPressEvent(QKeyEvent* event) { if(event->key()==Qt::Key_Escape) { cancelOperation(); return; } if(event->matches(QKeySequence::Undo)) {cancelOperation(); Engine::projectJournal()->undo();return;} if(event->matches(QKeySequence::Redo)) {cancelOperation(); Engine::projectJournal()->redo();return;} QWidget::keyPressEvent(event); }
void SVSResultStrip::contextMenuEvent(QContextMenuEvent* event) {
 if(!m_clip||m_selectedNote.isEmpty()) return; const auto& cap=static_cast<SVSTrack*>(m_clip->getTrack())->capabilities(); QMenu menu(this);
 auto* replace=menu.addAction(tr("Replace phoneme"),this,[this]{const auto& cap=static_cast<SVSTrack*>(m_clip->getTrack())->capabilities(); bool accepted=false; auto symbol=QInputDialog::getItem(this,tr("Phoneme"),tr("Symbol"),cap.phonemeSet,0,false,&accepted); if(!accepted) return; auto phonemes=manualPhonemes(m_selectedNote); auto segments=phonemes["segments"].toArray(),symbols=phonemes["symbols"].toArray(); if(m_selectedIndex<0||m_selectedIndex>=segments.size()) return; auto segment=segments[m_selectedIndex].toObject(); segment["symbol"]=symbol; segments[m_selectedIndex]=segment; symbols[m_selectedIndex]=symbol; phonemes["segments"]=segments; phonemes["symbols"]=symbols; auto notes=m_clip->notes(); for(auto& note:notes) if(note.id==m_selectedNote) note.phonemes=phonemes; m_clip->setNotes(notes); emit selectionChanged();}); replace->setEnabled(!cap.phonemeSet.isEmpty()&&cap.original["phonemes"].toObject()["attributesEditable"].toBool());
 menu.addAction(tr("Restore automatic phonemes"),this,[this]{auto notes=m_clip->notes(); for(auto& note:notes) if(note.id==m_selectedNote) note.phonemes={}; m_clip->setNotes(notes); emit selectionChanged();}); menu.exec(event->globalPos());
}
}
