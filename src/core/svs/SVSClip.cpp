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
void SVSClip::setNotes(const QVector<svs::Note>& notes) { addJournalCheckPoint(); m_notes=notes; invalidate(); synthesize(); Engine::getSong()->setModified(); }
void SVSClip::synthesize() {
 auto* track=static_cast<SVSTrack*>(getTrack()); auto plugin=svs::Registry::instance().plugin(track->pluginId());
 if(!plugin) { m_status="Missing voice/plugin"; emit dataChanged(); return; }
 svs::Input input; input.clipId=m_id; input.voiceId=track->voiceId(); input.generation=m_generation; input.revision=m_revision.load(); input.request=++m_request; input.notes=m_notes; input.rate=Engine::audioEngine()->outputSampleRate(); input.secondsPerTick=60.0/(Engine::getSong()->getTempo()*(DefaultTicksPerBar/4)); input.duration=int(length())*input.secondsPerTick;
 for(const auto& note:m_notes) input.duration=std::max(input.duration,(note.tick+note.duration)*input.secondsPerTick);
 input.document={{"clipId",m_id},{"voiceId",track->voiceId()},{"pluginId",track->pluginId()},{"position",int(startPosition())},{"contentOffset",-int(startTimeOffset())},{"tempo",Engine::getSong()->getTempo()}};
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
 auto notes=doc.createElement("notes"); node.appendChild(notes);
 for(const auto& n:m_notes) { auto note=doc.createElement("note"); notes.appendChild(note); note.setAttribute("id",n.id); note.setAttribute("tick",QString::number(n.tick,'g',17)); note.setAttribute("duration",QString::number(n.duration,'g',17)); note.setAttribute("pitch",QString::number(n.pitch,'g',17)); note.setAttribute("lyric",n.lyric); note.setAttribute("language",n.language); note.setAttribute("pronunciation",n.pronunciation); note.setAttribute("parameters",QString::fromUtf8(QJsonDocument(n.parameters).toJson(QJsonDocument::Compact))); note.setAttribute("phonemes",QString::fromUtf8(QJsonDocument(n.phonemes).toJson(QJsonDocument::Compact))); }
}
void SVSClip::loadSettings(const QDomElement& node) {
 m_original=node.cloneNode(true).toElement(); m_notes.clear(); m_id=node.attribute("id",m_id); if(node.attribute("pos").toInt()>=0) movePosition(node.attribute("pos").toInt()); changeLength(std::max(1,node.attribute("len").toInt())); setStartTimeOffset(node.attribute("off").toInt()); setMuted(node.attribute("muted").toInt()); setName(node.attribute("name","SVS")); setAutoResize(node.attribute("autoresize","1").toInt()); if(node.hasAttribute("color")) setColor(QColor(node.attribute("color")));
 for(auto n=node.firstChildElement("notes").firstChildElement("note");!n.isNull();n=n.nextSiblingElement("note")) { svs::Note note; note.id=n.attribute("id",QUuid::createUuid().toString(QUuid::WithoutBraces)); note.tick=n.attribute("tick").toDouble(); note.duration=n.attribute("duration","48").toDouble(); note.pitch=n.attribute("pitch","60").toDouble(); note.lyric=n.attribute("lyric","la"); note.language=n.attribute("language"); note.pronunciation=n.attribute("pronunciation"); note.parameters=QJsonDocument::fromJson(n.attribute("parameters").toUtf8()).object(); note.phonemes=QJsonDocument::fromJson(n.attribute("phonemes").toUtf8()).object(); m_notes.push_back(note); }
 ++m_generation; invalidate(); QTimer::singleShot(0,this,&SVSClip::synthesize);
}
}
