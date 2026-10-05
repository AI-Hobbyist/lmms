#include "SVSCache.h"
#include <QCryptographicHash>
#include <QDataStream>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>
#include <QStandardPaths>
#include <QDateTime>
#include <cmath>
#include <algorithm>
namespace lmms::svs {
namespace {
QJsonValue feedbackIds(const QJsonValue& value,const QMap<QString,QString>& ids) {
 if(value.isArray()) {QJsonArray array; for(const auto& item:value.toArray()) array.append(feedbackIds(item,ids)); return array;}
 if(!value.isObject()) return value;
 auto object=value.toObject();
 if(object.contains("noteId")) object["noteId"]=ids.value(object["noteId"].toString(),object["noteId"].toString());
 for(auto i=object.begin();i!=object.end();++i) {
  if(i.key()=="pronunciations") {QJsonObject mapped; const auto source=i.value().toObject(); for(auto j=source.begin();j!=source.end();++j) mapped[ids.value(j.key(),j.key())]=feedbackIds(j.value(),ids); i.value()=mapped;}
  else if(i.key()!="noteId") i.value()=feedbackIds(i.value(),ids);
 }
 return object;
}
QMap<QString,QString> noteIds(const Input& input,bool restore) {
 QMap<QString,QString> result; for(int i=0;i<input.notes.size();++i) {const auto stable="@note"+QString::number(i); if(restore) result[stable]=input.notes[i].id; else result[input.notes[i].id]=stable;} return result;
}
std::shared_ptr<const Audio> bindCachedAudio(const std::shared_ptr<const Audio>& source,const Input& input,const QString& key) {
 auto audio=std::make_shared<Audio>(*source); QString error;if(!readTimeMapping(input.document,input.secondsPerTick,audio->mapping,error)) return {};audio->revision=input.revision; audio->feedback=feedbackIds(source->feedback,noteIds(input,true)).toObject(); audio->cacheKey=key;audio->cacheInputHash=Cache::editableKey(input); return audio;
}
constexpr qint64 MaximumFile=136*1024*1024,MaximumMetadata=4*1024*1024;
bool validKey(const QString& key) { if(key.size()!=64) return false; for(const auto ch:key) if(!((ch>='0'&&ch<='9')||(ch>='a'&&ch<='f'))) return false; return true; }
}
Cache& Cache::instance() { static Cache cache(QStandardPaths::writableLocation(QStandardPaths::CacheLocation)+"/svs-v1"); return cache; }
Cache::Cache(QString directory,qint64 memoryLimit,qint64 diskLimit):m_directory(QDir(directory).absolutePath()),m_memoryLimit(std::max(qint64(0),memoryLimit)),m_diskLimit(std::max(qint64(0),diskLimit)) { QDir().mkpath(m_directory); trimDisk(); }
QString Cache::path(const QString& key) const {return validKey(key)?QDir(m_directory).filePath(key+".svscache"):QString{};}
QString Cache::key(const Input& input,const QString& identity) {
 auto document=input.document; document.remove("clipId"); document.remove("queryCapabilities");
 QJsonArray notes; for(const auto& note:input.notes) notes.append(QJsonObject{{"tick",note.tick},{"duration",note.duration},{"pitch",note.pitch},{"lyric",note.lyric},{"language",note.language},{"pronunciation",note.pronunciation},{"parameters",note.parameters},{"phonemes",note.phonemes}});
 document["notes"]=notes; document["pluginIdentity"]=identity; document["voiceId"]=input.voiceId; document["sampleRate"]=int(input.rate); document["channels"]=2; document["format"]="float32"; document["secondsPerTick"]=input.secondsPerTick; document["durationSeconds"]=input.duration;
 return QString::fromLatin1(QCryptographicHash::hash(QJsonDocument(document).toJson(QJsonDocument::Compact),QCryptographicHash::Sha256).toHex());
}
QString Cache::editableKey(const Input& source) {
 auto input=source;
 for(const auto& field:QStringList{"capabilities","voiceDictionaries","tempoMap","cacheOnlyKey"}) input.document.remove(field);
 input.duration=0;return key(input,{});
}
void Cache::remember(const QString& key,std::shared_ptr<const Audio> audio) {
 const auto cost=qint64(audio->samples.size()*sizeof(float)+audio->waveform.bytes()+QJsonDocument(audio->feedback).toJson(QJsonDocument::Compact).size()+(audio->mapping.tempo?audio->mapping.tempo->bytes():0));
 if(cost>m_memoryLimit) return;
 if(m_entries.contains(key)) {m_memoryBytes-=m_entries[key].cost; m_entries.remove(key);}
 while(m_memoryBytes+cost>m_memoryLimit&&!m_entries.isEmpty()) {auto oldest=m_entries.begin(); for(auto i=m_entries.begin();i!=m_entries.end();++i) if(i->access<oldest->access) oldest=i; m_memoryBytes-=oldest->cost; m_entries.erase(oldest);}
 m_entries[key]={std::move(audio),cost,++m_access}; m_memoryBytes+=cost;
}
std::shared_ptr<const Audio> Cache::get(const QString& key,const Input& input) {
 QMutexLocker lock(&m_mutex); if(!validKey(key)) return {};
 auto cached=m_entries.find(key); if(cached!=m_entries.end()) {if(cached->audio->cacheInputHash!=editableKey(input)) return {};cached->access=++m_access;return bindCachedAudio(cached->audio,input,key);}
 QFile file(path(key)); if(!file.open(QIODevice::ReadOnly)||file.size()<40||file.size()>MaximumFile) return {};
 const auto hash=file.read(32),payload=file.read(MaximumFile); if(QCryptographicHash::hash(payload,QCryptographicHash::Sha256)!=hash) {file.close();QFile::remove(path(key));return {};}
 QDataStream stream(payload); stream.setVersion(QDataStream::Qt_6_0); stream.setFloatingPointPrecision(QDataStream::SinglePrecision);
 quint32 magic=0,metadataSize=0; stream>>magic>>metadataSize;
 if(magic!=0x53565331||metadataSize>MaximumMetadata||qint64(metadataSize)+16>payload.size()) return {};
 QByteArray metadata(int(metadataSize),Qt::Uninitialized); if(stream.readRawData(metadata.data(),metadata.size())!=metadata.size()) return {};
 QJsonParseError parse; const auto document=QJsonDocument::fromJson(metadata,&parse); auto info=document.object(); quint64 samples=0; stream>>samples;
 if(parse.error!=QJsonParseError::NoError||!document.isObject()||info["key"].toString()!=key||samples>32*1024*1024||samples%2||stream.device()->bytesAvailable()!=qint64(samples)*4) return {};
 if(info["editableHash"].toString()!=editableKey(input)) return {};
 auto audio=std::make_shared<Audio>(); audio->rate=info["rate"].toInt(); audio->startSeconds=info["startSeconds"].toDouble(); audio->startTick=info["startTick"].toDouble(); audio->feedback=info["feedback"].toObject();audio->cacheInputHash=info["editableHash"].toString();
 if(audio->rate<8000||audio->rate>192000||!std::isfinite(audio->startSeconds)||!std::isfinite(audio->startTick)) return {};
 audio->samples.resize(size_t(samples)); for(auto& value:audio->samples) {stream>>value; if(!std::isfinite(value)) return {};}
 if(stream.status()!=QDataStream::Ok) return {}; audio->waveform.build(audio->samples); remember(key,audio); file.close(); if(file.open(QIODevice::ReadWrite)) file.setFileTime(QDateTime::currentDateTimeUtc(),QFileDevice::FileModificationTime); return bindCachedAudio(audio,input,key);
}
void Cache::put(const QString& key,const Input& input,const std::shared_ptr<const Audio>& source) {
 if(!source||!validKey(key)) return; QMutexLocker lock(&m_mutex);
 auto audio=std::make_shared<Audio>(*source); audio->revision=0; audio->feedback=feedbackIds(audio->feedback,noteIds(input,false)).toObject();audio->cacheInputHash=editableKey(input); remember(key,audio);
 const auto metadata=QJsonDocument(QJsonObject{{"key",key},{"editableHash",audio->cacheInputHash},{"rate",int(audio->rate)},{"startSeconds",audio->startSeconds},{"startTick",audio->startTick},{"feedback",audio->feedback}}).toJson(QJsonDocument::Compact);
 if(metadata.size()>MaximumMetadata||qint64(audio->samples.size())*4+metadata.size()+48>std::min(m_diskLimit,MaximumFile)) return;
 QByteArray payload; QDataStream stream(&payload,QIODevice::WriteOnly); stream.setVersion(QDataStream::Qt_6_0); stream.setFloatingPointPrecision(QDataStream::SinglePrecision); stream<<quint32(0x53565331)<<quint32(metadata.size()); stream.writeRawData(metadata.constData(),metadata.size()); stream<<quint64(audio->samples.size()); for(const auto value:audio->samples) stream<<value;
 QSaveFile file(path(key)); if(file.open(QIODevice::WriteOnly)) {const auto hash=QCryptographicHash::hash(payload,QCryptographicHash::Sha256); if(file.write(hash)!=hash.size()||file.write(payload)!=payload.size()) file.cancelWriting(); else file.commit();} trimDisk();
}
void Cache::trimDisk() {
 const auto files=QDir(m_directory).entryInfoList({"*.svscache"},QDir::Files|QDir::NoSymLinks,QDir::Time|QDir::Reversed); qint64 total=0; for(const auto& file:files) total+=file.size();
 for(const auto& file:files) {if(total<=m_diskLimit) break; if(validKey(file.completeBaseName())&&QFile::remove(file.absoluteFilePath())) total-=file.size();}
}
qint64 Cache::memoryBytes() const {QMutexLocker lock(&m_mutex);return m_memoryBytes;}
qint64 Cache::diskBytes() const {QMutexLocker lock(&m_mutex);qint64 total=0;for(const auto& file:QDir(m_directory).entryInfoList({"*.svscache"},QDir::Files|QDir::NoSymLinks)) total+=file.size();return total;}
void Cache::clearMemory() {QMutexLocker lock(&m_mutex);m_entries.clear();m_memoryBytes=0;}
}
