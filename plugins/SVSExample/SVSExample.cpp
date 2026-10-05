#include "svs.h"
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
namespace {
struct Note { double start, duration, pitch, power=1; bool soft=false; std::string lyric, id; example::Json phonemes; };
struct Session { std::vector<Note> notes; std::vector<float> audio; example::Json input; std::string voice,feedback; uint32_t rate=48000; double duration=0; std::atomic<bool> cancelled{false}; };
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
 try { s.input=example::Reader(in->input_json).read(); for(uint32_t i=0;i<in->note_count;++i) { const auto& n=in->notes[i]; if(n.size<sizeof(svs_note)||!std::isfinite(n.start_seconds)||!std::isfinite(n.duration_seconds)||!std::isfinite(n.pitch)||n.duration_seconds<=0||n.duration_seconds>600||std::abs(n.start_seconds)>600||n.pitch<0||n.pitch>127) return SVS_INVALID_INPUT; auto params=example::Reader(n.parameters_json).read(); s.notes.push_back({n.start_seconds,n.duration_seconds,n.pitch,std::clamp(params["example.power"].numeric(100),0.,200.)/100,params["example.soft"].boolean,n.lyric?n.lyric:"",n.id?n.id:"",example::Reader(n.phonemes_json).read()}); } } catch(...) { return SVS_FAILED; }
 return SVS_OK;
}
svs_status SVS_CALL render(svs_session handle,svs_result* out) {
 if(!handle||!out||out->size<sizeof(svs_result)) return SVS_INVALID_INPUT;
 auto& s=*static_cast<Session*>(handle);
 try {
 s.audio.assign(static_cast<size_t>(std::ceil(s.duration*s.rate))*2,0.f);
 constexpr double pi=3.14159265358979323846;
 const auto& parameters=s.input["clipParameters"]; const bool full=s.voice=="full";
 const double gain=std::clamp(s.input["trackParameters"]["example.gain"].numeric(1),0.01,2.);
 const double tension=full?std::clamp(parameters["example.tension"].numeric(.25),0.,1.):.25;
 const double breath=full?std::clamp(parameters["example.breath"].numeric(.1),0.,1.):0;
 const double gender=full?std::clamp(parameters["example.gender"].numeric(0),-1.,1.):0;
 for(const auto& n:s.notes) {
  const double frequency=440*std::pow(2,(n.pitch-69)/12);
  const auto first=static_cast<size_t>(std::max(0.,n.start)*s.rate);
  const auto end=std::min(s.audio.size()/2,static_cast<size_t>(std::max(0.,n.start+n.duration)*s.rate));
  for(auto f=first;f<end;++f) { if((f%1024)==0&&s.cancelled) return SVS_CANCELLED; const double t=double(f)/s.rate-n.start; const double envelope=std::max(0.,std::min({1.,t/0.01,(n.duration-t)/0.03})); const double power=full?n.power*(n.soft?.5:1)*std::clamp(n.phonemes["parameters"]["example.phonemeGain"].numeric(1),0.,2.):1; float value=float(0.15*gain*power*envelope*(std::sin(2*pi*frequency*t)+(0.1+tension*.6)*(1+gender*.2)*std::sin(4*pi*frequency*t)+breath*.15*std::sin(2*pi*7133*t)*std::sin(2*pi*7919*t))); s.audio[f*2]+=value; s.audio[f*2+1]+=value; }
 }
 std::string pitch="[",phonemes="["; bool firstPitch=true,firstPhoneme=true;
 for(const auto& note:s.notes) {
  if(full) { if(!firstPitch) pitch+=','; firstPitch=false; pitch+="{\"noteId\":"+example::quote(note.id)+",\"startSeconds\":"+std::to_string(note.start)+",\"durationSeconds\":"+std::to_string(note.duration)+",\"value\":"+std::to_string(note.pitch)+"}"; }
  const auto& symbols=s.input["pronunciations"][note.id]["phonemes"].array;
  for(size_t i=0;i<symbols.size();++i) { if(!firstPhoneme) phonemes+=','; firstPhoneme=false; phonemes+="{\"noteId\":"+example::quote(note.id)+",\"symbol\":"+example::quote(symbols[i].text())+",\"startSeconds\":"+std::to_string(note.start+double(i)*note.duration/symbols.size())+",\"durationSeconds\":"+std::to_string(note.duration/symbols.size())+"}"; }
 }
 double energy=0; for(float value:s.audio) energy+=value*value;
 s.feedback="{\"pitch\":"+pitch+"],\"phonemes\":"+phonemes+"],\"parameters\":"+(full?"{\"example.energy\":"+std::to_string(energy)+"}":"{}")+"}";
 *out={sizeof(svs_result),s.rate,2,static_cast<uint64_t>(s.audio.size()/2),0,s.audio.data(),s.feedback.c_str(),"{}",nullptr}; return SVS_OK;
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
