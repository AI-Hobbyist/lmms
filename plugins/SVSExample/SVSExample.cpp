#include "svs.h"
#include "svs_curve.hpp"
#include "svs_time.hpp"
#include "Json.h"
#include "Schemas.h"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <new>
#include <memory>
#include <string>
#include <vector>
#include <map>
#include <sstream>
#include <iomanip>
#include <chrono>
#include <thread>
namespace {
std::string decimal(double value) { std::ostringstream stream; stream.imbue(std::locale::classic()); stream<<std::setprecision(17)<<value; return stream.str(); }
struct Note { double start, duration, pitch, power=1; bool soft=false; std::string lyric, id; example::Json phonemes; bool continuation=false; double phraseStart=0,phraseEnd=0,tick=0,durationTicks=0; };
struct Session { std::vector<Note> notes; std::vector<float> audio; example::Json input; std::map<std::string,svs_sdk::Curve> curves; std::string voice,feedback; uint32_t rate=48000; double duration=0,secondsPerTick=0,originTick=0;svs_sdk::TempoMap tempo; std::atomic<bool> cancelled{false}; };
double localSeconds(const Session& s,double tick) {return s.tempo.empty()?tick*s.secondsPerTick:s.tempo.secondsAt(s.originTick+tick)-s.tempo.secondsAt(s.originTick);}
double localTick(const Session& s,double seconds) {return s.tempo.empty()?seconds/s.secondsPerTick:s.tempo.tickAt(s.tempo.secondsAt(s.originTick)+seconds)-s.originTick;}
double segmentBegin(const Session& s,const Note& note,const example::Json& segment) {return localSeconds(s,note.tick+segment["startTick"].numeric(0));}
double segmentEnd(const Session& s,const Note& note,const example::Json& segment) {return localSeconds(s,note.tick+segment["startTick"].numeric(0)+segment["durationTicks"].numeric(0));}
bool readCurves(Session& session) {
 session.curves.clear(); session.secondsPerTick=session.input["secondsPerTick"].numeric(0);
 session.originTick=session.input["position"].numeric(0)-session.input["contentOffset"].numeric(0);session.tempo={};
 if(session.input.object.count("tempoMap")) {const auto& json=session.input["tempoMap"];if(json.type!=example::Json::Array||json.array.size()>2*1024*1024) return false;std::vector<svs_sdk::TempoPoint> points;for(const auto& point:json.array) points.push_back({point["tick"].numeric(NAN),point["secondsPerTick"].numeric(NAN)});if(!session.tempo.setPoints(points)) return false;}
 if(session.voice!="full") return true;
 const auto schema=example::Reader(fullSchema).read();
 for(const auto& entry:session.input["curves"].object) {
  const auto& json=entry.second; const bool offset=session.input["capabilities"]["pitch"]["input"].text()=="offset"; const bool reference=offset&&entry.first=="svs.referencePitch"; const bool pitch=entry.first=="svs.pitch"||reference;
  const example::Json* descriptor=nullptr;
  for(const auto& parameter:schema["parameters"].array) if(parameter["id"].text()==entry.first&&parameter["curve"].boolean) descriptor=&parameter;
  if(!pitch&&!descriptor) continue; // Retain unsupported user data in the host.
  if(session.secondsPerTick<=0||!std::isfinite(session.secondsPerTick)||json["points"].type!=example::Json::Array) return false;
  if(pitch&&json["mode"].text()!=(offset&&!reference?"offset":"absolute")) return false;
  svs_sdk::Curve curve; const auto interpolation=json["interpolation"].text();
  curve.constrainTangents=json["constrained"].type!=example::Json::Boolean||json["constrained"].boolean; if(!curve.constrainTangents&&(!offset||reference)) return false;
  if(interpolation=="hermite") curve.interpolation=svs_sdk::Interpolation::Hermite;
  else if(interpolation=="step") curve.interpolation=svs_sdk::Interpolation::Step;
  else if(interpolation!="linear") return false;
  const auto type=pitch?std::string("float"):(*descriptor)["type"].text();
  if(type!="float"&&interpolation!="step") return false;
  double previous=-INFINITY;
  for(const auto& anchor:json["points"].array) {
   svs_sdk::CurvePoint point; point.tick=anchor["tick"].numeric(NAN);
   if(!std::isfinite(point.tick)||point.tick<=previous) return false;
   const auto& value=anchor["value"];
   if(type=="bool") { if(value.type!=example::Json::Boolean) return false; point.value=value.boolean?1:0; }
   else if(type=="enum") { if(value.type!=example::Json::String) return false; point.valueId=value.text(); bool found=false; for(const auto& choice:(*descriptor)["choices"].array) found|=choice["id"].text()==point.valueId; if(!found) return false; }
   else {
    point.value=value.numeric(NAN); const double minimum=pitch?(offset&&!reference?-127:0):(*descriptor)["min"].numeric(0),maximum=pitch?127:(*descriptor)["max"].numeric(1);
    if(!std::isfinite(point.value)||point.value<minimum||point.value>maximum||(type=="int"&&std::floor(point.value)!=point.value)) return false;
   }
   point.automatic=anchor["automatic"].type!=example::Json::Boolean||anchor["automatic"].boolean;
   point.tangentIn=anchor["in"].numeric(0); point.tangentOut=anchor["out"].numeric(0); point.breakAfter=anchor["breakAfter"].boolean;
   if(anchor.object.count("segment")) { const auto segment=anchor["segment"].text(); if(segment!="linear"&&segment!="hermite"&&segment!="step") return false; if(type!="float"&&segment!="step") return false; point.segmentInterpolation=segment=="linear"?0:segment=="hermite"?1:2; }
   curve.points.push_back(std::move(point)); previous=anchor["tick"].number;
  }
  for(const auto& gap:json["gaps"].array) { const double start=gap["start"].numeric(NAN),end=gap["end"].numeric(NAN); if(!std::isfinite(start)||!std::isfinite(end)||start>=end) return false; curve.gaps.push_back({start,end}); }
  session.curves.emplace(entry.first,std::move(curve));
 }
 return true;
}
double curveValue(const Session& session,const char* id,double tick,double fallback) {
 const auto found=session.curves.find(id); if(found==session.curves.end()) return fallback;
 const auto sample=found->second.evaluate(tick); return sample.covered?sample.value:fallback;
}
std::string curveIdValue(const Session& session,const char* id,double tick,const std::string& fallback) {
 const auto found=session.curves.find(id); if(found==session.curves.end()) return fallback;
 const auto sample=found->second.evaluate(tick); return sample.covered&&sample.valueId?*sample.valueId:fallback;
}
double effectivePitch(const Session& session,double tick,double fallback) {
 if(session.input["capabilities"]["pitch"]["input"].text()=="offset") return curveValue(session,"svs.referencePitch",tick,fallback)+curveValue(session,"svs.pitch",tick,0);
 return curveValue(session,"svs.pitch",tick,fallback);
}
svs_status ownedString(const char* value,const char** out) { if(!out||!value) return SVS_INVALID_INPUT; const auto size=std::strlen(value)+1; auto* text=new(std::nothrow) char[size]; if(!text) return SVS_FAILED; std::memcpy(text,value,size); *out=text; return SVS_OK; }
svs_status ownedString(const std::string& value,const char** out) { return ownedString(value.c_str(),out); }
svs_status SVS_CALL create(const svs_host*, svs_engine* e) { if(!e) return SVS_INVALID_INPUT; *e=new(std::nothrow) int(0); return *e?SVS_OK:SVS_FAILED; }
void SVS_CALL destroy(svs_engine e) { delete static_cast<int*>(e); }
svs_status SVS_CALL catalog(svs_engine, const char** s) {
 return ownedString(R"({"voices":[{"id":"full","name":"SVS Example","description":"Deterministic multilingual SDK demonstration","author":"LMMS SVS contributors","license":"GPL-2.0-or-later","version":"0.1.0","languages":["zh","ja","en"],"defaultLanguage":"en","defaultLyric":"la","avatar":"avatar.svg","portrait":"portrait.svg","range":[36,84]},{"id":"minimal","name":"SVS Example Lite","description":"Restricted capability demonstration","author":"LMMS SVS contributors","license":"GPL-2.0-or-later","version":"0.1.0","languages":["en"],"defaultLanguage":"en","defaultLyric":"la","avatar":"avatar.svg","portrait":"portrait.svg"}]})",s);
}
svs_status SVS_CALL capabilities(svs_engine,const char* voice,const char*,const char** s) { if(!voice||(std::strcmp(voice,"full")&&std::strcmp(voice,"minimal"))) return SVS_INVALID_INPUT; return ownedString(std::strcmp(voice,"full")==0?fullSchema:minimalSchema,s); }
void SVS_CALL releaseString(svs_engine,const char* text) { delete[] text; }
svs_status SVS_CALL pronunciation(svs_engine,const char* voice,const char* request,const char** out) {
 if(!voice||(std::strcmp(voice,"full")&&std::strcmp(voice,"minimal"))) return SVS_INVALID_INPUT;
 try { auto input=example::Reader(request).read(); auto text=input["pronunciation"].text(); if(text.empty()) text=input["lyric"].text(); auto schema=example::Reader(std::strcmp(voice,"full")==0?fullSchema:minimalSchema).read();
  std::vector<std::string> tokens; size_t position=0; while(position<text.size()) { auto end=text.find(' ',position); auto token=text.substr(position,end==std::string::npos?end:end-position); if(!token.empty()) tokens.push_back(token); if(end==std::string::npos) break; position=end+1; }
  bool valid=!tokens.empty(); for(const auto& token:tokens) { bool found=false; for(const auto& symbol:schema["pronunciation"]["phonemes"].array) found|=symbol.text()==token; valid&=found; }
  std::string json="{\"generated\":"+std::string(valid?"true":"false")+",\"text\":"+example::quote(text)+",\"source\":\"pluginParser\",\"phonemes\":[";
  if(valid) for(size_t i=0;i<tokens.size();++i) { if(i) json+=','; json+=example::quote(tokens[i]); }
  json+="],\"diagnostic\":"+example::quote(valid?"":"Unknown text; original retained")+"}"; return ownedString(json,out);
 } catch(...) { return SVS_INVALID_INPUT; }
}
svs_status SVS_CALL createSession(svs_engine,const char* voice,svs_session* s) { if(!s||!voice||(std::strcmp(voice,"full")&&std::strcmp(voice,"minimal"))) return SVS_INVALID_INPUT; try { auto session=std::make_unique<Session>(); session->voice=voice; *s=session.release(); return SVS_OK; } catch(...) { return SVS_FAILED; } }
void SVS_CALL destroySession(svs_session s) { delete static_cast<Session*>(s); }
svs_status SVS_CALL submit(svs_session handle,const svs_snapshot* in) {
 if(!handle||!in||in->size<sizeof(svs_snapshot)||in->sample_rate<8000||in->sample_rate>192000||in->note_count>100000||!std::isfinite(in->duration_seconds)||in->duration_seconds<0||in->duration_seconds>600||(in->note_count&&!in->notes)) return SVS_INVALID_INPUT;
 auto& s=*static_cast<Session*>(handle); s.notes.clear(); s.rate=in->sample_rate; s.duration=in->duration_seconds; s.cancelled=false;
 try { s.input=example::Reader(in->input_json).read(); for(uint32_t i=0;i<in->note_count;++i) { const auto& n=in->notes[i]; if(n.size<sizeof(svs_note)||!std::isfinite(n.start_seconds)||!std::isfinite(n.duration_seconds)||!std::isfinite(n.tick)||!std::isfinite(n.duration_tick)||!std::isfinite(n.pitch)||n.duration_seconds<=0||n.duration_seconds>600||n.duration_tick<=0||std::abs(n.start_seconds)>600||n.pitch<0||n.pitch>127) return SVS_INVALID_INPUT; auto params=example::Reader(n.parameters_json).read(); s.notes.push_back({n.start_seconds,n.duration_seconds,n.pitch,std::clamp(params["example.power"].numeric(100),0.,200.)/100,params["example.soft"].boolean,n.lyric?n.lyric:"",n.id?n.id:"",example::Reader(n.phonemes_json).read()});s.notes.back().tick=n.tick;s.notes.back().durationTicks=n.duration_tick; } } catch(...) { return SVS_FAILED; }
 try {
  if(!readCurves(s)) return SVS_INVALID_INPUT;
  const auto phonemeSchema=example::Reader(fullSchema).read();
  if(s.voice=="full") for(const auto& note:s.notes) if(note.phonemes.object.count("segments")) {
   if(note.phonemes["segments"].type!=example::Json::Array||note.phonemes["segments"].array.empty()||s.secondsPerTick<=0) return SVS_INVALID_INPUT;
   double previous=-INFINITY;
   for(const auto& segment:note.phonemes["segments"].array) {
    const double start=segmentBegin(s,note,segment)-note.start,duration=segmentEnd(s,note,segment)-segmentBegin(s,note,segment);
    if(segment["startTick"].type!=example::Json::Number||segment["durationTicks"].type!=example::Json::Number) return SVS_INVALID_INPUT;
    if(!std::isfinite(start)||!std::isfinite(duration)||start<-.2||start<previous-1e-8||duration<.005-1e-8||start+duration>note.duration+1e-8) return SVS_INVALID_INPUT;
    bool known=false; for(const auto& symbol:phonemeSchema["pronunciation"]["phonemes"].array) known|=symbol.text()==segment["symbol"].text(); if(!known) return SVS_INVALID_INPUT;
    const auto& gain=segment["parameters"]["example.phonemeGain"]; if(gain.type!=example::Json::Null&&(gain.type!=example::Json::Number||gain.number<0||gain.number>2)) return SVS_INVALID_INPUT;
    previous=start+duration;
   }
  }
 } catch(...) { return SVS_FAILED; }
 std::stable_sort(s.notes.begin(),s.notes.end(),[](const auto& a,const auto& b){return a.start<b.start;});
 for(size_t i=0;i<s.notes.size();++i) {auto& note=s.notes[i]; note.phraseStart=note.start; note.phraseEnd=note.start+note.duration; note.continuation=s.input["pronunciations"][note.id]["continuation"].boolean&&i>0&&std::abs(s.notes[i-1].start+s.notes[i-1].duration-note.start)<1e-6; if(note.continuation) note.phraseStart=s.notes[i-1].phraseStart;}
 for(size_t i=s.notes.size();i>1;--i) if(s.notes[i-1].continuation) s.notes[i-2].phraseEnd=s.notes[i-1].phraseEnd;
 return SVS_OK;
}
svs_status SVS_CALL render(svs_session handle,svs_result* out) {
 if(!handle||!out||out->size<sizeof(svs_result)) return SVS_INVALID_INPUT;
 auto& s=*static_cast<Session*>(handle);
 try {
 const auto& faults=s.input["developmentFaults"]; const auto delay=int(std::clamp(faults["delayMs"].numeric(0),0.,5000.));
 for(int elapsed=0;elapsed<delay;++elapsed) { if(s.cancelled&&!faults["lateReturn"].boolean) return SVS_CANCELLED; std::this_thread::sleep_for(std::chrono::milliseconds(1)); }
 if(faults["fail"].boolean) return SVS_FAILED;
 double audioStart=0; if(s.voice=="full") for(const auto& note:s.notes) if(!note.phonemes["segments"].array.empty()) audioStart=std::min(audioStart,segmentBegin(s,note,note.phonemes["segments"].array.front()));
 s.audio.assign(static_cast<size_t>(std::ceil((s.duration-audioStart)*s.rate))*2,0.f);
 constexpr double pi=3.14159265358979323846;
 const auto& parameters=s.input["clipParameters"]; const bool full=s.voice=="full";
 const double gain=std::clamp(s.input["trackParameters"]["example.gain"].numeric(1),0.01,2.);
 const double tension=full?std::clamp(parameters["example.tension"].numeric(.25),0.,1.):.25;
 const double breath=full?std::clamp(parameters["example.breath"].numeric(.1),0.,1.):0;
 const double gender=full?std::clamp(parameters["example.gender"].numeric(0),-1.,1.):0;
 double phase=0; for(const auto& n:s.notes) {
  if(!n.continuation) phase=0;
  const auto& segments=n.phonemes["segments"].array;
  const double start=full&&!segments.empty()?std::min(n.start,segmentBegin(s,n,segments.front())):n.start;
  const auto first=static_cast<size_t>(std::max(0.,start-audioStart)*s.rate);
  const auto end=std::min(s.audio.size()/2,static_cast<size_t>(std::max(0.,n.start+n.duration-audioStart)*s.rate));
  for(auto f=first;f<end;++f) {
   if((f%1024)==0&&s.cancelled&&!faults["lateReturn"].boolean) return SVS_CANCELLED;
   const double seconds=audioStart+double(f)/s.rate,t=seconds-n.start,tick=s.secondsPerTick>0?localTick(s,seconds):0;
   const double pitch=full?effectivePitch(s,tick,n.pitch):n.pitch;
   const double frequency=440*std::pow(2,(pitch-69)/12);
   const double envelope=std::max(0.,std::min({1.,(seconds-std::min(start,n.phraseStart))/0.01,(n.phraseEnd-seconds)/0.03}));
   double phonemeGain=std::clamp(n.phonemes["parameters"]["example.phonemeGain"].numeric(1),0.,2.);
   if(full&&!segments.empty()) { bool covered=false; for(const auto& segment:segments) { const auto begin=segmentBegin(s,n,segment),finish=segmentEnd(s,n,segment); if(seconds>=begin&&seconds<finish) { covered=true; phonemeGain=segment["parameters"]["example.phonemeGain"].numeric(phonemeGain); break; } } if(!covered) phonemeGain=0; }
   const double power=full?curveValue(s,"example.power",tick,n.power*100)/100*(curveValue(s,"example.soft",tick,n.soft?1:0)!=0?.5:1)*phonemeGain:1;
   const auto currentTension=full?curveValue(s,"example.tension",tick,tension):tension;
   const bool advanced=curveIdValue(s,"example.mode",tick,s.input["trackParameters"]["example.mode"].text("basic"))=="advanced";
   const auto currentBreath=full&&advanced?curveValue(s,"example.breath",tick,breath):0;
   const auto currentGender=full?curveValue(s,"example.gender",tick,gender):gender;
   float value=float(0.15*gain*power*envelope*(std::sin(phase)+(0.1+currentTension*.6)*(1+currentGender*.2)*std::sin(2*phase)+currentBreath*.15*std::sin(2*pi*7133*t)*std::sin(2*pi*7919*t)));
   s.audio[f*2]+=value; s.audio[f*2+1]+=value; phase=std::fmod(phase+2*pi*frequency/s.rate,2*pi);
  }
 }
 std::string pitch="[",phonemes="["; bool firstPitch=true,firstPhoneme=true;
 for(const auto& note:s.notes) {
  if(full) {
   const double step=s.secondsPerTick>0?4*s.secondsPerTick:note.duration;
   for(double seconds=note.start;seconds<note.start+note.duration;seconds+=step) {
    if(!firstPitch) pitch+=','; firstPitch=false;
    const auto value=effectivePitch(s,s.secondsPerTick>0?localTick(s,seconds):0,note.pitch);
    pitch+="{\"noteId\":"+example::quote(note.id)+",\"startSeconds\":"+decimal(seconds)+",\"durationSeconds\":"+decimal(std::min(step,note.start+note.duration-seconds))+",\"value\":"+decimal(value)+"}";
   }
  }
  if(full&&!note.phonemes["segments"].array.empty()) {
   for(const auto& segment:note.phonemes["segments"].array) { if(!firstPhoneme) phonemes+=','; firstPhoneme=false; phonemes+="{\"noteId\":"+example::quote(note.id)+",\"symbol\":"+example::quote(segment["symbol"].text())+",\"startSeconds\":"+decimal(segmentBegin(s,note,segment))+",\"durationSeconds\":"+decimal(segmentEnd(s,note,segment)-segmentBegin(s,note,segment))+",\"parameters\":"+(segment["parameters"]["example.phonemeGain"].type==example::Json::Number?"{\"example.phonemeGain\":"+decimal(segment["parameters"]["example.phonemeGain"].number)+"}":"{}")+"}"; }
   continue;
  }
  const auto& symbols=s.input["pronunciations"][note.id]["phonemes"].array;
  if(note.continuation&&!symbols.empty()) {if(!firstPhoneme) phonemes+=','; firstPhoneme=false; phonemes+="{\"noteId\":"+example::quote(note.id)+",\"symbol\":"+example::quote(symbols.back().text())+",\"startSeconds\":"+decimal(note.start)+",\"durationSeconds\":"+decimal(note.duration)+"}"; continue;}
  for(size_t i=0;i<symbols.size();++i) { if(!firstPhoneme) phonemes+=','; firstPhoneme=false; phonemes+="{\"noteId\":"+example::quote(note.id)+",\"symbol\":"+example::quote(symbols[i].text())+",\"startSeconds\":"+decimal(note.start+double(i)*note.duration/symbols.size())+",\"durationSeconds\":"+decimal(note.duration/symbols.size())+"}"; }
 }
 double energy=0; for(float value:s.audio) energy+=value*value;
 s.feedback="{\"pitch\":"+pitch+"],\"phonemes\":"+phonemes+"],\"parameters\":"+(full?"{\"example.energy\":"+decimal(energy)+"}":"{}")+"}";
 const double globalOrigin=s.tempo.empty()?s.originTick*s.secondsPerTick:s.tempo.secondsAt(s.originTick);
 *out={sizeof(svs_result),s.rate,2,static_cast<uint64_t>(s.audio.size()/2),globalOrigin+audioStart,s.audio.data(),s.feedback.c_str(),"{}",nullptr}; return SVS_OK;
 } catch(...) { return SVS_FAILED; }
}
void SVS_CALL cancel(svs_session h) { if(h) static_cast<Session*>(h)->cancelled=true; }
void SVS_CALL releaseResult(svs_session,svs_result*) {}
}
extern "C" SVS_EXPORT svs_status SVS_CALL svs_get_api(uint32_t major,uint32_t,uint32_t size,svs_api* out) {
 if(major!=SVS_ABI_MAJOR||!out||size<SVS_API_REQUIRED_SIZE) return SVS_BAD_ABI;
 const auto copied=std::min(size,uint32_t(sizeof(svs_api)));
 const svs_api api{copied,SVS_ABI_MAJOR,SVS_ABI_MINOR,0,create,destroy,catalog,capabilities,releaseString,createSession,destroySession,submit,render,cancel,releaseResult,pronunciation};
 std::memcpy(out,&api,copied); return SVS_OK;
}
