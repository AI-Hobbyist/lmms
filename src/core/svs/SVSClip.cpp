#include "SVSClip.h"
#include "SVSTrack.h"
#include "SVSViews.h"
#include "SVSSynthesisScheduler.h"
#include "SVSCache.h"
#include "SVSTempoSource.h"
#include "SVSNoteOperations.h"
#include "SVSXml.h"
#include "ConfigManager.h"
#include <QScopeGuard>
#include "Engine.h"
#include "Song.h"
#include "AudioEngine.h"
#include <QCoreApplication>
#include <QThreadPool>
#include <QRunnable>
#include <QTimer>
#include <QUuid>
#include <QJsonDocument>
#include <QJsonArray>
namespace lmms {
bool SVSClip::readOnly() const {return !m_migrationDiagnostic.isEmpty()||static_cast<SVSTrack*>(getTrack())->readOnly();}
QString SVSClip::migrationDiagnostic() const {return m_migrationDiagnostic.isEmpty()?static_cast<SVSTrack*>(getTrack())->migrationDiagnostic():m_migrationDiagnostic;}
SVSClip::SVSClip(Track* track):Clip(track),m_id(QUuid::createUuid().toString(QUuid::WithoutBraces)) {
 changeLength(TimePos::ticksPerBar()); setName("SVS");
 connect(&svs::TempoSource::forSong(*Engine::getSong()),&svs::TempoSource::changed,this,[this]{invalidate(); scheduleSynthesis();});
 connect(this,&Clip::positionChanged,this,[this]{invalidate(); scheduleSynthesis();});
 connect(this,&Clip::lengthChanged,this,[this]{invalidate(); scheduleSynthesis();});
}
void SVSClip::scheduleSynthesis() {if(m_synthesisScheduled) return; m_synthesisScheduled=true; QTimer::singleShot(0,this,[this]{m_synthesisScheduled=false;if(m_status=="Dirty") synthesize();});}
void SVSClip::setStartTimeOffset(const TimePos& value) {if((readOnly()&&!m_loading)||value==startTimeOffset()) return; Clip::setStartTimeOffset(value); invalidate(); scheduleSynthesis();}
void SVSClip::movePosition(const TimePos& value) {if(!readOnly()||m_loading) Clip::movePosition(value);}
void SVSClip::changeLength(const TimePos& value) {if(!readOnly()||m_loading) Clip::changeLength(value);}
SVSClip::~SVSClip() { if(m_renderControl) m_renderControl->cancel(); ++m_revision; ++m_generation; }
void SVSClip::cancelSynthesis() { if(m_renderControl) svs::SynthesisScheduler::instance().cancel(m_renderControl); ++m_request; m_status="Cancelled"; std::atomic_store(&m_audio,std::shared_ptr<const svs::Audio>{}); emit dataChanged(); }
void SVSClip::invalidate() { if(m_renderControl) svs::SynthesisScheduler::instance().cancel(m_renderControl); ++m_revision; std::atomic_store(&m_audio,std::shared_ptr<const svs::Audio>{}); m_status=readOnly()?migrationDiagnostic():"Dirty"; emit dataChanged(); }
std::shared_ptr<const svs::Audio> SVSClip::audio() const { auto result=std::atomic_load(&m_audio); return result&&result->revision==m_revision.load()?result:nullptr; }
void SVSClip::setNotes(const QVector<svs::Note>& notes) { setEditorData(notes,m_curves); }
void SVSClip::setEditorData(const QVector<svs::Note>& notes,const svs::Curves& curves) {
 if(readOnly()||(notes==m_notes&&curves==m_curves)) return;
 addJournalCheckPoint(); m_notes=notes; m_curves=curves;
 if(getAutoResize()) {
  double end=TimePos::ticksPerBar();
  for(const auto& note:notes) end=std::max(end,note.tick+note.duration+int(startTimeOffset()));
  const auto bars=std::ceil(end/TimePos::ticksPerBar());
  changeLength(TimePos(int(bars)*TimePos::ticksPerBar()));
 }
 static_cast<SVSTrack*>(getTrack())->refreshCapabilities({{"clipParameters",m_parameters},{"noteCount",notes.size()}});
 invalidate(); synthesize(); Engine::getSong()->setModified();
}
void SVSClip::setEditorState(const QJsonObject& state) { if(!readOnly()&&state!=m_editorState) { m_editorState=state; Engine::getSong()->setModified(); } }
bool SVSClip::setParameter(const QString& id,const QJsonValue& value) {
 if(readOnly()) return false;
 const auto* descriptor=static_cast<SVSTrack*>(getTrack())->capabilities().parameter(id,"clip");
 if(!descriptor||!descriptor->writable||!descriptor->enabled||!descriptor->accepts(value)) return false;
 if(m_parameters.value(id)==value) return true;
 addJournalCheckPoint(); m_parameters[id]=value; static_cast<SVSTrack*>(getTrack())->refreshCapabilities({{"clipParameters",m_parameters}}); invalidate(); synthesize(); Engine::getSong()->setModified(); return true;
}
QJsonValue SVSClip::parameterBase(const svs::Parameter& p) const {
 return svs::parameterBase(p,static_cast<const SVSTrack*>(getTrack())->parameters(),m_parameters,m_globalParameters);
}
bool SVSClip::setGlobalParameter(const QString& id,const QJsonValue& value) {
 auto* track=static_cast<SVSTrack*>(getTrack());
 const svs::Parameter* p=nullptr;for(const auto& parameter:track->capabilities().parameters) if(parameter.id==id) p=&parameter;
 if(readOnly()||!p||!svs::globalParameter(*p)||!p->writable||!p->enabled||!p->accepts(value)) return false;
 if(p->scope=="track") return track->setParameter(id,value);
 if(p->scope=="clip") return setParameter(id,value);
 if(m_globalParameters.value(id)==value) return true;
 addJournalCheckPoint();m_globalParameters[id]=value;invalidate();synthesize();Engine::getSong()->setModified();return true;
}
bool SVSClip::importDictionary(const QByteArray& bytes,QString& error) {
 if(readOnly()) {error=migrationDiagnostic();return false;}
 svs::Dictionary dictionary;
 if(!svs::Dictionary::parse(bytes,static_cast<SVSTrack*>(getTrack())->capabilities().phonemeSet,dictionary,error)) return false;
 const auto& cap=static_cast<SVSTrack*>(getTrack())->capabilities();
 if(dictionary.phonemeSet!=cap.phonemeSetId||!cap.languages.contains(dictionary.language)) { error="Dictionary language/phoneme set is incompatible with this voice"; return false; }
 addJournalCheckPoint(); auto root=QJsonDocument::fromJson(bytes).object(); bool replaced=false;
 for(int i=0;i<m_projectDictionaryData.size();++i) if(m_projectDictionaryData[i].toObject()["id"]==dictionary.id) { m_projectDictionaryData[i]=root; replaced=true; break; }
 if(!replaced) m_projectDictionaryData.append(root);
 invalidate(); synthesize(); Engine::getSong()->setModified(); return true;
}
bool SVSClip::setNoteParameter(const QStringList& ids,const QString& id,const QJsonValue& value,bool phoneme) {
 if(readOnly()) return false;
 const auto* parameter=static_cast<SVSTrack*>(getTrack())->capabilities().parameter(id,phoneme?"phoneme":"note");
 if(!parameter||!parameter->writable||!parameter->enabled||!parameter->accepts(value)) return false;
 auto notes=m_notes; bool changed=false;
 for(auto& note:notes) if(ids.contains(note.id)) {
  if(phoneme) { auto parameters=note.phonemes["parameters"].toObject(); if(parameters[id]!=value) { parameters[id]=value; note.phonemes["parameters"]=parameters; changed=true; } }
  else if(note.parameters[id]!=value) { note.parameters[id]=value; changed=true; }
 }
 if(changed) setNotes(notes); return true;
}
void SVSClip::synthesize() {
 if(readOnly()) {m_status=migrationDiagnostic();emit dataChanged();return;}
 if(m_renderControl) svs::SynthesisScheduler::instance().cancel(m_renderControl);
 auto* track=static_cast<SVSTrack*>(getTrack()); auto plugin=svs::Registry::instance().plugin(track->pluginId());
 if(track->voice().id.isEmpty()) plugin.reset();
 if(plugin&&!track->capabilitiesReady()) {m_status=track->capabilityDiagnostics().isEmpty()?"Queued: capabilities":"Failed: "+track->capabilityDiagnostics().join('\n');emit dataChanged();return;}
 for(const auto& parameter:track->capabilities().parameters) if(parameter.curve&&m_unparsedCurves.contains(parameter.id)) {m_migrationDiagnostic="Invalid SVS curve "+parameter.id+"; original node preserved";invalidate();return;}
 ++m_request;
 auto input=captureInput(Engine::audioEngine()->outputSampleRate());
 if(!plugin&&!captureCachedInput(input)) {m_status="Missing voice/plugin; no valid cached audio";emit dataChanged();return;}
 QPointer<SVSClip> target(this);
 auto current=[target,input]{return target&&target->m_id==input.clipId&&target->m_generation==input.generation&&target->m_revision==input.revision&&target->m_request==input.request;};
 m_renderControl=svs::SynthesisScheduler::instance().submit(plugin,input,0,[target,current](const QString& state){if(current()){target->m_status=state;emit target->dataChanged();}},[target,current,rate=input.rate,cachedOnly=!plugin](std::shared_ptr<const svs::Audio> result,const QString& error){
  if(!current()) return;
  if(result&&!result->cacheKey.isEmpty()) {target->m_cacheKey=result->cacheKey;target->m_cacheInputHash=result->cacheInputHash;target->m_cacheRate=rate;}
  std::atomic_store(&target->m_audio,result); target->m_status=result?(cachedOnly?"Missing voice/plugin: cached audio":"Ready"):"Failed: "+error; emit target->dataChanged();
 });
}
bool SVSClip::captureCachedInput(svs::Input& input) const {
 if(readOnly()||m_cacheKey.size()!=64||m_cacheInputHash.size()!=64||m_cacheRate<8000||m_cacheRate>192000) return false;
 auto cached=captureInput(m_cacheRate);if(svs::Cache::editableKey(cached)!=m_cacheInputHash) return false;
 cached.document["cacheOnlyKey"]=m_cacheKey;input=std::move(cached);return true;
}
svs::Input SVSClip::captureInput(uint32_t rate) const {
 const auto tempo=svs::TempoSource::forSong(*Engine::getSong()).snapshot();
 const auto* track=static_cast<const SVSTrack*>(getTrack());
 svs::Input input; input.clipId=m_id; input.voiceId=track->voiceId(); input.generation=m_generation; input.revision=m_revision.load(); input.request=m_request; input.notes=m_notes; input.rate=rate; input.tempoSnapshot=tempo;input.secondsPerTick=60.0/(tempo->baseTempo*(DefaultTicksPerBar/4)); input.duration=int(length())*input.secondsPerTick;
 for(const auto& note:m_notes) input.duration=std::max(input.duration,(note.tick+note.duration)*input.secondsPerTick);
 double contentEnd=-int(startTimeOffset())+int(length());for(const auto& note:m_notes) contentEnd=std::max(contentEnd,note.tick+note.duration);
 input.document={{"clipId",m_id},{"voiceId",track->voiceId()},{"pluginId",track->pluginId()},{"position",int(startPosition())},{"contentOffset",-int(startTimeOffset())},{"tempo",tempo->baseTempo},{"tempoSource",tempo->toJson()},{"contentEndTick",contentEnd}};
 if(!m_globalParameters.isEmpty()) input.document["globalParameters"]=m_globalParameters;
 input.document["trackParameters"]=track->parameters(); input.document["clipParameters"]=m_parameters;
 input.document["engineSettings"]=QJsonDocument::fromJson(ConfigManager::inst()->value("svsEngineSettings","engine_"+QString::fromLatin1(track->pluginId().toUtf8().toHex())).toUtf8()).object();
 input.document["computeBackend"]="cpu";
 input.document["computeDevice"]="cpu";
 input.document["curves"]=svs::curvesToJson(m_curves); input.document["secondsPerTick"]=input.secondsPerTick;
 input.document["language"]=track->language(); input.document["capabilities"]=track->capabilities().original;
 input.document["projectDictionaries"]=m_projectDictionaryData;
 QJsonArray dictionaries; for(const auto& dictionary:track->dictionaries()) dictionaries.append(QJsonObject{{"id",dictionary.id},{"version",dictionary.version},{"hash",dictionary.hash},{"language",dictionary.language},{"phonemeSet",dictionary.phonemeSet},{"entries",dictionary.entries}});
 input.document["voiceDictionaries"]=dictionaries;
 input.document["queryCapabilities"]=true; input.document["voiceVersion"]=track->voice().version;
 input.document["pluginVersion"]=track->voice().metadata["pluginVersion"];
 return input;
}
gui::ClipView* SVSClip::createView(gui::TrackView* view) { return new gui::SVSClipView(this,view); }
Clip* SVSClip::clone() { auto* copy=new SVSClip(getTrack()); QDomDocument doc; auto node=doc.createElement("svsclip"); saveSettings(doc,node); node.setAttribute("newEntity",1); copy->loadSettings(node); return copy; }
SVSClip* SVSClip::splitAt(const TimePos& projectTick) {
 if(readOnly()) return nullptr;
 const int cut=int(projectTick)-int(startPosition()); if(cut<=0||cut>=int(length())) return nullptr;
 const double begin=-int(startTimeOffset()),middle=begin+cut,end=begin+int(length()); const auto notes=m_notes; const auto curves=m_curves; const auto originalLength=length();
 auto* track=getTrack(); track->addJournalCheckPoint(); track->saveJournallingState(false); saveJournallingState(false); auto* right=static_cast<SVSClip*>(clone()); right->saveJournallingState(false);
 auto restore=qScopeGuard([this,track,right]{right->restoreJournallingState();restoreJournallingState();track->restoreJournallingState();});
 auto range=[&](double first,double last,bool newIds){QVector<svs::Note> result; for(const auto& source:notes) {
  auto note=source; if(note.tick>=last||note.tick+note.duration<=first) continue;
  if(note.tick<first) note=svs::splitNoteForReparse(note,first)->second;
  if(note.tick+note.duration>last) note=svs::splitNoteForReparse(note,last)->first;
  note.tick-=first; if(newIds) note.id=QUuid::createUuid().toString(QUuid::WithoutBraces); result.push_back(note);
 }return result;};
 auto curveRange=[&](double first,double last){svs::Curves result;for(auto i=curves.begin();i!=curves.end();++i) result[i.key()]=i.value().slice(first,last);return result;};
 setAutoResize(false); right->setAutoResize(false); setStartTimeOffset(0); right->setStartTimeOffset(0); right->movePosition(projectTick); changeLength(cut); right->changeLength(int(originalLength)-cut);
 setEditorData(range(begin,middle,false),curveRange(begin,middle)); right->setEditorData(range(middle,end,true),curveRange(middle,end)); Engine::getSong()->setModified(); return right;
}
void SVSClip::saveSettings(QDomDocument& doc,QDomElement& node) {
 if(!m_migrationDiagnostic.isEmpty()) {svs::copyXml(doc,node,m_original);return;}
 if(!m_original.isNull()) { auto attrs=m_original.attributes(); for(int i=0;i<attrs.count();++i) node.setAttribute(attrs.item(i).nodeName(),attrs.item(i).nodeValue()); for(auto child=m_original.firstChild();!child.isNull();child=child.nextSibling()) if(child.nodeName()!="notes") node.appendChild(doc.importNode(child,true)); }
 node.setAttribute("schemaVersion",1); node.setAttribute("id",m_id); node.setAttribute("pos",node.parentNode().nodeName()=="clipboard"?-1:int(startPosition())); node.setAttribute("len",int(length())); node.setAttribute("off",int(startTimeOffset())); node.setAttribute("muted",isMuted()); node.setAttribute("name",name()); node.setAttribute("autoresize",getAutoResize()); if(color()) node.setAttribute("color",color()->name());
 node.setAttribute("cacheKey",m_cacheKey);node.setAttribute("cacheInputHash",m_cacheInputHash);node.setAttribute("cacheSampleRate",m_cacheRate);
 node.setAttribute("globalParameters",QString::fromUtf8(QJsonDocument(m_globalParameters).toJson(QJsonDocument::Compact)));
 node.setAttribute("parameters",QString::fromUtf8(QJsonDocument(m_parameters).toJson(QJsonDocument::Compact))); node.setAttribute("projectDictionaries",QString::fromUtf8(QJsonDocument(m_projectDictionaryData).toJson(QJsonDocument::Compact)));
 auto curves=m_unparsedCurves; const auto known=svs::curvesToJson(m_curves); for(auto i=known.begin();i!=known.end();++i) curves[i.key()]=i.value();
 node.setAttribute("curves",QString::fromUtf8(QJsonDocument(curves).toJson(QJsonDocument::Compact))); node.setAttribute("editorState",QString::fromUtf8(QJsonDocument(m_editorState).toJson(QJsonDocument::Compact)));
 auto notes=doc.createElement("notes"); node.appendChild(notes); svs::applyXmlExtras(doc,notes,m_notesXmlExtras);
 for(const auto& n:m_notes) { auto note=doc.createElement("note"); notes.appendChild(note); svs::applyXmlExtras(doc,note,n.xmlExtras); note.setAttribute("id",n.id); note.setAttribute("tick",QString::number(n.tick,'g',17)); note.setAttribute("duration",QString::number(n.duration,'g',17)); note.setAttribute("pitch",QString::number(n.pitch,'g',17)); note.setAttribute("lyric",n.lyric); note.setAttribute("language",n.language); note.setAttribute("pronunciation",n.pronunciation); note.setAttribute("parameters",QString::fromUtf8(QJsonDocument(n.parameters).toJson(QJsonDocument::Compact))); note.setAttribute("phonemes",QString::fromUtf8(QJsonDocument(n.phonemes).toJson(QJsonDocument::Compact))); }
}
void SVSClip::loadSettings(const QDomElement& node) {
 m_loading=true; m_migrationDiagnostic.clear(); auto loading=qScopeGuard([this]{m_loading=false;}); svs::supportedXmlSchema(node,m_migrationDiagnostic);
 m_original=node.cloneNode(true).toElement(); m_original.removeAttribute("newEntity"); m_notes.clear(); m_id=node.attribute("id",m_id); if(node.attribute("pos").toInt()>=0) movePosition(node.attribute("pos").toInt()); changeLength(std::max(1,node.attribute("len").toInt())); setStartTimeOffset(node.attribute("off").toInt()); setMuted(node.attribute("muted").toInt()); setName(node.attribute("name","SVS")); setAutoResize(node.attribute("autoresize","1").toInt()); if(node.hasAttribute("color")) setColor(QColor(node.attribute("color")));
 m_cacheKey=node.attribute("cacheKey");m_cacheInputHash=node.attribute("cacheInputHash");m_cacheRate=node.attribute("cacheSampleRate").toUInt();
 for(const auto& field:QStringList{"pos","len","off"}) if(node.hasAttribute(field)) {bool valid=false;const auto value=node.attribute(field).toInt(&valid);if(!valid||(field=="len"&&value<=0)) m_migrationDiagnostic="Invalid SVS "+field+"; original node preserved";}
 svs::jsonObjectAttribute(node,"globalParameters",m_globalParameters,m_migrationDiagnostic);
 svs::jsonObjectAttribute(node,"parameters",m_parameters,m_migrationDiagnostic); svs::jsonArrayAttribute(node,"projectDictionaries",m_projectDictionaryData,m_migrationDiagnostic); svs::jsonObjectAttribute(node,"editorState",m_editorState,m_migrationDiagnostic);
 QJsonObject curves; svs::jsonObjectAttribute(node,"curves",curves,m_migrationDiagnostic); m_curves.clear(); m_unparsedCurves={}; for(auto i=curves.begin();i!=curves.end();++i) {svs::Curve curve;QString error;if(i.value().isObject()&&svs::Curve::fromJson(i.value().toObject(),curve,error)) m_curves[i.key()]=curve;else m_unparsedCurves[i.key()]=i.value();}
 const auto& capabilities=static_cast<SVSTrack*>(getTrack())->capabilities(); for(auto i=m_unparsedCurves.begin();i!=m_unparsedCurves.end();++i) if(i.key()=="svs.pitch"||capabilities.parameter(i.key(),"clip")) m_migrationDiagnostic="Invalid SVS curve "+i.key()+"; original node preserved";
 const auto notesRoot=node.firstChildElement("notes"); m_notesXmlExtras=svs::xmlExtras(notesRoot,{}, {"note"}); QSet<QString> ids;
 for(auto n=notesRoot.firstChildElement("note");!n.isNull();n=n.nextSiblingElement("note")) { svs::Note note; note.id=n.attribute("id",QUuid::createUuid().toString(QUuid::WithoutBraces)); bool tickValid=false,durationValid=false,pitchValid=false; note.tick=n.attribute("tick","0").toDouble(&tickValid); note.duration=n.attribute("duration","48").toDouble(&durationValid); note.pitch=n.attribute("pitch","60").toDouble(&pitchValid); note.lyric=n.attribute("lyric","la"); note.language=n.attribute("language"); note.pronunciation=n.attribute("pronunciation"); svs::jsonObjectAttribute(n,"parameters",note.parameters,m_migrationDiagnostic); svs::jsonObjectAttribute(n,"phonemes",note.phonemes,m_migrationDiagnostic);
  note.xmlExtras=svs::xmlExtras(n,{"id","tick","duration","pitch","lyric","language","pronunciation","parameters","phonemes"});
  if(!tickValid||!durationValid||!pitchValid||!std::isfinite(note.tick)||!std::isfinite(note.duration)||!std::isfinite(note.pitch)||note.duration<=0||note.pitch<0||note.pitch>127||ids.contains(note.id)) {m_migrationDiagnostic="Invalid SVS note data; original node preserved";continue;} ids.insert(note.id);m_notes.push_back(note);
 }
 bool newEntity=node.attribute("newEntity")=="1"||node.attribute("pos")=="-1";
 for(auto parent=node.parentNode();!parent.isNull();parent=parent.parentNode()) if(parent.nodeName()=="clonedtrack"||parent.nodeName()=="StateCopy"||parent.nodeName()=="clipboard") newEntity=true;
 if(newEntity) {
  m_id=QUuid::createUuid().toString(QUuid::WithoutBraces);QMap<QString,QString> identities;
  for(auto& note:m_notes) {const auto previous=note.id;note.id=QUuid::createUuid().toString(QUuid::WithoutBraces);identities[previous]=note.id;}
  // Protected data is saved from its original XML. Keep the independent
  // copy's known entity identities there as well, without rewriting payloads.
  if(!m_migrationDiagnostic.isEmpty()) {
   m_original.setAttribute("id",m_id);QSet<QString> copiedIds;
   for(auto note=m_original.firstChildElement("notes").firstChildElement("note");!note.isNull();note=note.nextSiblingElement("note")) {
    auto identity=identities.value(note.attribute("id"));if(identity.isEmpty()||copiedIds.contains(identity)) identity=QUuid::createUuid().toString(QUuid::WithoutBraces);
    copiedIds.insert(identity);note.setAttribute("id",identity);
   }
  }
 }
 ++m_generation; invalidate(); scheduleSynthesis();
}
}
