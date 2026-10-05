#include "SVSTrack.h"
#include "SVSClip.h"
#include "SVSViews.h"
#include "ConfigManager.h"
#include "Engine.h"
#include "AudioEngine.h"
#include "Mixer.h"
#include "Song.h"
#include "EffectChain.h"
#include "SampleFrame.h"
#include "volume.h"
#include "panning.h"
#include <cmath>
#include <algorithm>
#include <QFile>
#include <QDir>
#include <QJsonDocument>
#include <QPointer>
#include <QCoreApplication>
#include <QThreadPool>
#include <QRunnable>
namespace lmms {
namespace {
class SVSPlaybackHandle : public PlayHandle {
public:
 SVSPlaybackHandle(SVSTrack* track,std::shared_ptr<const svs::Audio> audio,double start,f_cnt_t frames,f_cnt_t offset):PlayHandle(Type::SVSPlayHandle,offset),m_track(track),m_audio(std::move(audio)),m_start(start),m_frames(frames) { setAudioBusHandle(track->audioBusHandle()); }
 bool isFinished() const override { return m_frames==0; }
 bool isFromTrack(const Track* track) const override { return track==m_track; }
 void play(SampleFrame* buffer) override {
  const double ratio=double(m_audio->rate)/Engine::audioEngine()->outputSampleRate(); const auto available=Engine::audioEngine()->framesPerPeriod();
  const auto count=std::min(m_frames,available-offset());
  for(f_cnt_t f=0;f<count;++f) { const double position=m_start+f*ratio; if(position<0) continue; auto index=static_cast<size_t>(position); if(index+1>=m_audio->samples.size()/2) continue; const float fraction=position-index; for(int channel=0;channel<2;++channel) buffer[f+offset()][channel]=m_audio->samples[index*2+channel]*(1-fraction)+m_audio->samples[(index+1)*2+channel]*fraction; }
  m_start+=count*ratio; m_frames-=count; setOffset(0);
 }
private: SVSTrack* m_track; std::shared_ptr<const svs::Audio> m_audio; double m_start; f_cnt_t m_frames;
};
}
SVSTrack::SVSTrack(TrackContainer* tc):Track(Type::SVS,tc),m_volume(DefaultVolume,MinVolume,MaxVolume,0.1f,this,"Volume"),m_pan(DefaultPanning,PanningLeft,PanningRight,1,this,"Panning"),m_mix(0,0,Engine::mixer()->numChannels()-1,this,"Mixer channel"),m_bus("SVS",true,&m_volume,&m_pan,&m_mutedModel) {
 Track::setName("SVS"); m_pan.setCenterValue(DefaultPanning); connect(&m_mix,&IntModel::dataChanged,this,[this]{m_bus.setNextMixerChannel(m_mix.value());});
 m_portraitSettings={{"visible",ConfigManager::inst()->value("svs","portraitVisible","1")!="0"},{"transparency",std::clamp(ConfigManager::inst()->value("svs","portraitTransparency","70").toInt(),0,100)},{"x",1.},{"y",1.}};
}
SVSTrack::~SVSTrack() { Engine::audioEngine()->removePlayHandlesOfTypes(this,PlayHandle::Type::SVSPlayHandle); }
void SVSTrack::setName(const QString& name) { m_customName=true; Track::setName(name); m_bus.setName(name); }
void SVSTrack::restoreVoiceName() { addJournalCheckPoint(); m_customName=false; Track::setName(m_voice.name.isEmpty()?"SVS":m_voice.name); m_bus.setName(name()); }
void SVSTrack::bindVoice(const QString& plugin,const QString& voice) {
 addJournalCheckPoint(); m_pluginId=plugin; m_voiceId=voice; m_voice={}; for(const auto& v:svs::Registry::instance().voices()) if(v.pluginId==plugin&&v.id==voice) { m_voice=v; break; }
 ++m_capabilityRequest;
 m_capabilities={}; m_dictionaries.clear(); m_capabilityDiagnostics.clear(); QString error;
 if(auto instance=svs::Registry::instance().plugin(plugin)) {
  auto schema=instance->capabilities(voice,m_parameters,error);
  if(!error.isEmpty()||!svs::Capabilities::parse(schema,m_capabilities,error)) m_capabilityDiagnostics<<error;
  else for(const auto& resource:m_capabilities.dictionaryResources) {
   const auto root=QFileInfo(m_voice.package).canonicalFilePath()+"/";
   const auto path=QFileInfo(QDir(m_voice.package).filePath(resource)).canonicalFilePath();
   QFile file(path); svs::Dictionary dictionary;
   if(!path.startsWith(root,Qt::CaseInsensitive)||!file.open(QIODevice::ReadOnly)) m_capabilityDiagnostics<<"Missing dictionary: "+resource;
   else if(!svs::Dictionary::parse(file.read(4*1024*1024+1),m_capabilities.phonemeSet,dictionary,error)) m_capabilityDiagnostics<<resource+": "+error;
   else if(dictionary.phonemeSet!=m_capabilities.phonemeSetId||!m_capabilities.languages.contains(dictionary.language)) m_capabilityDiagnostics<<resource+": incompatible dictionary language/phoneme set";
   else m_dictionaries.push_back(dictionary);
  }
 }
 if(!m_capabilities.languages.contains(m_language)) m_language=m_capabilities.defaultLanguage;
 if(!m_customName) Track::setName(m_voice.name.isEmpty()?"SVS":m_voice.name); m_bus.setName(name());
 for(auto* clip:getClips()) { auto* c=static_cast<SVSClip*>(clip); c->invalidate(); c->synthesize(); } emit dataChanged(); Engine::getSong()->setModified();
}
Clip* SVSTrack::createClip(const TimePos& pos) { auto* clip=new SVSClip(this); clip->movePosition(pos); return clip; }
bool SVSTrack::setParameter(const QString& id,const QJsonValue& value) {
 const auto* parameter=m_capabilities.parameter(id,"track");
 if(!parameter||!parameter->writable||!parameter->enabled||!parameter->accepts(value)) return false;
 if(m_parameters.value(id)==value) return true;
 addJournalCheckPoint(); m_parameters[id]=value;
 refreshCapabilities();
 for(auto* clip:getClips()) { auto* c=static_cast<SVSClip*>(clip); c->invalidate(); c->synthesize(); }
 emit dataChanged(); Engine::getSong()->setModified(); return true;
}
bool SVSTrack::setLanguage(const QString& value) {
 if(!m_capabilities.languages.contains(value)) return false; if(m_language==value) return true;
 addJournalCheckPoint(); m_language=value;
 refreshCapabilities();
 for(auto* clip:getClips()) { auto* c=static_cast<SVSClip*>(clip); c->invalidate(); c->synthesize(); }
 emit dataChanged(); Engine::getSong()->setModified(); return true;
}
void SVSTrack::refreshCapabilities(const QJsonObject& editorContext) {
 auto plugin=svs::Registry::instance().plugin(m_pluginId); if(!plugin) return;
 const auto request=++m_capabilityRequest; const auto voice=m_voiceId; auto context=m_parameters;
 for(auto i=editorContext.begin();i!=editorContext.end();++i) context[i.key()]=i.value(); context["language"]=m_language;
 QPointer<SVSTrack> target(this);
 QThreadPool::globalInstance()->start(QRunnable::create([plugin,target,voice,request,context]{
  QString error; auto declaration=plugin->capabilities(voice,context,error); svs::Capabilities parsed;
  if(error.isEmpty()) svs::Capabilities::parse(declaration,parsed,error);
  QMetaObject::invokeMethod(QCoreApplication::instance(),[target,request,parsed,error]{
   if(!target||target->m_capabilityRequest!=request) return;
   if(!error.isEmpty()) { target->m_capabilityDiagnostics={error}; emit target->dataChanged(); return; }
   if(target->m_capabilities.original==parsed.original) return;
   target->m_capabilities=parsed; emit target->dataChanged();
   for(auto* clip:target->getClips()) { auto* current=static_cast<SVSClip*>(clip); current->invalidate(); current->synthesize(); }
  },Qt::QueuedConnection);
 }));
}
gui::TrackView* SVSTrack::createView(gui::TrackContainerView* view) { return new gui::SVSTrackView(this,view); }
bool SVSTrack::play(const TimePos& start,f_cnt_t frames,f_cnt_t offset,int clipNum) {
 if(clipNum>=0||isMuted()||!tryLock()) return false; bool played=false;
 for(auto* base:getClips()) { auto* clip=static_cast<SVSClip*>(base); if(clip->isMuted()||start<clip->startPosition()||start>=clip->endPosition()) continue; auto audio=clip->audio(); if(!audio) continue;
  double localTick=int(start)-int(clip->startPosition())-int(clip->startTimeOffset());
  const double remainder=Engine::getSong()->getTimeline().frameOffset();
  double sampleStart=localTick*Engine::framesPerTick(audio->rate)+remainder*audio->rate/Engine::audioEngine()->outputSampleRate();
  auto bounded=f_cnt_t(std::ceil(std::max(0.,std::min(double(Engine::framesPerTick()),double(int(clip->endPosition())-int(start))*Engine::framesPerTick())-remainder)));
  played=Engine::audioEngine()->addPlayHandle(new SVSPlaybackHandle(this,std::move(audio),sampleStart,bounded,offset))||played;
 } unlock(); return played;
}
void SVSTrack::setPortraitSettings(const QJsonObject& input) {
 auto settings=input; settings["visible"]=input["visible"].toBool(true); settings["transparency"]=std::clamp(input["transparency"].toInt(70),0,100); settings["x"]=std::clamp(input["x"].toDouble(1),0.,1.); settings["y"]=std::clamp(input["y"].toDouble(1),0.,1.);
 if(settings==m_portraitSettings) return; m_portraitSettings=settings; emit dataChanged(); Engine::getSong()->setModified();
}
void SVSTrack::saveTrackSpecificSettings(QDomDocument& doc,QDomElement& node,bool) {
 if(!m_original.isNull()) { auto attrs=m_original.attributes(); for(int i=0;i<attrs.count();++i) node.setAttribute(attrs.item(i).nodeName(),attrs.item(i).nodeValue()); }
 node.setAttribute("schemaVersion",1); node.setAttribute("pluginId",m_pluginId); node.setAttribute("voiceId",m_voiceId); node.setAttribute("voiceVersion",m_voice.version); node.setAttribute("language",m_voice.language); node.setAttribute("nameMode",m_customName?"custom":"followVoice"); m_volume.saveSettings(doc,node,"vol"); m_pan.saveSettings(doc,node,"pan"); m_mix.saveSettings(doc,node,"mixch"); m_bus.effects()->saveState(doc,node);
 node.setAttribute("language",m_language); node.setAttribute("parameters",QString::fromUtf8(QJsonDocument(m_parameters).toJson(QJsonDocument::Compact)));
 node.setAttribute("portraitSettings",QString::fromUtf8(QJsonDocument(m_portraitSettings).toJson(QJsonDocument::Compact)));
}
void SVSTrack::loadTrackSpecificSettings(const QDomElement& node) {
 if(node.hasAttribute("portraitSettings")) setPortraitSettings(QJsonDocument::fromJson(node.attribute("portraitSettings").toUtf8()).object());
 m_original=node.cloneNode(true).toElement(); m_parameters=QJsonDocument::fromJson(node.attribute("parameters").toUtf8()).object(); m_language=node.attribute("language"); m_customName=node.attribute("nameMode")=="custom"; bindVoice(node.attribute("pluginId"),node.attribute("voiceId")); m_volume.loadSettings(node,"vol"); m_pan.loadSettings(node,"pan"); m_mix.loadSettings(node,"mixch"); m_bus.effects()->clear(); auto effects=node.firstChildElement(m_bus.effects()->nodeName()); if(!effects.isNull()) m_bus.effects()->restoreState(effects);
}
}
