#include "SVSModel.h"
#include "SVSCapabilities.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLibrary>
#include <QMutex>
#include <QMutexLocker>
#include <QSet>
#include <cmath>
#include "ConfigManager.h"
namespace lmms::svs {
struct Plugin::Impl { QLibrary library; svs_api api{}; svs_engine engine=nullptr; QString error; QMutex mutex; explicit Impl(const QString& path):library(path){} };
Plugin::Plugin(const QString& path):m_impl(std::make_unique<Impl>(path)) {
 auto& d=*m_impl;
 auto get=reinterpret_cast<svs_get_api_fn>(d.library.resolve("svs_get_api"));
 if(!get) { d.error=d.library.errorString(); return; }
 if(get(SVS_ABI_MAJOR,SVS_ABI_MINOR,sizeof(d.api),&d.api)!=SVS_OK||d.api.major!=SVS_ABI_MAJOR||d.api.size<SVS_API_REQUIRED_SIZE||!d.api.create_engine||!d.api.destroy_engine||!d.api.catalog||!d.api.capabilities||!d.api.release_string||!d.api.create_session||!d.api.destroy_session||!d.api.submit||!d.api.render||!d.api.cancel||!d.api.release_result) { d.error="Incompatible SVS ABI"; return; }
 if(d.api.size<sizeof(svs_api)) d.api.pronunciation=nullptr;
 svs_host host{}; host.size=sizeof(host);
 if(d.api.create_engine(&host,&d.engine)!=SVS_OK||!d.engine) d.error="SVS engine initialization failed";
}
Plugin::~Plugin() { if(m_impl->engine) m_impl->api.destroy_engine(m_impl->engine); }
bool Plugin::valid() const { return m_impl->engine&&m_impl->error.isEmpty(); }
QString Plugin::error() const { return m_impl->error; }
QJsonObject Plugin::capabilities(const QString& voice, const QJsonObject& context, QString& error) {
 QMutexLocker lock(&m_impl->mutex); const char* text=nullptr;
 auto voiceBytes=voice.toUtf8(), request=QJsonDocument(context).toJson(QJsonDocument::Compact);
 if(!valid()||m_impl->api.capabilities(m_impl->engine,voiceBytes.constData(),request.constData(),&text)!=SVS_OK||!text) { error="SVS capability query failed"; return {}; }
 QJsonParseError parse; auto document=QJsonDocument::fromJson(text,&parse); m_impl->api.release_string(m_impl->engine,text);
 if(parse.error!=QJsonParseError::NoError||!document.isObject()) { error="Invalid capability JSON"; return {}; }
 error.clear(); return document.object();
}
QJsonObject Plugin::pronunciation(const QString& voice, const QJsonObject& context, QString& error) {
 QMutexLocker lock(&m_impl->mutex); const char* text=nullptr;
 if(!valid()||!m_impl->api.pronunciation) { error="Plugin pronunciation parser unavailable"; return {}; }
 auto voiceBytes=voice.toUtf8(), request=QJsonDocument(context).toJson(QJsonDocument::Compact);
 if(m_impl->api.pronunciation(m_impl->engine,voiceBytes.constData(),request.constData(),&text)!=SVS_OK||!text) { error="SVS pronunciation query failed"; return {}; }
 QJsonParseError parse; auto document=QJsonDocument::fromJson(text,&parse); m_impl->api.release_string(m_impl->engine,text);
 if(parse.error!=QJsonParseError::NoError||!document.isObject()) { error="Invalid pronunciation JSON"; return {}; }
 error.clear(); return document.object();
}
QVector<Voice> Plugin::voices(const QString& package,const QString& id) {
 QMutexLocker lock(&m_impl->mutex); QVector<Voice> voices; const char* text=nullptr;
 if(!valid()||m_impl->api.catalog(m_impl->engine,&text)!=SVS_OK||!text) return voices;
 QJsonParseError error; auto document=QJsonDocument::fromJson(text,&error); m_impl->api.release_string(m_impl->engine,text);
 if(error.error!=QJsonParseError::NoError) { m_impl->error="Invalid voice catalog JSON"; return voices; }
 QSet<QString> ids;
 for(const auto& item:document.object()["voices"].toArray()) { auto v=item.toObject(); auto voiceId=v["id"].toString(); if(voiceId.isEmpty()||ids.contains(voiceId)) { m_impl->error="Duplicate or missing voice ID"; return {}; } ids.insert(voiceId);
  auto resource=[&](const QString& key) { auto path=v[key].toString(); if(path.isEmpty()) return QString{}; auto resolved=QFileInfo(QDir(package).filePath(path)).canonicalFilePath(); auto root=QFileInfo(package).canonicalFilePath()+"/"; return resolved.startsWith(root,Qt::CaseInsensitive)?resolved:QString{}; };
  voices.push_back({id,voiceId,v["name"].toString(),v["version"].toString(),v["defaultLanguage"].toString(),v["defaultLyric"].toString(),resource("avatar"),resource("portrait"),package,v});
 } return voices;
}
std::shared_ptr<const Audio> Plugin::render(const Input& input,QString& error) {
 // v0.1 M1: serialize instance access. M4 adds cancellation and priority scheduling.
 QMutexLocker lock(&m_impl->mutex); auto& d=*m_impl; if(!valid()) { error=d.error; return {}; }
 auto voice=input.voiceId.toUtf8(); svs_session session=nullptr;
 if(d.api.create_session(d.engine,voice.constData(),&session)!=SVS_OK||!session) { error="Voice session unavailable"; return {}; }
 struct Strings { QByteArray id,lyric,language,pronunciation,phonemes,parameters; };
 std::vector<Strings> strings; strings.reserve(input.notes.size()); std::vector<svs_note> notes; notes.reserve(input.notes.size());
 for(const auto& n:input.notes) { strings.push_back({n.id.toUtf8(),n.lyric.toUtf8(),n.language.toUtf8(),n.pronunciation.toUtf8(),QJsonDocument(n.phonemes).toJson(QJsonDocument::Compact),QJsonDocument(n.parameters).toJson(QJsonDocument::Compact)}); auto& s=strings.back(); notes.push_back({sizeof(svs_note),s.id.constData(),n.tick,n.duration,n.tick*input.secondsPerTick,n.duration*input.secondsPerTick,n.pitch,s.lyric.constData(),s.language.constData(),s.pronunciation.constData(),s.phonemes.constData(),s.parameters.constData()}); }
 auto document=input.document; Capabilities cap;
 if(!Capabilities::parse(document["capabilities"].toObject(),cap,error)) { d.api.destroy_session(session); return {}; }
 QVector<Dictionary> voiceDictionaries,projectDictionaries;
 for(const auto& item:document["voiceDictionaries"].toArray()) { auto value=item.toObject(); Dictionary dictionary; dictionary.id=value["id"].toString(); dictionary.version=value["version"].toString(); dictionary.hash=value["hash"].toString(); dictionary.language=value["language"].toString(); dictionary.phonemeSet=value["phonemeSet"].toString(); dictionary.entries=value["entries"].toObject(); voiceDictionaries.push_back(dictionary); }
 QJsonArray dictionaryDiagnostics;
 for(const auto& item:document["projectDictionaries"].toArray()) { Dictionary dictionary; QString diagnostic; if(Dictionary::parse(QJsonDocument(item.toObject()).toJson(QJsonDocument::Compact),cap.phonemeSet,dictionary,diagnostic)&&dictionary.phonemeSet==cap.phonemeSetId&&cap.languages.contains(dictionary.language)) projectDictionaries.push_back(dictionary); else dictionaryDiagnostics.append(QJsonObject{{"dictionaryId",item.toObject()["id"]},{"message",diagnostic.isEmpty()?QString("Dictionary language/phoneme set incompatible with voice"):diagnostic}}); }
 QVector<Note> ordered=input.notes; std::stable_sort(ordered.begin(),ordered.end(),[](const Note& a,const Note& b){return a.tick<b.tick;});
 QJsonObject pronunciations;
 for(int i=0;i<ordered.size();++i) {
  const auto& note=ordered[i]; auto resolved=resolvePronunciation(note,cap,voiceDictionaries,projectDictionaries,document["language"].toString(cap.defaultLanguage),i?&ordered[i-1]:nullptr); auto value=resolved.toJson();
  if(!resolved.generated&&resolved.source!="continuation"&&!note.phonemes.contains("symbols")&&cap.languages.contains(note.language.isEmpty()?document["language"].toString():note.language)&&d.api.pronunciation) {
   auto request=QJsonDocument(QJsonObject{{"lyric",note.lyric},{"pronunciation",note.pronunciation},{"language",note.language.isEmpty()?document["language"].toString():note.language}}).toJson(QJsonDocument::Compact); const char* text=nullptr;
   if(d.api.pronunciation(d.engine,voice.constData(),request.constData(),&text)==SVS_OK&&text) { auto parsed=QJsonDocument::fromJson(text).object(); d.api.release_string(d.engine,text); if(!parsed.isEmpty()) { value=parsed; if(!note.pronunciation.isEmpty()) value["source"]="manualPronunciation"; } }
  }
  pronunciations[note.id]=value;
 }
 document["pronunciations"]=pronunciations;
 auto id=input.clipId.toUtf8(); auto json=QJsonDocument(document).toJson(QJsonDocument::Compact);
 svs_snapshot snapshot{sizeof(svs_snapshot),id.constData(),input.generation,input.revision,input.request,voice.constData(),input.rate,notes.data(),uint32_t(notes.size()),input.duration,json.constData()};
 auto status=d.api.submit(session,&snapshot); svs_result result{}; result.size=sizeof(result);
 if(status==SVS_OK) status=d.api.render(session,&result);
 std::shared_ptr<Audio> audio;
 if(status==SVS_OK&&result.size>=sizeof(result)&&result.channels==2&&result.sample_rate>=8000&&result.sample_rate<=192000&&result.frame_count<=16*1024*1024&&(!result.frame_count||result.audio)) {
  audio=std::make_shared<Audio>(); audio->rate=result.sample_rate; audio->revision=input.revision; if(result.frame_count) audio->samples.assign(result.audio,result.audio+result.frame_count*2); audio->feedback=QJsonDocument::fromJson(result.feedback_json?result.feedback_json:"{}").object(); audio->feedback["pronunciations"]=pronunciations; audio->feedback["dictionaryDiagnostics"]=dictionaryDiagnostics;
  for(float sample:audio->samples) if(!std::isfinite(sample)) { error="Non-finite SVS audio"; audio.reset(); break; }
 } else error=QString("SVS synthesis failed (%1)").arg(status);
 d.api.release_result(session,&result); d.api.destroy_session(session); return audio;
}
Registry& Registry::instance() { static Registry registry; return registry; }
const QVector<Voice>& Registry::voices() {
 if(m_scanned) return m_voices; m_scanned=true;
 QStringList roots{QCoreApplication::applicationDirPath()+"/svs",QCoreApplication::applicationDirPath()+"/../svs",ConfigManager::inst()->dataDir()+"svs"};
 if(qEnvironmentVariableIsSet("LMMS_SVS_PLUGIN_DIR")) roots.prepend(qEnvironmentVariable("LMMS_SVS_PLUGIN_DIR"));
 for(const auto& root:roots) for(const auto& folder:QDir(root).entryList(QDir::Dirs|QDir::NoDotAndDotDot)) {
  QString package=QDir(root).filePath(folder); QFile file(package+"/manifest.json"); if(!file.open(QIODevice::ReadOnly)) continue;
  auto manifest=QJsonDocument::fromJson(file.read(65537)).object(); auto id=manifest["id"].toString(); auto entry=manifest["entry"].toString();
  if(id.isEmpty()||manifest["category"]!="Singing Voice Synthesis"||manifest["apiMajor"].toInt()!=SVS_ABI_MAJOR||entry.contains('/')||entry.contains('\\')||entry.contains("..")) { m_diagnostics<<"Invalid SVS manifest: "+package; continue; }
  if(m_plugins.contains(id)) continue; // same installed package may be visible through two roots
  auto plugin=std::make_shared<Plugin>(package+"/"+entry); if(!plugin->valid()) { m_diagnostics<<id+": "+plugin->error(); continue; }
  auto voices=plugin->voices(package,id); if(voices.isEmpty()) { m_diagnostics<<id+": invalid/empty voice catalog"; continue; }
  m_plugins[id]=plugin; m_voices+=voices;
 } return m_voices;
}
std::shared_ptr<Plugin> Registry::plugin(const QString& id) { voices(); return m_plugins.value(id); }
}
