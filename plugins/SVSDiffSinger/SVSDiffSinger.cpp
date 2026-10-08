/* Native DiffSinger pronunciation/duration; PCM synthesis is stage A3. */
#include "svs.h"
#include "VoiceCatalog.h"
#include "NativeRuntime.h"
#include "Pronunciation.h"
#include "Duration.h"
#include "Speaker.h"
#include "Synthesis.h"
#include <algorithm>
#include <cstring>
#include <new>
#include <string>
#include <mutex>

namespace {
using diffsinger::Json;
struct Engine {
    Ort::Env environment{ORT_LOGGING_LEVEL_WARNING, "DiffSinger"};
    std::shared_ptr<const diffsinger::Catalog> catalog=std::make_shared<diffsinger::Catalog>();
    std::mutex scanMutex;
    Json scanContext;
};
struct ResourceHandle {std::shared_ptr<const diffsinger::Resource> resource;};
struct Session {
    Engine* engine=nullptr;
    std::shared_ptr<const diffsinger::VoicePackage> voice;
    std::vector<diffsinger::NoteInput> notes;
    Json input=Json::object();
    std::unique_ptr<diffsinger::Duration> duration;
    std::unique_ptr<diffsinger::Synthesis> synthesis;
    std::vector<float> audio;
    std::atomic<bool> cancelled{false};
    std::mutex mutex;
    std::string error,feedback;
    uint32_t rate=48000;
    bool submitted=false;
};
svs_status text(const char* value, const char** out)
{
    if (!out) { return SVS_INVALID_INPUT; }
    *out = nullptr;
    const auto size = std::strlen(value) + 1;
    auto* result = new (std::nothrow) char[size];
    if (!result) { return SVS_FAILED; }
    std::memcpy(result, value, size);
    *out = result;
    return SVS_OK;
}
svs_status SVS_CALL create(const svs_host* host, svs_engine* out)
{
    if (!out) { return SVS_INVALID_INPUT; }
    *out = nullptr;
    try {
        diffsinger::initializeRuntime();
        *out = new Engine;
        return SVS_OK;
    } catch (const std::exception& error) {
        if (host && SVS_HAS_FIELD(*host, svs_host, log) && host->log) {
            host->log(host->context, 2, error.what());
        }
        return SVS_FAILED;
    }
}
void SVS_CALL destroy(svs_engine engine) { delete static_cast<Engine*>(engine); }
svs_status SVS_CALL catalog(svs_engine engine, const char** out)
{
    if (!engine) { return SVS_INVALID_INPUT; }
    try {return text(std::atomic_load(&static_cast<Engine*>(engine)->catalog)->declaration().dump().c_str(),out);}catch(...) {return SVS_FAILED;}
}
svs_status SVS_CALL queryCatalog(svs_engine handle,const char* request,const char** out)
{
    if(!handle||!out) {return SVS_INVALID_INPUT;}*out=nullptr;
    try {
        auto& engine=*static_cast<Engine*>(handle);const auto context=Json::parse(request?request:"{}");
        if(!context.is_object()) {throw std::runtime_error("Catalog context must be an object");}
        std::lock_guard lock(engine.scanMutex);auto current=std::atomic_load(&engine.catalog);
        const auto settings=context.value("engineSettings",Json::object());
        if(!settings.is_object()) {throw std::runtime_error("Engine settings must be an object");}
        const auto steps=settings.value("diffsinger.renderSteps",Json(20));
        if(!steps.is_number_integer()||steps.get<int64_t>()<1||steps.get<int64_t>()>100) {throw std::runtime_error("Rendering steps must be an integer in 1-100");}
        const auto directories=settings.value("diffsinger.voicebankDirectories",Json::array());
        if(current->revision==0||context.value("rescan",false)||engine.scanContext.value("directories",Json())!=directories) {
            auto next=diffsinger::scan(context,current->revision+1);std::atomic_store(&engine.catalog,next);engine.scanContext={{"directories",directories}};current=std::move(next);
        }
        return text(current->declaration().dump().c_str(),out);
    }catch(const std::exception& error) {text(Json{{"message",error.what()},{"stage","catalog"}}.dump().c_str(),out);return SVS_INVALID_INPUT;}
}
svs_status SVS_CALL settings(svs_engine engine, const char*, const char** out)
{
    if (!engine) { return SVS_INVALID_INPUT; }
    try {return text(diffsinger::engineSettings().dump().c_str(),out);}catch(...) {return SVS_FAILED;}
}
svs_status SVS_CALL capabilities(svs_engine handle, const char* id, const char*, const char** out) {
    if(!handle||!id||!out) {return SVS_INVALID_INPUT;}
    try {const auto voice=std::atomic_load(&static_cast<Engine*>(handle)->catalog)->find(id);if(!voice) {return SVS_INVALID_INPUT;}
        const auto info=voice->declaration();auto phonemes=Json::array();for(auto item=voice->stages.at("acoustic").phonemes.begin();item!=voice->stages.at("acoustic").phonemes.end();++item) {phonemes.push_back(item.key());}
        Json schema{{"schemaVersion",1},{"languages",info["languages"]},{"defaultLanguage",info["defaultLanguage"]},{"noteLanguage",info["languages"].size()>1},
            {"parameters",Json::array()},{"feedbackParameters",Json::array()},{"pronunciation",{{"phonemeSet","diffsinger:"+voice->fingerprint},{"phonemes",phonemes}}},
            {"synthesis",{{"available",true},{"cancel",true},{"concurrent",false},{"channels",2},{"format","float32"},{"backend","CPU"}}}};
        schema["phonemes"]={{"timingEditable",true},{"attributesEditable",false},{"minimumDurationSeconds",.005},{"maximumLeadSeconds",.15}};
        schema["pronunciation"]["parser"]="diffsinger.native.v1";schema["pronunciation"]["continuation"]="-";
        schema["synthesis"]["segmented"]={{"split","rests"},{"version",1},{"paddingSeconds",.65}};
        const auto choices=diffsinger::speakerChoices(*voice);if(!choices.empty()) {schema["parameters"].push_back({{"id","diffsinger.speaker"},{"name","Speaker"},{"group","Voice"},{"scope","track"},{"type","enum"},{"default",choices[0]["id"]},{"choices",choices},{"curve",false}});}
        diffsinger::Synthesis::declareParameters(*voice,schema);
        return text(schema.dump().c_str(),out);
    }catch(...) {return SVS_FAILED;}
}
svs_status SVS_CALL openResource(svs_engine handle,const char* id,svs_resource* out,svs_resource_info* info) {
    if(!handle||!id||!out||!info||info->size<sizeof(*info)) {return SVS_INVALID_INPUT;}*out=nullptr;
    try {const auto catalog=std::atomic_load(&static_cast<Engine*>(handle)->catalog);for(const auto& voice:catalog->voices) {for(const auto& entry:voice->resources) {const auto& resource=entry.second;if(resource->id!=id) {continue;}auto result=std::make_unique<ResourceHandle>();result->resource=resource;*info={sizeof(*info),resource->id.c_str(),resource->mime.c_str(),resource->bytes.size(),resource->sha256.c_str()};*out=result.release();return SVS_OK;}}return SVS_INVALID_INPUT;}catch(...) {return SVS_FAILED;}
}
svs_status SVS_CALL readResource(svs_engine engine,svs_resource handle,uint64_t offset,void* destination,uint64_t capacity,uint64_t* count) {
    if(!engine||!handle||!count||(!destination&&capacity)) {return SVS_INVALID_INPUT;}*count=0;const auto& bytes=static_cast<ResourceHandle*>(handle)->resource->bytes;if(offset>bytes.size()) {return SVS_INVALID_INPUT;}const auto size=std::min(capacity,uint64_t(bytes.size()-offset));if(size) {std::memcpy(destination,bytes.data()+offset,size);}*count=size;return SVS_OK;
}
void SVS_CALL closeResource(svs_engine,svs_resource handle) {delete static_cast<ResourceHandle*>(handle);}
void SVS_CALL releaseString(svs_engine, const char* value) { delete[] value; }
svs_status SVS_CALL pronunciation(svs_engine handle,const char* id,const char* request,const char** out) {
    if(!handle||!id||!out) {return SVS_INVALID_INPUT;}*out=nullptr;
    try {const auto voice=std::atomic_load(&static_cast<Engine*>(handle)->catalog)->find(id);if(!voice) {return SVS_INVALID_INPUT;}
        return text(diffsinger::Pronunciation(voice).resolve(Json::parse(request?request:"{}")).dump().c_str(),out);
    }catch(const std::exception& error) {return text(Json{{"generated",false},{"diagnostic",error.what()},{"phonemes",Json::array()}}.dump().c_str(),out);}
}
svs_status SVS_CALL createSession(svs_engine handle, const char* id, svs_session* out)
{
    if(!handle||!id||!out) {return SVS_INVALID_INPUT;}*out=nullptr;
    try {auto session=std::make_unique<Session>();session->engine=static_cast<Engine*>(handle);session->voice=std::atomic_load(&session->engine->catalog)->find(id);if(!session->voice) {return SVS_INVALID_INPUT;}*out=session.release();return SVS_OK;}catch(...) {return SVS_FAILED;}
}
void SVS_CALL destroySession(svs_session handle) {
    if(!handle) {return;}auto* session=static_cast<Session*>(handle);session->cancelled=true;
    {std::lock_guard lock(session->mutex);}delete session;
}
svs_status SVS_CALL submit(svs_session handle,const svs_snapshot* snapshot) {
    if(!handle||!snapshot||snapshot->size<sizeof(*snapshot)||snapshot->note_count>4096||(snapshot->note_count&&!snapshot->notes)||snapshot->sample_rate<8000||snapshot->sample_rate>192000) {return SVS_INVALID_INPUT;}
    auto& session=*static_cast<Session*>(handle);std::lock_guard lock(session.mutex);session.submitted=false;
    try {
        if(!snapshot->voice_id||session.voice->id!=snapshot->voice_id) {throw std::runtime_error("Snapshot voice does not match session");}
        const char* json=snapshot->input_json?snapshot->input_json:"{}";if(std::strlen(json)>16*1024*1024) {throw std::runtime_error("Snapshot exceeds JSON size bound");}
        auto input=Json::parse(json);if(!input.is_object()) {throw std::runtime_error("Snapshot input must be an object");}
        std::vector<diffsinger::NoteInput> notes;notes.reserve(snapshot->note_count);
        for(uint32_t i=0;i<snapshot->note_count;++i) {const auto& source=snapshot->notes[i];if(source.size<sizeof(source)) {throw std::runtime_error("Truncated note ABI");}
            diffsinger::NoteInput note;note.id=source.id?source.id:"";note.lyric=source.lyric?source.lyric:"";note.language=source.language?source.language:"";note.reading=source.pronunciation?source.pronunciation:"";
            note.tick=source.tick;note.durationTick=source.duration_tick;note.start=source.start_seconds;note.duration=source.duration_seconds;note.pitch=source.pitch;
            note.phonemes=Json::parse(source.phonemes_json?source.phonemes_json:"{}");if(!note.phonemes.is_object()) {throw std::runtime_error("Note phonemes must be an object");}
            if(note.language.empty()) {note.language=input.value("language",session.voice->declaration().value("defaultLanguage",std::string("zh")));}
            const auto pronunciation=input.value("pronunciations",Json::object());if(pronunciation.contains(note.id)) {note.pronunciation=pronunciation.at(note.id);}
            notes.push_back(std::move(note));
        }
        session.input=std::move(input);session.notes=std::move(notes);session.rate=snapshot->sample_rate;session.cancelled=false;session.error.clear();session.feedback.clear();session.submitted=true;return SVS_OK;
    }catch(const std::exception& error) {session.error=Json{{"stage","submit"},{"voiceId",session.voice->id},{"message",error.what()}}.dump();return SVS_INVALID_INPUT;}
}
svs_status SVS_CALL render(svs_session handle,svs_result* out) {
    if(!handle||!out||out->size<sizeof(*out)) {return SVS_INVALID_INPUT;}auto& session=*static_cast<Session*>(handle);std::lock_guard lock(session.mutex);
    const char* activeStage="duration";
    try {
        if(!session.submitted) {throw std::runtime_error("No submitted snapshot");}
        svs_sdk::TempoMap tempo;std::vector<svs_sdk::TempoPoint> points;
        const auto map=session.input.value("tempoMap",Json::array());if(!map.is_array()||map.size()>2*1024*1024) {throw std::runtime_error("Invalid tempo map");}
        for(const auto& point:map) {points.push_back({point.at("tick").get<double>(),point.at("secondsPerTick").get<double>()});}
        if(points.empty()) {points.push_back({0,session.input.value("secondsPerTick",0.)});}if(!tempo.setPoints(points)) {throw std::runtime_error("Invalid tempo points");}
        if(!session.duration) {session.duration=std::make_unique<diffsinger::Duration>(session.engine->environment,session.voice);}
        const double origin=session.input.value("position",0.)-session.input.value("contentOffset",0.);
        const auto plan=session.duration->predict(session.notes,tempo,origin,session.input.value("trackParameters",Json::object()),session.cancelled);
        Json phones=Json::array();for(const auto& phone:plan.phones) {if(!phone.noteId.empty()) {phones.push_back({{"noteId",phone.noteId},{"symbol",phone.symbol},{"startSeconds",phone.start},{"durationSeconds",phone.end-phone.start}});}}
        activeStage="synthesis";if(!session.synthesis) {session.synthesis=std::make_unique<diffsinger::Synthesis>(session.engine->environment,session.voice);}
        auto rendered=session.synthesis->render(plan,session.notes,session.input,tempo,origin,session.rate,session.cancelled);
        rendered.feedback["phonemes"]=phones;rendered.feedback["pronunciations"]=plan.feedback;
        session.feedback=rendered.feedback.dump();session.audio=std::move(rendered.stereo);session.error.clear();
        *out={sizeof(*out),session.rate,2,session.audio.size()/2,tempo.secondsAt(origin)+rendered.start,session.audio.data(),session.feedback.c_str(),nullptr,nullptr};return SVS_OK;
    }catch(const std::exception& error) {session.error=Json{{"stage",activeStage},{"voiceId",session.voice->id},{"path",session.voice->root.u8string()},{"message",error.what()}}.dump();*out={sizeof(*out),session.rate,2,0,0,nullptr,nullptr,session.error.c_str(),nullptr};return session.cancelled?SVS_CANCELLED:SVS_FAILED;}
}
void SVS_CALL cancel(svs_session handle) {if(handle) {static_cast<Session*>(handle)->cancelled=true;}}
void SVS_CALL releaseResult(svs_session handle, svs_result* result) {if(handle) {auto& session=*static_cast<Session*>(handle);std::lock_guard lock(session.mutex);session.audio.clear();session.feedback.clear();}if(result&&result->size>=sizeof(*result)) {*result={};}}
}
SVS_EXPORT svs_status SVS_CALL svs_get_api(uint32_t major, uint32_t minor, uint32_t size, svs_api* out)
{
    if (major != SVS_ABI_MAJOR || !out || size < SVS_API_REQUIRED_SIZE) { return SVS_BAD_ABI; }
    svs_api api{};
    api.major = SVS_ABI_MAJOR;
    api.minor = std::min(minor, uint32_t(SVS_ABI_MINOR));
    api.size = std::min(size, uint32_t(sizeof(api)));
    api.create_engine = create; api.destroy_engine = destroy;
    api.catalog = catalog; api.capabilities = capabilities; api.release_string = releaseString;
    api.create_session = createSession; api.destroy_session = destroySession;
    api.submit = submit; api.render = render; api.cancel = cancel; api.release_result = releaseResult;
    if (SVS_HAS_FIELD(api, svs_api, query_engine_settings) && minor >= 2) {
        api.features |= SVS_FEATURE_ENGINE_SETTINGS;
        api.query_engine_settings = settings;
    }
    if(SVS_HAS_FIELD(api,svs_api,query_catalog)&&minor>=3) {api.features|=SVS_FEATURE_CATALOG_QUERY;api.query_catalog=queryCatalog;}
    if(SVS_HAS_FIELD(api,svs_api,close_resource)&&minor>=1) {api.features|=SVS_FEATURE_RESOURCES;api.open_resource=openResource;api.read_resource=readResource;api.close_resource=closeResource;}
    if(SVS_HAS_FIELD(api,svs_api,pronunciation)&&minor>=1) {api.features|=SVS_FEATURE_PRONUNCIATION;api.pronunciation=pronunciation;}
    std::memcpy(out, &api, api.size);
    return SVS_OK;
}
