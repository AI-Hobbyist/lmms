#include "SVSClip.h"
#include "SVSTrack.h"
#include "SVSViews.h"
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
namespace { QThreadPool& synthesisPool() { static QThreadPool pool; pool.setMaxThreadCount(2); return pool; } }
SVSClip::SVSClip(Track* track):Clip(track),m_id(QUuid::createUuid().toString(QUuid::WithoutBraces)) {
 changeLength(TimePos::ticksPerBar()); setName("SVS");
 connect(Engine::getSong(),&Song::tempoChanged,this,[this]{invalidate(); synthesize();});
 connect(this,&Clip::positionChanged,this,[this]{invalidate(); QTimer::singleShot(0,this,&SVSClip::synthesize);});
}
SVSClip::~SVSClip() { ++m_revision; ++m_generation; }
void SVSClip::invalidate() { ++m_revision; std::atomic_store(&m_audio,std::shared_ptr<const svs::Audio>{}); m_status="Dirty"; emit dataChanged(); }
std::shared_ptr<const svs::Audio> SVSClip::audio() const { auto result=std::atomic_load(&m_audio); return result&&result->revision==m_revision.load()?result:nullptr; }
void SVSClip::setNotes(const QVector<svs::Note>& notes) { setEditorData(notes,m_curves); }
void SVSClip::setEditorData(const QVector<svs::Note>& notes,const svs::Curves& curves) {
 if(notes==m_notes&&curves==m_curves) return;
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
void SVSClip::setEditorState(const QJsonObject& state) { if(state!=m_editorState) { m_editorState=state; Engine::getSong()->setModified(); } }
bool SVSClip::setParameter(const QString& id,const QJsonValue& value) {
 const auto* descriptor=static_cast<SVSTrack*>(getTrack())->capabilities().parameter(id,"clip");
 if(!descriptor||!descriptor->writable||!descriptor->enabled||!descriptor->accepts(value)) return false;
 if(m_parameters.value(id)==value) return true;
 addJournalCheckPoint(); m_parameters[id]=value; static_cast<SVSTrack*>(getTrack())->refreshCapabilities({{"clipParameters",m_parameters}}); invalidate(); synthesize(); Engine::getSong()->setModified(); return true;
}
bool SVSClip::importDictionary(const QByteArray& bytes,QString& error) {
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
 auto* track=static_cast<SVSTrack*>(getTrack()); auto plugin=svs::Registry::instance().plugin(track->pluginId());
 if(!plugin) { m_status="Missing voice/plugin"; emit dataChanged(); return; }
 svs::Input input; input.clipId=m_id; input.voiceId=track->voiceId(); input.generation=m_generation; input.revision=m_revision.load(); input.request=++m_request; input.notes=m_notes; input.rate=Engine::audioEngine()->outputSampleRate(); input.secondsPerTick=60.0/(Engine::getSong()->getTempo()*(DefaultTicksPerBar/4)); input.duration=int(length())*input.secondsPerTick;
 for(const auto& note:m_notes) input.duration=std::max(input.duration,(note.tick+note.duration)*input.secondsPerTick);
 input.document={{"clipId",m_id},{"voiceId",track->voiceId()},{"pluginId",track->pluginId()},{"position",int(startPosition())},{"contentOffset",-int(startTimeOffset())},{"tempo",Engine::getSong()->getTempo()}};
 input.document["trackParameters"]=track->parameters(); input.document["clipParameters"]=m_parameters;
 input.document["curves"]=svs::curvesToJson(m_curves); input.document["secondsPerTick"]=input.secondsPerTick;
 input.document["language"]=track->language(); input.document["capabilities"]=track->capabilities().original;
 input.document["projectDictionaries"]=m_projectDictionaryData;
 QJsonArray dictionaries; for(const auto& dictionary:track->dictionaries()) dictionaries.append(QJsonObject{{"id",dictionary.id},{"version",dictionary.version},{"hash",dictionary.hash},{"language",dictionary.language},{"phonemeSet",dictionary.phonemeSet},{"entries",dictionary.entries}});
 input.document["voiceDictionaries"]=dictionaries;
 m_status="Queued"; emit dataChanged(); QPointer<SVSClip> target(this);
 synthesisPool().start(QRunnable::create([plugin,input,target]{
  QMetaObject::invokeMethod(QCoreApplication::instance(),[target,input]{ if(target&&target->m_revision==input.revision&&target->m_request==input.request) { target->m_status="Rendering"; emit target->dataChanged(); } },Qt::QueuedConnection);
  QString error; auto result=plugin->render(input,error);
  QMetaObject::invokeMethod(QCoreApplication::instance(),[target,input,result,error]{
   if(!target||target->m_id!=input.clipId||target->m_generation!=input.generation||target->m_revision!=input.revision||target->m_request!=input.request) return;
   std::atomic_store(&target->m_audio,result); target->m_status=result?"Ready":"Failed: "+error; emit target->dataChanged();
  },Qt::QueuedConnection);
 }));
}
gui::ClipView* SVSClip::createView(gui::TrackView* view) { return new gui::SVSClipView(this,view); }
Clip* SVSClip::clone() { auto* copy=new SVSClip(getTrack()); QDomDocument doc; auto node=doc.createElement("svsclip"); saveSettings(doc,node); copy->loadSettings(node); copy->m_id=QUuid::createUuid().toString(QUuid::WithoutBraces); for(auto& note:copy->m_notes) note.id=QUuid::createUuid().toString(QUuid::WithoutBraces); copy->invalidate(); return copy; }
void SVSClip::saveSettings(QDomDocument& doc,QDomElement& node) {
 if(!m_original.isNull()) { auto attrs=m_original.attributes(); for(int i=0;i<attrs.count();++i) node.setAttribute(attrs.item(i).nodeName(),attrs.item(i).nodeValue()); for(auto child=m_original.firstChild();!child.isNull();child=child.nextSibling()) if(child.nodeName()!="notes") node.appendChild(doc.importNode(child,true)); }
 node.setAttribute("schemaVersion",1); node.setAttribute("id",m_id); node.setAttribute("pos",node.parentNode().nodeName()=="clipboard"?-1:int(startPosition())); node.setAttribute("len",int(length())); node.setAttribute("off",int(startTimeOffset())); node.setAttribute("muted",isMuted()); node.setAttribute("name",name()); node.setAttribute("autoresize",getAutoResize()); if(color()) node.setAttribute("color",color()->name());
 node.setAttribute("parameters",QString::fromUtf8(QJsonDocument(m_parameters).toJson(QJsonDocument::Compact))); node.setAttribute("projectDictionaries",QString::fromUtf8(QJsonDocument(m_projectDictionaryData).toJson(QJsonDocument::Compact)));
 node.setAttribute("curves",QString::fromUtf8(QJsonDocument(svs::curvesToJson(m_curves)).toJson(QJsonDocument::Compact))); node.setAttribute("editorState",QString::fromUtf8(QJsonDocument(m_editorState).toJson(QJsonDocument::Compact)));
 auto notes=doc.createElement("notes"); node.appendChild(notes);
 for(const auto& n:m_notes) { auto note=doc.createElement("note"); notes.appendChild(note); note.setAttribute("id",n.id); note.setAttribute("tick",QString::number(n.tick,'g',17)); note.setAttribute("duration",QString::number(n.duration,'g',17)); note.setAttribute("pitch",QString::number(n.pitch,'g',17)); note.setAttribute("lyric",n.lyric); note.setAttribute("language",n.language); note.setAttribute("pronunciation",n.pronunciation); note.setAttribute("parameters",QString::fromUtf8(QJsonDocument(n.parameters).toJson(QJsonDocument::Compact))); note.setAttribute("phonemes",QString::fromUtf8(QJsonDocument(n.phonemes).toJson(QJsonDocument::Compact))); }
}
void SVSClip::loadSettings(const QDomElement& node) {
 m_original=node.cloneNode(true).toElement(); m_notes.clear(); m_id=node.attribute("id",m_id); if(node.attribute("pos").toInt()>=0) movePosition(node.attribute("pos").toInt()); changeLength(std::max(1,node.attribute("len").toInt())); setStartTimeOffset(node.attribute("off").toInt()); setMuted(node.attribute("muted").toInt()); setName(node.attribute("name","SVS")); setAutoResize(node.attribute("autoresize","1").toInt()); if(node.hasAttribute("color")) setColor(QColor(node.attribute("color")));
 m_parameters=QJsonDocument::fromJson(node.attribute("parameters").toUtf8()).object(); m_projectDictionaryData=QJsonDocument::fromJson(node.attribute("projectDictionaries").toUtf8()).array();
 m_curves=svs::curvesFromJson(QJsonDocument::fromJson(node.attribute("curves").toUtf8()).object()); m_editorState=QJsonDocument::fromJson(node.attribute("editorState").toUtf8()).object();
 for(auto n=node.firstChildElement("notes").firstChildElement("note");!n.isNull();n=n.nextSiblingElement("note")) { svs::Note note; note.id=n.attribute("id",QUuid::createUuid().toString(QUuid::WithoutBraces)); note.tick=n.attribute("tick").toDouble(); note.duration=n.attribute("duration","48").toDouble(); note.pitch=n.attribute("pitch","60").toDouble(); note.lyric=n.attribute("lyric","la"); note.language=n.attribute("language"); note.pronunciation=n.attribute("pronunciation"); note.parameters=QJsonDocument::fromJson(n.attribute("parameters").toUtf8()).object(); note.phonemes=QJsonDocument::fromJson(n.attribute("phonemes").toUtf8()).object(); m_notes.push_back(note); }
 ++m_generation; invalidate(); QTimer::singleShot(0,this,&SVSClip::synthesize);
}
}
