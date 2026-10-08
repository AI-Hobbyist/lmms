// SDK-only ABI, ownership, resource, cancellation and deterministic-example checks.
#include "svs.hpp"
#include "Json.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <mutex>
#include <set>
#include <sstream>
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#else
#include <dlfcn.h>
#endif
namespace {
void require(bool valid,const std::string& message) {if(!valid) throw std::runtime_error(message);}
struct Library {
#ifdef _WIN32
 HMODULE handle=nullptr;
 explicit Library(const std::filesystem::path& path):handle(LoadLibraryW(path.c_str())) {require(handle!=nullptr,"Cannot load SVS library: "+std::to_string(GetLastError()));}
 ~Library() {if(handle) FreeLibrary(handle);}
 svs_get_api_fn entry() const {return reinterpret_cast<svs_get_api_fn>(GetProcAddress(handle,"svs_get_api"));}
#else
 void* handle=nullptr;
 explicit Library(const std::filesystem::path& path):handle(dlopen(path.c_str(),RTLD_NOW|RTLD_LOCAL)) {if(!handle) throw std::runtime_error(dlerror());}
 ~Library() {if(handle) dlclose(handle);}
 svs_get_api_fn entry() const {return reinterpret_cast<svs_get_api_fn>(dlsym(handle,"svs_get_api"));}
#endif
};
std::string json(const example::Json& value) {
 using J=example::Json;switch(value.type) {
 case J::Null:return "null";case J::Boolean:return value.boolean?"true":"false";case J::String:return example::quote(value.string);
 case J::Number:{std::ostringstream out;out.imbue(std::locale::classic());out<<std::setprecision(17)<<value.number;return out.str();}
 case J::Array:{std::string out="[";for(const auto& item:value.array) {if(out.size()>1) out+=',';out+=json(item);}return out+"]";}
 case J::Object:{std::string out="{";for(const auto& entry:value.object) {if(out.size()>1) out+=',';out+=example::quote(entry.first)+":"+json(entry.second);}return out+"}";}
 }throw std::runtime_error("Invalid JSON type");
}
void schema(const example::Json& declaration) {
 using J=example::Json;require(declaration.type==J::Object&&declaration["schemaVersion"].numeric(0)==1,"Invalid capability schema version");require(declaration["languages"].type==J::Array&&!declaration["languages"].array.empty(),"Missing capability languages");std::set<std::string> ids;
 for(const auto* group:{"parameters","feedbackParameters"}) {
  const auto& parameters=declaration[group];require(parameters.type==J::Null||parameters.type==J::Array,"Invalid parameter list");
  for(const auto& parameter:parameters.array) {
   const auto id=parameter["id"].text(),type=parameter["type"].text(),scope=parameter["scope"].text();require(!id.empty()&&ids.insert(id).second,"Duplicate or missing parameter ID: "+id);require(scope=="track"||scope=="clip"||scope=="note"||scope=="phoneme","Invalid parameter scope: "+id);
   const auto& value=parameter["default"];
   if(type=="float"||type=="int") {const auto lo=parameter["min"].numeric(NAN),hi=parameter["max"].numeric(NAN),step=parameter["step"].numeric(NAN),initial=value.numeric(NAN);require(std::isfinite(lo)&&std::isfinite(hi)&&lo<hi&&std::isfinite(step)&&step>0&&std::isfinite(initial)&&initial>=lo&&initial<=hi,"Invalid numeric range/default: "+id);if(type=="int") require(std::floor(lo)==lo&&std::floor(hi)==hi&&std::floor(step)==step&&std::floor(initial)==initial,"Invalid integer parameter: "+id);if(parameter["scale"].text()=="log") require(lo>0,"Invalid logarithmic range: "+id);}
   else if(type=="bool") require(value.type==J::Boolean,"Invalid boolean default: "+id);
   else if(type=="string") require(value.type==J::String,"Invalid string default: "+id);
   else if(type=="directory-list") {require(value.type==J::Array&&!parameter["curve"].boolean,"Invalid directory-list default: "+id);const auto maximum=parameter["maxItems"].numeric(128);require(maximum>=1&&maximum<=128&&value.array.size()<=maximum,"Invalid directory-list bound: "+id);for(const auto& path:value.array) require(path.type==J::String,"Directory-list path must be a UTF-8 string: "+id);}
   else if(type=="enum") {std::set<std::string> choices;for(const auto& choice:parameter["choices"].array) {const auto key=choice["id"].text();require(!key.empty()&&choices.insert(key).second,"Duplicate/missing enum ID: "+id);}require(value.type==J::String&&choices.count(value.string),"Invalid enum default: "+id);}
   else require(false,"Unsupported parameter type: "+id);
   if(parameter["curve"].boolean) {const auto interpolation=parameter["interpolation"].text();require(interpolation=="linear"||interpolation=="hermite"||interpolation=="step","Invalid interpolation: "+id);if(type!="float") require(interpolation=="step","Discrete parameter curve must use step: "+id);}
  }
 }
}
struct Host {
 struct Completion {uint64_t request;svs_status status;std::string diagnostic;};
 std::mutex mutex;std::map<void*,uint64_t> buffers;std::vector<Completion> completed;uint64_t bytes=0;unsigned progress=0;
 ~Host() {for(const auto& buffer:buffers) std::free(buffer.first);}
 static svs_status SVS_CALL allocate(void* context,uint32_t kind,uint64_t bytes,svs_buffer* out) {try {auto& host=*static_cast<Host*>(context);if(!out||out->size<sizeof(*out)||kind>SVS_BUFFER_AUDIO||bytes>(kind==SVS_BUFFER_AUDIO?128u:64u)*1024*1024) return SVS_INVALID_INPUT;std::lock_guard lock(host.mutex);if(bytes>128u*1024*1024-host.bytes) return SVS_FAILED;std::unique_ptr<void,decltype(&std::free)> memory(std::malloc(size_t(std::max(uint64_t(1),bytes))),std::free);if(!memory) return SVS_FAILED;host.buffers.emplace(memory.get(),bytes);host.bytes+=bytes;*out={sizeof(svs_buffer),memory.release(),bytes,&host};return SVS_OK;}catch(...) {return SVS_FAILED;}}
 static void SVS_CALL release(void* context,svs_buffer* buffer) {try {if(!buffer||buffer->size<sizeof(*buffer)||buffer->owner!=context) return;auto& host=*static_cast<Host*>(context);std::lock_guard lock(host.mutex);const auto entry=host.buffers.find(buffer->data);if(entry==host.buffers.end()) return;host.bytes-=entry->second;std::free(entry->first);host.buffers.erase(entry);*buffer={sizeof(svs_buffer)};}catch(...) {}}
 static void SVS_CALL onProgress(void* context,uint64_t,double,const char*) {auto& host=*static_cast<Host*>(context);std::lock_guard lock(host.mutex);++host.progress;}
 static void SVS_CALL onCompleted(void* context,uint64_t request,svs_status status,const char* diagnostic) {try {auto& host=*static_cast<Host*>(context);std::lock_guard lock(host.mutex);host.completed.push_back({request,status,diagnostic?diagnostic:"{}"});}catch(...) {}}
 svs_host table() {return {sizeof(svs_host),this,nullptr,onProgress,allocate,release,onCompleted};}
};
int run(const std::filesystem::path& path) {
 const auto absolute=std::filesystem::absolute(path);Library library(absolute);auto entry=library.entry();require(entry!=nullptr,"Missing svs_get_api");svs_api rejected{};require(entry(SVS_ABI_MAJOR+1,0,sizeof(rejected),&rejected)==SVS_BAD_ABI,"Plugin accepted incompatible ABI major");require(entry(SVS_ABI_MAJOR,0,SVS_API_REQUIRED_SIZE-1,&rejected)==SVS_BAD_ABI,"Plugin accepted undersized API table");const auto api=svs_sdk::negotiate(entry);
 Host host;const auto services=host.table();svs_sdk::Engine engine(entry,&services);const auto catalog=example::Reader(engine.catalog().c_str()).read();require(catalog["voices"].type==example::Json::Array,"Invalid voice catalog");std::set<std::string> voices;
 if(engine.hasCatalogQuery()) {const auto refreshed=example::Reader(engine.catalog(R"({"engineSettings":{},"rescan":false})").c_str()).read();require(refreshed["voices"].type==example::Json::Array,"Invalid refreshed catalog");std::cout<<"PASS optional catalog query / returned string ownership\n";}
 if(engine.hasEngineSettings()) {
  const auto settings=example::Reader(engine.engineSettings().c_str()).read();require(settings["schemaVersion"].numeric(0)==1,"Invalid engine settings schema version");require(!settings["name"].text().empty(),"Missing engine name");
  const auto type=settings["engineType"].text();require(type==SVS_ENGINE_TYPE_AI||type==SVS_ENGINE_TYPE_CONCATENATIVE||type==SVS_ENGINE_TYPE_EXAMPLE,"Invalid engine type");require(settings["engineSettings"].type==example::Json::Array,"Engine settings must be an array");
  auto parameterSchema=settings;for(auto& parameter:parameterSchema.object["engineSettings"].array) {parameter.object["scope"].type=example::Json::String;parameter.object["scope"].string="track";}parameterSchema.object["parameters"]=parameterSchema["engineSettings"];parameterSchema.object["languages"]=example::Reader("[\"en\"]").read();schema(parameterSchema);
 }
 for(const auto& voice:catalog["voices"].array) {
  const auto id=voice["id"].text();require(!id.empty()&&voices.insert(id).second,"Duplicate or missing voice ID");const auto language=voice["defaultLanguage"].text("en"),lyric=voice["defaultLyric"].text("la");const auto declaration=example::Reader(engine.capabilities(id.c_str()).c_str()).read();schema(declaration);
  if(api.open_resource&&api.read_resource&&api.close_resource) for(const auto* key:{"avatar","portrait"}) {const auto resourceId=voice[key].text();if(resourceId.empty()) continue;auto resource=engine.resource(resourceId.c_str());const auto bytes=resource.read();require(!bytes.empty()&&resource.info().id&&resourceId==resource.info().id&&resource.info().content_type&&resource.info().sha256&&std::strlen(resource.info().sha256)==64,"Invalid resource descriptor");}
  example::Json track,clip,note;track.type=clip.type=note.type=example::Json::Object;for(const auto& parameter:declaration["parameters"].array) {auto* target=parameter["scope"].text()=="track"?&track:parameter["scope"].text()=="clip"?&clip:parameter["scope"].text()=="note"?&note:nullptr;if(target) target->object[parameter["id"].text()]=parameter["default"];}
  const bool labelDeclared=note.object.count("example.label")!=0;
  if(labelDeclared) {auto& label=note.object["example.label"];label.type=example::Json::String;label.string="SDK label \"quoted\"";}
  const auto noteParameters=json(note);std::string input="{\"position\":192,\"contentOffset\":0,\"originSeconds\":2,\"secondsPerTick\":0.010416666666666666,\"language\":"+example::quote(language)+",\"trackParameters\":"+json(track)+",\"clipParameters\":"+json(clip)+",\"curves\":{}}";
  const svs_note n{sizeof(svs_note),"sdk-note",0,48,0,.5,60,lyric.c_str(),language.c_str(),"","{}",noteParameters.c_str()};svs_snapshot snapshot{sizeof(svs_snapshot),"sdk-clip",1,1,77,id.c_str(),32000,&n,1,.5,input.c_str()};auto session=engine.session(id.c_str());require(session.submit(snapshot)==SVS_OK,"Snapshot rejected for voice "+id);
  if(api.query_ranges) {const auto ranges=example::Reader(session.ranges().c_str()).read();require(ranges["ranges"].type==example::Json::Array&&!ranges["ranges"].array.empty(),"Invalid synthesis ranges");}
  double energy=0;{auto result=session.render();require(result.status()==SVS_OK,"Render failed for voice "+id);const auto& pcm=result.value();require(pcm.sample_rate==32000&&pcm.channels==2&&pcm.audio&&pcm.frame_count>0&&pcm.frame_count<=16u*1024*1024&&std::isfinite(pcm.start_seconds),"Invalid PCM result");require(std::abs(pcm.start_seconds-2)<1e-6,"Result origin does not match nonzero clip position");for(uint64_t sample=0;sample<pcm.frame_count*2;++sample) {require(std::isfinite(pcm.audio[sample]),"Non-finite PCM");energy+=double(pcm.audio[sample])*pcm.audio[sample];}require(energy>0,"Expected audible example output");if(labelDeclared) {const auto feedback=example::Reader(pcm.feedback_json).read();require(feedback["labels"].array.size()==1&&feedback["labels"].array[0]["noteId"].text()=="sdk-note"&&feedback["labels"].array[0]["value"].text()==note["example.label"].text(),"String parameter did not affect feedback output");}require(session.submit(snapshot)==SVS_INVALID_INPUT,"Wrapper allowed resubmit while holding result");}
  require(host.buffers.empty()&&host.bytes==0,"Host result buffer not returned to allocator");require(session.submit(snapshot)==SVS_OK,"Resubmit failed after release");session.cancel();{auto result=session.render();require(result.status()==SVS_CANCELLED,"Cancellation was not observed");}
  if(api.features&SVS_FEATURE_HOST_BUFFERS) require(host.progress>0&&!host.completed.empty()&&host.completed.back().request==77&&host.completed.back().status==SVS_CANCELLED,"Missing host progress/completion delivery");
  std::cout<<"PASS voice="<<id<<" sampleRate=32000 energy="<<energy<<" ownership/cancel/origin\n";
 }
 require(host.buffers.empty(),"Outstanding host allocations");if(voices.empty()) std::cout<<"PASS empty installed-engine catalog (PCM/cancellation not exercised)\n";std::cout<<"PASS ABI "<<api.major<<'.'<<api.minor<<" voices="<<voices.size()<<" SDK-only conformance\n";return 0;
}
}
#ifdef _WIN32
int wmain(int argc,wchar_t** argv) {if(argc!=2) {std::cerr<<"Usage: SVSConformance <plugin library>\n";return 2;}try {return run(std::filesystem::path(argv[1]));}catch(const std::exception& error) {std::cerr<<"FAIL: "<<error.what()<<'\n';return 1;}}
#else
int main(int argc,char** argv) {if(argc!=2) {std::cerr<<"Usage: SVSConformance <plugin library>\n";return 2;}try {return run(std::filesystem::u8path(argv[1]));}catch(const std::exception& error) {std::cerr<<"FAIL: "<<error.what()<<'\n';return 1;}}
#endif
