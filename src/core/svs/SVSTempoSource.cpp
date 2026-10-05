#include "SVSTempoSource.h"
#include "Engine.h"
#include "Song.h"
#include "AutomationTrack.h"
#include "PatternStore.h"
#include "Clip.h"
#include <QPointer>
#include <QTimer>
#include <QThread>
namespace lmms::svs {
TempoSource& TempoSource::forSong(Song& song) {static QPointer<TempoSource> source;if(!source||source->parent()!=&song) source=new TempoSource(song);return *source;}
TempoSource::TempoSource(Song& song):QObject(&song),m_song(song),m_baseTempo(song.getTempo()),m_observedTempo(song.getTempo()) {
 for(auto* container:std::initializer_list<TrackContainer*>{&song,Engine::patternStore()}) {
  connect(container,&TrackContainer::trackAdded,this,[this]{schedule();});connect(container,&TrackContainer::trackRemoved,this,[this]{schedule();});connect(container,&TrackContainer::trackMoved,this,[this]{schedule();});
 }
 // Capture transport context at emission, before a queued GUI delivery can cross Stop/seek.
 connect(&song,&Song::tempoChanged,this,[this](int tempo){
  // Stop restores controller use and can notify without changing the value.
  // Such a notification must not replace the captured fallback tempo.
  if(m_observedTempo.exchange(tempo)==tempo) return;
  const bool playing=m_song.isPlaying()||m_song.isExporting();const int tick=int(m_song.getPlayPos(Song::PlayMode::Song));QMetaObject::invokeMethod(this,[this,tempo,playing,tick]{if(playing&&m_snapshot&&m_snapshot->automatedTempoAt(tick)) return;if(m_baseTempo!=tempo) {m_baseTempo=tempo;refresh();}},Qt::AutoConnection);
 },Qt::DirectConnection);
 refresh();
}
void TempoSource::schedule() {if(m_pending) return;m_pending=true;QTimer::singleShot(0,this,[this]{if(m_pending) refresh();});}
std::shared_ptr<const TempoSnapshot> TempoSource::snapshot() {if(m_pending) refresh();return m_snapshot;}
void TempoSource::refresh() {
 Q_ASSERT(QThread::currentThread()==thread());m_pending=false;for(const auto& connection:m_watches) disconnect(connection);m_watches.clear();
 auto tracks=TrackContainer::TrackList{m_song.globalAutomationTrack()};tracks.insert(tracks.end(),m_song.tracks().begin(),m_song.tracks().end());tracks.insert(tracks.end(),Engine::patternStore()->tracks().begin(),Engine::patternStore()->tracks().end());
 for(auto* track:tracks) {
  if(track->type()!=Track::Type::Automation&&track->type()!=Track::Type::HiddenAutomation&&track->type()!=Track::Type::Pattern) continue;
  m_watches<<connect(track,&Track::clipAdded,this,[this]{schedule();})<<connect(track->getMutedModel(),&BoolModel::dataChanged,this,[this]{schedule();});
  for(auto* clip:track->getClips()) {
   m_watches<<connect(clip,&Clip::dataChanged,this,[this]{schedule();})<<connect(clip,&Clip::positionChanged,this,[this]{schedule();})<<connect(clip,&Clip::lengthChanged,this,[this]{schedule();})<<connect(clip,&QObject::destroyed,this,[this]{schedule();});
   for(auto* model:clip->findChildren<BoolModel*>(QString{},Qt::FindDirectChildrenOnly)) m_watches<<connect(model,&BoolModel::dataChanged,this,[this]{schedule();});
  }
 }
 auto captured=TempoSnapshot::capture(m_song,m_baseTempo);auto signature=captured->toJson();
 // An edited/muted automation graph starts a new snapshot from the transport's
 // current fallback tempo, as LMMS retains that value outside active automation.
 if(m_snapshot&&signature["layers"]!=m_signature["layers"]&&m_baseTempo!=m_song.getTempo()) {m_baseTempo=m_song.getTempo();captured=TempoSnapshot::capture(m_song,m_baseTempo);signature=captured->toJson();}
 const bool different=signature!=m_signature;m_snapshot=std::move(captured);m_signature=std::move(signature);if(different) emit changed();
}
}
