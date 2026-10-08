#include "SVSProjectExport.h"
#include "AutomationClip.h"
#include "PathUtil.h"
#include "SampleClip.h"
#include "SampleTrack.h"
#include "Song.h"
#include "SVSClip.h"
#include "SVSTrack.h"
#include <QDomDocument>
#include <QDataStream>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QMap>
#include <QSet>
#include <QSaveFile>
#include <algorithm>
#include <cmath>

namespace lmms::svs {
namespace {
bool varyingMeter(IntModel& model) {
 for(const auto* clip:AutomationClip::clipsForModel(&model)) {
  if(clip->isMuted()||clip->getTrack()->isMuted()) continue;
  QDomDocument document;auto element=document.createElement("automationclip");
  // Existing serialization owns the clip's numeric-node mutex.
  const_cast<AutomationClip*>(clip)->saveSettings(document,element);
  for(auto point=element.firstChildElement("time");!point.isNull();point=point.nextSiblingElement("time")) {
   if(point.attribute("value").toDouble()!=model.value()||point.attribute("outValue",point.attribute("value")).toDouble()!=model.value()) return true;
  }
 }
 return false;
}
int tempoExtent(const std::vector<TempoSnapshot::Layer>& layers) {
 int end=0;
 for(const auto& layer:layers) {
  end=std::max(end,layer.start+layer.length);
  for(const auto& node:layer.nodes) end=std::max(end,layer.start+layer.offset+node.tick);
 }
 return end;
}
}
ProjectExportSnapshot ProjectExport::capture(Song& song) {
 ProjectExportSnapshot result;
 auto& meter=song.getTimeSigModel();result.numerator=meter.getNumerator();result.denominator=meter.getDenominator();
 if((192*result.numerator)%result.denominator||varyingMeter(meter.numeratorModel())||varyingMeter(meter.denominatorModel())) {
  result.error=QStringLiteral("当前宿主不能准确导出变化拍号，未写入文件");return result;
 }
 result.tempo=TempoSnapshot::capture(song,song.getTempo());result.lastTick=tempoExtent(result.tempo->layers);
 for(auto* base:song.tracks()) {
  if(base->type()!=Track::Type::SVS&&base->type()!=Track::Type::Sample) continue;
  ProjectExportTrack track;track.name=base->name();track.singing=base->type()==Track::Type::SVS;
  track.muted=base->isMuted();track.solo=base->isSolo();
  QDomDocument document;auto settings=document.createElement(base->nodeName());base->saveTrackSpecificSettings(document,settings,false);
  track.volume=settings.attribute("vol","100").toDouble()/100.;track.pan=settings.attribute("pan","0").toDouble()/100.;
  if(!settings.elementsByTagName("effect").isEmpty()) result.losses<<QStringLiteral("轨道 %1：外部工程不保留宿主效果链和混音器路由").arg(track.name);
  for(auto* baseClip:base->getClips()) {
   result.lastTick=std::max(result.lastTick,int(baseClip->endPosition()));
   if(auto* clip=dynamic_cast<SVSClip*>(baseClip)) {
    ProjectExportClip captured;captured.name=clip->name();captured.position=int(clip->startPosition());captured.contentOffset=-int(clip->startTimeOffset());captured.length=int(clip->length());captured.muted=clip->isMuted();captured.notes=clip->notes();captured.curves=clip->curves();
    if(clip->readOnly()) result.losses<<QStringLiteral("轨道 %1 / 片段 %2：未知版本的只读字段无法导出").arg(track.name,captured.name);
    if(!clip->parameters().isEmpty()||!clip->globalParameters().isEmpty()||!clip->projectDictionaryData().isEmpty()) result.losses<<QStringLiteral("轨道 %1 / 片段 %2：声库私有参数和工程词典没有统一格式对应").arg(track.name,captured.name);
    track.clips.append(std::move(captured));
   } else if(auto* clip=dynamic_cast<SampleClip*>(baseClip)) {
    ProjectExportAudio captured;captured.name=clip->name();captured.sourcePath=PathUtil::toAbsolute(clip->sampleFile());captured.position=int(clip->startPosition());captured.startOffset=int(clip->startTimeOffset());captured.length=int(clip->length());captured.muted=clip->isMuted();captured.reversed=clip->reversed();captured.amplification=clip->sample().amplification();captured.buffer=clip->sample().buffer();
    if(clip->sampleFile().isEmpty()) captured.sourcePath.clear();
    if(!captured.buffer||captured.buffer->empty()) result.losses<<QStringLiteral("轨道 %1 / 片段 %2：音频缺失或未解码：%3").arg(track.name,captured.name,captured.sourcePath);
    else track.audio.append(std::move(captured));
   }
  }
  if(!track.clips.isEmpty()||!track.audio.isEmpty()) result.tracks.append(std::move(track));
 }
 if(result.tracks.isEmpty()) result.error=QStringLiteral("没有可导出的 SVS 歌声或音频片段");
 result.losses.removeDuplicates();return result;
}

ProjectExportData ProjectExport::build(const ProjectExportSnapshot& snapshot,const std::atomic<bool>* cancelled) {
 ProjectExportData result;result.losses=snapshot.losses;
 auto reject=[&](const QString& error){result.error=error;result.project={};return result;};
 if(!snapshot.valid()) return reject(snapshot.error.isEmpty()?QStringLiteral("导出快照无效"):snapshot.error);
 auto stopped=[&]{return cancelled&&cancelled->load();};
 const int firstBar=int(std::round(1920.*snapshot.numerator/snapshot.denominator));
 QJsonArray tempos;int previous=-1;
 for(int tick=0;tick<=snapshot.lastTick;++tick) {
  if((tick%1024)==0&&stopped()) return reject(QStringLiteral("已取消"));
  const int bpm=snapshot.tempo->tempoAt(tick);
  if(bpm<=0) return reject(QStringLiteral("速度数据无效"));
  if(bpm!=previous) {tempos.append(QJsonObject{{"position",double(tick)*10},{"bpm",bpm}});previous=bpm;}
 }
 if(tempos.isEmpty()) return reject(QStringLiteral("导出时间范围无效"));
 if(!snapshot.tempo->layers.empty()) result.losses<<QStringLiteral("全局速度自动化按每个整数 LMMS tick 采样；保留该采样网格上的完整速度变化");
 auto secondsBetween=[&](double start,double end) {
  double seconds=0;
  while(start<end) {const double next=std::min(end,std::floor(start)+1);seconds+=(next-start)*60./(snapshot.tempo->tempoAt(int(std::floor(start)))*48.);start=next;}
  return seconds;
 };
 QJsonArray tracks;
 for(const auto& source:snapshot.tracks) {
  if(stopped()) return reject(QStringLiteral("已取消"));
  QJsonObject common{{"title",source.name},{"mute",source.muted},{"solo",source.solo},{"volume",source.volume},{"pan",source.pan}};
  if(source.singing) {
   QJsonArray notes;struct PitchPoint {int value=0;bool breakBefore=false;};QMap<int,PitchPoint> pitch;
   for(const auto& clip:source.clips) {
    if(!std::isfinite(clip.position)||!std::isfinite(clip.contentOffset)||!std::isfinite(clip.length)||clip.position<0||clip.length<=0) return reject(QStringLiteral("轨道 %1 / 片段 %2：时间范围无效").arg(source.name,clip.name));
    if(clip.muted) result.losses<<QStringLiteral("轨道 %1 / 片段 %2：目标统一模型没有片段静音字段，导出其数据时无法保留片段静音").arg(source.name,clip.name);
    for(const auto& note:clip.notes) {
     if(!std::isfinite(note.tick)||!std::isfinite(note.duration)||!std::isfinite(note.pitch)||note.duration<=0) return reject(QStringLiteral("轨道 %1：音符数据无效").arg(source.name));
     const double start=std::max(note.tick,clip.contentOffset),end=std::min(note.tick+note.duration,clip.contentOffset+clip.length);
     if(end<=start) continue;
     const double projectStart=clip.position+start-clip.contentOffset,projectEnd=clip.position+end-clip.contentOffset;
     const auto first=std::llround(projectStart*10),last=std::llround(projectEnd*10);
     if(last<=first) return reject(QStringLiteral("轨道 %1：音符在目标整数 tick 精度下长度为零").arg(source.name));
     const auto key=std::llround(note.pitch);if(key<0||key>127) return reject(QStringLiteral("轨道 %1：音符音高超出范围").arg(source.name));
     if(std::abs(projectStart*10-first)>1e-8||std::abs(projectEnd*10-last)>1e-8||std::abs(note.pitch-key)>1e-8) result.losses<<QStringLiteral("轨道 %1：音符时间量化到 480 tick/拍、音符键号量化为整数；编辑音高曲线单独保留").arg(source.name);
     if(!note.parameters.isEmpty()||!note.phonemes.isEmpty()) result.losses<<QStringLiteral("轨道 %1：音符私有参数和音素时长没有统一格式对应").arg(source.name);
     notes.append(QJsonObject{{"start_pos",double(first)},{"length",double(last-first)},{"key_number",int(key)},{"lyric",note.lyric},{"pronunciation",note.pronunciation}});
    }
    for(auto i=clip.curves.begin();i!=clip.curves.end();++i) if(i.key()!="svs.pitch") result.losses<<QStringLiteral("轨道 %1 / 片段 %2：参数 %3 没有确定的统一格式语义").arg(source.name,clip.name,i.key());
    if(!clip.curves.contains("svs.pitch")) continue;
    const auto& curve=clip.curves["svs.pitch"];
    if(curve.mode!="absolute"||curve.unit!="semitone") return reject(QStringLiteral("轨道 %1：音高曲线不是绝对半音单位，无法准确导出").arg(source.name));
    const auto& points=curve.evaluator.points;if(points.empty()) continue;
    const double lower=std::max(clip.contentOffset,points.front().tick),upper=std::min(clip.contentOffset+clip.length,points.back().tick);if(upper<lower) continue;
    const double origin=clip.position-clip.contentOffset;
    const qint64 first=std::llround((origin+lower)*10),last=std::llround((origin+upper)*10);
    if(first<0||last+firstBar>=1073741823||last-first>4000000) return reject(QStringLiteral("轨道 %1：音高范围超出转换协议可处理大小").arg(source.name));
    QMap<int,double> samples;
    for(qint64 tick=first;tick<=last;++tick) {if((tick%1024)==0&&stopped()) return reject(QStringLiteral("已取消"));samples[int(tick)]=std::clamp(tick/10.-origin,lower,upper);}
    for(const auto& point:points) if(point.tick>=lower&&point.tick<=upper) samples[int(std::llround((origin+point.tick)*10))]=point.tick;
    samples[int(first)]=lower;samples[int(last)]=upper;
    bool previousCovered=false;int previousTick=-1;double previousLocal=lower;
    for(auto sample=samples.begin();sample!=samples.end();++sample) {
     const auto value=curve.evaluator.evaluate(sample.value());
     if(!value.covered) {previousCovered=false;continue;}
     if(!std::isfinite(value.value)||value.value<0||value.value>127) return reject(QStringLiteral("轨道 %1：音高曲线数值无效").arg(source.name));
     const bool connected=previousCovered&&sample.key()==previousTick+1&&curve.evaluator.evaluate((previousLocal+sample.value())/2).covered;
     const int cents=int(std::llround(value.value*100));
     if(pitch.contains(sample.key())&&pitch.value(sample.key()).value!=cents) result.losses<<QStringLiteral("轨道 %1：重叠片段的音高在同一时间冲突，按片段稳定顺序采用后一个片段；重叠音符仍全部保留").arg(source.name);
     pitch[sample.key()]={cents,!connected};previousCovered=true;previousTick=sample.key();previousLocal=sample.value();
    }
    result.losses<<QStringLiteral("轨道 %1：音高曲线按 480 tick/拍、整数 cent 采样，断点保持独立").arg(source.name);
   }
   std::vector<QJsonObject> ordered;for(const auto& value:notes) ordered.push_back(value.toObject());std::stable_sort(ordered.begin(),ordered.end(),[](const auto& a,const auto& b){return a["start_pos"].toDouble()<b["start_pos"].toDouble();});notes={};for(const auto& note:ordered) notes.append(note);
   QJsonArray pitchPoints;pitchPoints.append(QJsonArray{-192000,-100});int previousTick=-1;
   for(auto point=pitch.begin();point!=pitch.end();++point) {
    if(previousTick>=0&&(point->breakBefore||point.key()!=previousTick+1)) pitchPoints.append(QJsonArray{previousTick+firstBar,-100});
    if(previousTick<0||point->breakBefore||point.key()!=previousTick+1) pitchPoints.append(QJsonArray{point.key()+firstBar,-100});
    pitchPoints.append(QJsonArray{point.key()+firstBar,point->value});previousTick=point.key();
   }
   if(previousTick>=0) pitchPoints.append(QJsonArray{previousTick+firstBar,-100});pitchPoints.append(QJsonArray{1073741823,-100});
   common["type_"]="Singing";common["note_list"]=notes;common["edited_params"]=QJsonObject{{"pitch",QJsonObject{{"points",pitchPoints}}}};
   if(!notes.isEmpty()||!pitch.isEmpty()) tracks.append(common);
  } else {
   if(source.audio.size()>1) result.losses<<QStringLiteral("音频轨 %1：%2 个片段拆分为独立音频条目，轨道设置复制到每个条目").arg(source.name).arg(source.audio.size());
   for(int index=0;index<source.audio.size();++index) {
    const auto& audio=source.audio[index];if(!audio.buffer||audio.buffer->empty()) continue;
    const double offset=std::max(0.,audio.startOffset),position=audio.position+offset;
    if(audio.length<=offset) continue;
    const qint64 skipped=std::llround(secondsBetween(audio.position+std::min(0.,audio.startOffset),audio.position)*audio.buffer->sampleRate());
    const qint64 frames=std::min(qint64(audio.buffer->size())-skipped,std::llround(secondsBetween(position,audio.position+audio.length)*audio.buffer->sampleRate()));
    if(skipped<0||frames<=0) {result.losses<<QStringLiteral("音频轨 %1 / 片段 %2：有效范围不包含音频采样，将省略").arg(source.name,audio.name);continue;}
    auto track=common;track["type_"]="Instrumental";track["title"]=source.audio.size()>1?QStringLiteral("%1 / %2").arg(source.name,audio.name):source.name;track["offset"]=std::round(position*10);track["mute"]=source.muted||audio.muted;
    const bool whole=skipped==0&&frames==qint64(audio.buffer->size())&&!audio.reversed&&audio.amplification==1&&QFileInfo(audio.sourcePath).isFile();
    if(whole) track["audio_file_path"]=audio.sourcePath;
    else {
     const auto name=QStringLiteral("audio-%1.wav").arg(result.audioFiles.size()+1);track["audio_file_path"]=name;
     result.audioFiles.append({int(tracks.size()),audio,skipped,frames,name});
    }
    tracks.append(track);
   }
  }
 }
 if(tracks.isEmpty()) return reject(QStringLiteral("没有位于片段有效范围内的可导出内容"));
 result.project={{"song_tempo_list",tempos},{"time_signature_list",QJsonArray{QJsonObject{{"bar_index",0},{"numerator",snapshot.numerator},{"denominator",snapshot.denominator}}}},{"track_list",tracks}};
 result.losses.removeDuplicates();return result;
}
bool ProjectExport::writeAudioFiles(const ProjectExportData& prepared,const QString& directory,QString& error,const std::atomic<bool>* cancelled) {
 if(!prepared.valid()) {error=prepared.error;return false;}
 if(!QDir().mkpath(directory)) {error=QStringLiteral("不能创建导出音频暂存目录");return false;}
 for(const auto& file:prepared.audioFiles) {
  const auto& buffer=file.audio.buffer;
  if(!buffer||file.firstFrame<0||file.frameCount<=0||file.firstFrame+file.frameCount>qint64(buffer->size())||file.frameCount>(0xffffffffll-36)/8||!std::isfinite(file.audio.amplification)) {error=QStringLiteral("导出音频范围无效");return false;}
  if(file.fileName!=QFileInfo(file.fileName).fileName()||file.fileName=="."||file.fileName=="..") {error=QStringLiteral("配套音频文件名无效");return false;}
  const auto path=QDir(directory).filePath(file.fileName);if(QFileInfo::exists(path)) {error=QStringLiteral("暂存音频文件已存在：%1").arg(path);return false;}
  QSaveFile output(path);if(!output.open(QIODevice::WriteOnly)) {error=output.errorString();return false;}
  QDataStream stream(&output);stream.setByteOrder(QDataStream::LittleEndian);stream.setFloatingPointPrecision(QDataStream::SinglePrecision);
  stream.writeRawData("RIFF",4);stream<<quint32(36+file.frameCount*8);stream.writeRawData("WAVEfmt ",8);stream<<quint32(16)<<quint16(3)<<quint16(2)<<quint32(buffer->sampleRate())<<quint32(buffer->sampleRate()*8)<<quint16(8)<<quint16(32);stream.writeRawData("data",4);stream<<quint32(file.frameCount*8);
  for(qint64 index=0;index<file.frameCount;++index) {
   if((index%4096)==0&&cancelled&&cancelled->load()) {output.cancelWriting();error=QStringLiteral("已取消");return false;}
   const qint64 source=file.audio.reversed?qint64(buffer->size())-file.firstFrame-index-1:file.firstFrame+index;
   const auto& frame=buffer->data()[source];stream<<float(frame[0]*file.audio.amplification)<<float(frame[1]*file.audio.amplification);
  }
  if(stream.status()!=QDataStream::Ok||!output.commit()) {error=output.errorString();return false;}
 }
 return true;
}
}
