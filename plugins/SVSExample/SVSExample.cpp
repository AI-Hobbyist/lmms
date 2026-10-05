#include "svs.h"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <new>
#include <string>
#include <vector>
namespace {
struct Note { double start, duration, pitch; std::string lyric; };
struct Session { std::vector<Note> notes; std::vector<float> audio; uint32_t rate=48000; double duration=0; std::atomic<bool> cancelled{false}; };
svs_status SVS_CALL create(const svs_host*, svs_engine* e) { if(!e) return SVS_INVALID_INPUT; *e=new(std::nothrow) int(0); return *e?SVS_OK:SVS_FAILED; }
void SVS_CALL destroy(svs_engine e) { delete static_cast<int*>(e); }
svs_status SVS_CALL catalog(svs_engine, const char** s) {
 *s=R"({"voices":[{"id":"full","name":"SVS Example","version":"0.1.0","languages":["zh","ja","en"],"defaultLanguage":"en","defaultLyric":"la","avatar":"avatar.svg","portrait":"portrait.svg"},{"id":"minimal","name":"SVS Example Lite","version":"0.1.0","languages":["en"],"defaultLanguage":"en","defaultLyric":"la","avatar":"avatar.svg","portrait":"portrait.svg"}]})"; return SVS_OK;
}
svs_status SVS_CALL capabilities(svs_engine,const char*,const char*,const char** s) { *s=R"({"parameters":[],"pitchInput":"none","concurrent":false})"; return SVS_OK; }
void SVS_CALL releaseString(svs_engine,const char*) {}
svs_status SVS_CALL createSession(svs_engine,const char* voice,svs_session* s) { if(!s||!voice||(std::strcmp(voice,"full")&&std::strcmp(voice,"minimal"))) return SVS_INVALID_INPUT; *s=new(std::nothrow) Session; return *s?SVS_OK:SVS_FAILED; }
void SVS_CALL destroySession(svs_session s) { delete static_cast<Session*>(s); }
svs_status SVS_CALL submit(svs_session handle,const svs_snapshot* in) {
 if(!handle||!in||in->size<sizeof(svs_snapshot)||in->sample_rate<8000||in->sample_rate>192000||in->note_count>100000||!std::isfinite(in->duration_seconds)||in->duration_seconds<0||in->duration_seconds>600||(in->note_count&&!in->notes)) return SVS_INVALID_INPUT;
 auto& s=*static_cast<Session*>(handle); s.notes.clear(); s.rate=in->sample_rate; s.duration=in->duration_seconds; s.cancelled=false;
 try { for(uint32_t i=0;i<in->note_count;++i) { const auto& n=in->notes[i]; if(n.size<sizeof(svs_note)||!std::isfinite(n.start_seconds)||!std::isfinite(n.duration_seconds)||!std::isfinite(n.pitch)||n.duration_seconds<=0) return SVS_INVALID_INPUT; s.notes.push_back({n.start_seconds,n.duration_seconds,n.pitch,n.lyric?n.lyric:""}); } } catch(...) { return SVS_FAILED; }
 return SVS_OK;
}
svs_status SVS_CALL render(svs_session handle,svs_result* out) {
 if(!handle||!out||out->size<sizeof(svs_result)) return SVS_INVALID_INPUT;
 auto& s=*static_cast<Session*>(handle);
 try {
 s.audio.assign(static_cast<size_t>(std::ceil(s.duration*s.rate))*2,0.f);
 constexpr double pi=3.14159265358979323846;
 for(const auto& n:s.notes) {
  const double frequency=440*std::pow(2,(n.pitch-69)/12);
  const auto first=static_cast<size_t>(std::max(0.,n.start)*s.rate);
  const auto end=std::min(s.audio.size()/2,static_cast<size_t>(std::max(0.,n.start+n.duration)*s.rate));
  for(auto f=first;f<end;++f) { if((f%1024)==0&&s.cancelled) return SVS_CANCELLED; const double t=double(f)/s.rate-n.start; const double envelope=std::min({1.,t/0.01,(n.duration-t)/0.03}); float value=float(0.15*envelope*(std::sin(2*pi*frequency*t)+0.25*std::sin(4*pi*frequency*t))); s.audio[f*2]+=value; s.audio[f*2+1]+=value; }
 }
 *out={sizeof(svs_result),s.rate,2,static_cast<uint64_t>(s.audio.size()/2),0,s.audio.data(),"{\"pitch\":[],\"phonemes\":[],\"parameters\":{}}","{}",nullptr}; return SVS_OK;
 } catch(...) { return SVS_FAILED; }
}
void SVS_CALL cancel(svs_session h) { if(h) static_cast<Session*>(h)->cancelled=true; }
void SVS_CALL releaseResult(svs_session,svs_result*) {}
}
extern "C" SVS_EXPORT svs_status SVS_CALL svs_get_api(uint32_t major,uint32_t,uint32_t size,svs_api* out) {
 if(major!=SVS_ABI_MAJOR||!out||size<sizeof(svs_api)) return SVS_BAD_ABI;
 *out={sizeof(svs_api),SVS_ABI_MAJOR,SVS_ABI_MINOR,0,create,destroy,catalog,capabilities,releaseString,createSession,destroySession,submit,render,cancel,releaseResult}; return SVS_OK;
}
