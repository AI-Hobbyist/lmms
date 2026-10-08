#include "Synthesis.h"
#include "Hash.h"
#include <iostream>
#include <fstream>
#include <chrono>
using namespace diffsinger;
namespace {
void require(bool value,const std::string& message) {if(!value) {throw std::runtime_error(message);}}
std::string digest(const std::vector<float>& audio) {Sha256 hash;hash.add(audio.data(),audio.size()*sizeof(float));return hash.finish();}
void audition(const fs::path& path,const std::vector<float>& audio) {
    std::ostringstream out(std::ios::out|std::ios::binary);auto u16=[&](uint16_t value){out.put(char(value));out.put(char(value>>8));};auto u32=[&](uint32_t value){u16(uint16_t(value));u16(uint16_t(value>>16));};
    out.write("RIFF",4);u32(uint32_t(36+audio.size()*2));out.write("WAVEfmt ",8);u32(16);u16(1);u16(2);u32(48000);u32(48000*4);u16(4);u16(16);out.write("data",4);u32(uint32_t(audio.size()*2));for(float value:audio) {u16(uint16_t(int16_t(std::lround(std::clamp(value,-1.f,1.f)*32767))));}const auto bytes=out.str(),sha=hashText(bytes);const auto destination=path.parent_path()/(sha+".wav");if(!fs::exists(destination)) {std::ofstream file(destination,std::ios::binary);file.write(bytes.data(),std::streamsize(bytes.size()));if(!file) {throw std::runtime_error("Cannot save audition WAV");}}require(hashFile(destination)==sha,"Audition filename does not match file SHA-256");std::cout<<"AUDITION "<<path.stem().u8string()<<" "<<destination.u8string()<<std::endl;
}
void check(const SynthesisResult& result) {require(result.stereo.size()>48000,"PCM too short");double energy=0;for(float value:result.stereo) {require(std::isfinite(value),"Non-finite PCM");energy+=double(value)*value;}require(energy/result.stereo.size()>1e-10,"Silent PCM");const auto extrema=std::minmax_element(result.stereo.begin(),result.stereo.end());require(*extrema.second-*extrema.first>1e-6,"Static PCM");require(result.feedback.at("pitch").size()>10,"Missing pitch feedback");require(result.feedback.at("curves").size()==3,"Missing variance feedback");}
void tensorCacheFixture() {
    const auto fixture=packageDirectory()/fs::u8path("a3-tensor-fixture-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));require(!fs::exists(fixture),"Fixture collision");fs::create_directory(fixture);const auto owned=fs::canonical(fixture);require(owned.parent_path()==fs::canonical(packageDirectory()),"Fixture escaped test directory");
    struct Cleanup {fs::path path;~Cleanup(){std::error_code error;fs::remove_all(path,error);}} cleanup{owned};const auto root=owned/"cache"/"SVS"/"DiffSinger";TensorCache cache(root,500);
    const auto values=Tensor::make<float>(ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT,{1,2},{.25f,.5f});const Tensors inputs{{"in",values}},outputs{{"out",values}};const auto key=TensorCache::key("fixture-v1",inputs);require(key!=TensorCache::key("fixture-v2",inputs),"Tensor identity ignored");auto changed=inputs;changed.at("in").dimensions={2,1};require(key!=TensorCache::key("fixture-v1",changed),"Tensor shape ignored");changed.at("in").bytes[0]^=1;require(key!=TensorCache::key("fixture-v1",changed),"Tensor bytes ignored");
    cache.save(key,outputs);Tensors read;require(cache.load(key,read)&&read.at("out").bytes==values.bytes,"Tensor roundtrip failed");const auto file=root/(key+".tensor");{std::fstream corrupt(file,std::ios::in|std::ios::out|std::ios::binary);corrupt.put('X');}require(!cache.load(key,read)&&!fs::exists(file),"Corrupt tensor did not become a miss");
    std::ofstream(root/"audition.wav")<<"preserve";for(int i=0;i<8;++i) {cache.save(TensorCache::key("fixture-"+std::to_string(i),inputs),outputs);}uint64_t bytes=0;for(const auto& entry:fs::directory_iterator(root)) {if(entry.path().extension()==".tensor") {bytes+=entry.file_size();}}require(bytes<=500&&fs::exists(root/"audition.wav"),"Tensor LRU budget crossed its boundary");std::cout<<"PASS tensor codec / identity / corruption / bounded LRU / audition preservation"<<std::endl;
}
}
int run(int argc,char** argv) {
    try {
        if(argc==5&&std::string(argv[1])=="--voice") {
            initializeRuntime();Ort::Env env{ORT_LOGGING_LEVEL_WARNING,"DiffSingerExternalVoiceTest"};
            const auto catalog=scan({{"engineSettings",{{"diffsinger.voicebankDirectories",Json::array({fs::absolute(fs::u8path(argv[2])).u8string()})},{"diffsinger.vocoderDirectories",Json::array({fs::absolute(fs::u8path(argv[4])).u8string()})}}}},1);
            require(catalog->voices.size()==1,"Expected one external voice: "+catalog->diagnostics.dump());const auto voice=catalog->voices.front();
            svs_sdk::TempoMap tempo;require(tempo.setPoints({{0,1./96.}}),"Invalid test tempo");std::atomic<bool> cancel{false};NoteInput note;note.id="external-la";note.lyric="la";note.reading="la";note.language="zh";note.durationTick=96;note.duration=1;note.pitch=60;
            const std::vector<NoteInput> notes{note};Duration duration(env,voice);Synthesis synthesis(env,voice);const auto plan=duration.predict(notes,tempo,0,Json::object(),cancel);
            const Json input{{"cacheDirectory",fs::absolute(fs::u8path(argv[3])).u8string()},{"engineSettings",{{"diffsinger.renderSteps",5}}}};
            const auto result=synthesis.render(plan,notes,input,tempo,0,48000,cancel);require(result.stereo.size()>48000,"External PCM too short");double energy=0;for(const auto value:result.stereo) {require(std::isfinite(value),"Non-finite external PCM");energy+=double(value)*value;}require(energy/result.stereo.size()>1e-10,"Silent external PCM");require(result.feedback.at("pitch").size()>10,"Missing external pitch feedback");
            const auto repeated=synthesis.render(plan,notes,input,tempo,0,48000,cancel);require(digest(result.stereo)==digest(repeated.stereo),"External cached PCM changed");
            auto uncached=input;uncached.erase("cacheDirectory");require(digest(result.stereo)==digest(synthesis.render(plan,notes,uncached,tempo,0,48000,cancel).stereo),"External uncached seeded PCM changed");
            Json schema{{"parameters",Json::array()},{"feedbackParameters",Json::array()}};Synthesis::declareParameters(*voice,schema);require(result.feedback.at("curves").size()==schema.at("feedbackParameters").size(),"External variance feedback differs from declared voice capability");
            audition(fs::absolute(fs::u8path(argv[3]))/"external-la-CPU.wav",result.stereo);std::cout<<"PASS external voice "<<voice->metadata.at("name").get<std::string>()<<" frames="<<result.stereo.size()/2<<" curves="<<result.feedback.at("curves").size()<<std::endl;return 0;
        }
        if(argc!=3) {throw std::runtime_error("Usage: DiffSingerSynthesisTest <six-package root> <cache/SVS/DiffSinger>");}
        initializeRuntime();tensorCacheFixture();Ort::Env env{ORT_LOGGING_LEVEL_WARNING,"DiffSingerSynthesisTest"};const auto catalog=scan({{"engineSettings",{{"diffsinger.voicebankDirectories",Json::array({fs::absolute(fs::u8path(argv[1])).u8string()})}}}},1);require(catalog->voices.size()==6,"Six voices missing");
        svs_sdk::TempoMap tempo;require(tempo.setPoints({{0,1./96.}}),"Invalid test tempo");std::atomic<bool> cancel{false};
        Json input{{"secondsPerTick",1./96.},{"cacheDirectory",fs::absolute(fs::u8path(argv[2])).u8string()},{"engineSettings",{{"diffsinger.renderSteps",5}}}};
        for(const auto& voice:catalog->voices) {NoteInput a;a.id="n1";a.lyric="你";a.language="zh";a.durationTick=48;a.duration=.5;a.pitch=60;NoteInput b=a;b.id="n2";b.lyric="好";b.tick=48;b.start=.5;b.pitch=62;const std::vector<NoteInput> notes{a,b};Duration duration(env,voice);Synthesis synthesis(env,voice);const auto plan=duration.predict(notes,tempo,0,Json::object(),cancel);
            auto result=synthesis.render(plan,notes,input,tempo,0,48000,cancel);check(result);const auto hash=digest(result.stereo);std::cout<<"PASS "<<voice->metadata.at("name").get<std::string>()<<" frames="<<result.stereo.size()/2<<" sha256="<<hash<<std::endl;
            const auto cached=synthesis.render(plan,notes,input,tempo,0,48000,cancel);require(hash==digest(cached.stereo),"Cached PCM changed");
            audition(fs::absolute(fs::u8path(argv[2]))/(voice->root.filename().u8string()+"-A3-nihao-CPU.wav"),result.stereo);
            auto drawn=input;drawn["curves"]["svs.pitch"]={{"points",Json::array({{{"tick",-96},{"value",72}},{{"tick",192},{"value",72}}})}};const auto changed=synthesis.render(plan,notes,drawn,tempo,0,48000,cancel);check(changed);require(hash!=digest(changed.stereo),"Pitch edit did not change audio");
            std::cout<<"PASS cached replay and actual pitch-conditioned PCM change"<<std::endl;
            auto noCache=input;noCache.erase("cacheDirectory");const auto recomputed=synthesis.render(plan,notes,noCache,tempo,0,48000,cancel);require(hash==digest(recomputed.stereo),"Seeded uncached recomputation changed PCM");
            auto control=input;control["clipParameters"]={{"diffsinger.velocity",1.3},{"diffsinger.breathiness.offset",6}};const auto controlled=synthesis.render(plan,notes,control,tempo,0,48000,cancel);check(controlled);require(hash!=digest(controlled.stereo),"Voice controls did not change PCM");
            auto handNotes=notes;handNotes[0].phonemes["symbols"]=Json::array({"zh/a"});const auto handPlan=duration.predict(handNotes,tempo,0,Json::object(),cancel);const auto hand=synthesis.render(handPlan,handNotes,input,tempo,0,48000,cancel);check(hand);require(hash!=digest(hand.stereo),"Manual phonemes did not change PCM");
            cancel=true;try {synthesis.render(plan,notes,input,tempo,0,48000,cancel);throw std::runtime_error("Cancellation ignored");}catch(const std::exception& error) {require(std::string(error.what()).find("Cancelled")!=std::string::npos,"Unexpected cancellation error");}cancel=false;
            std::cout<<"PASS stable seed without cache / model controls / manual phonemes / cancellation"<<std::endl;
            if(voice==catalog->voices.front()) {
                auto seeded=input;seeded["seed"]=2;const auto another=synthesis.render(plan,notes,seeded,tempo,0,48000,cancel);require(hash!=digest(another.stereo),"Seed change did not change PCM");require(hash==digest(synthesis.render(plan,notes,input,tempo,0,48000,cancel).stereo),"Returning to default seed changed PCM");
                auto longNotes=notes;for(auto note:notes) {note.id+="-later";note.tick+=4800;note.start+=50;longNotes.push_back(note);}auto longPlan=plan;for(auto phone:plan.phones) {phone.noteId+="-later";phone.start+=50;phone.end+=50;longPlan.phones.push_back(phone);}const auto split=synthesis.render(longPlan,longNotes,input,tempo,0,48000,cancel);check(split);require(split.stereo.size()>48000*50*2,"Chunk placement lost silence");const size_t later=size_t(std::llround(50.*48000))*2;require(std::equal(result.stereo.begin(),result.stereo.end(),split.stereo.begin())&&std::equal(result.stereo.begin(),result.stereo.end(),split.stereo.begin()+later),"Chunk placement changed PCM");require(split.feedback.at("curves").at("diffsinger.tension").at("gaps").size()==1,"Chunk curve gap missing");
                auto restPlan=longPlan;restPlan.phones.insert(restPlan.phones.begin()+plan.phones.size(),{"SP","zh","rest",plan.phones.back().end,50,60,false});auto restNotes=longNotes;auto rest=notes.front();rest.id="rest";rest.lyric="";rest.tick=96;rest.start=1;rest.durationTick=4704;rest.duration=49;restNotes.push_back(rest);require(digest(split.stereo)==digest(synthesis.render(restPlan,restNotes,input,tempo,0,48000,cancel).stereo),"Explicit long rest changed chunk placement");
                svs_sdk::TempoMap varied;require(varied.setPoints({{0,1./96.},{216,1./48.}}),"Invalid varied tempo");const double origin=192;auto mappedNotes=notes;for(auto& note:mappedNotes) {note.start=varied.secondsAt(origin+note.tick)-varied.secondsAt(origin);note.duration=varied.secondsAt(origin+note.tick+note.durationTick)-varied.secondsAt(origin+note.tick);}const auto mappedPlan=duration.predict(mappedNotes,varied,origin,Json::object(),cancel);const auto mapped=synthesis.render(mappedPlan,mappedNotes,input,varied,origin,48000,cancel);check(mapped);require(mapped.stereo.size()>result.stereo.size(),"Tempo change did not change duration");const auto& points=mapped.feedback.at("curves").at("diffsinger.tension").at("points");const auto expected=varied.tickAt(varied.secondsAt(origin)+mapped.start+(points.size()-1)*512./44100.)-origin;require(std::abs(points.back().at("tick").get<double>()-expected)<1e-8,"Feedback did not use frozen tempo and content-local ticks");
                auto tooLong=plan;tooLong.phones.back().end=500;try {synthesis.render(tooLong,notes,input,tempo,0,48000,cancel);throw std::runtime_error("PCM bound ignored");}catch(const std::exception& error) {require(std::string(error.what()).find("before inference")!=std::string::npos,"Unexpected preflight error");}
                std::cout<<"PASS seed identity / natural-rest chunk placement / tempo with nonzero content origin / PCM preflight bound"<<std::endl;
            }
        }return 0;
    }catch(const std::exception& error) {std::cerr<<"FAIL "<<error.what()<<std::endl;return 1;}
}
#ifdef _WIN32
int wmain(int argc,wchar_t** argv) {
    std::vector<std::string> utf8;std::vector<char*> arguments;for(int i=0;i<argc;++i) {utf8.push_back(fs::path(argv[i]).u8string());}for(auto& value:utf8) {arguments.push_back(value.data());}return run(argc,arguments.data());
}
#else
int main(int argc,char** argv) {return run(argc,argv);}
#endif
