#include "Duration.h"
#include "Speaker.h"
#include "svs.hpp"
#include <fstream>
#include <chrono>
#include <iostream>
#include <stdexcept>
using namespace diffsinger;
#ifdef _WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#endif
namespace {
void require(bool condition,const std::string& message) {if(!condition) {throw std::runtime_error(message);}}
template<class F> void rejects(F action,const std::string& message) {try {action();}catch(const std::exception& error) {std::cout<<"Expected rejection: "<<error.what()<<'\n';return;}throw std::runtime_error(message);}
NoteInput note(std::string id,std::string lyric,double tick,double duration=48) {
    NoteInput result;result.id=std::move(id);result.lyric=std::move(lyric);result.language="zh";result.tick=tick;result.durationTick=duration;result.start=tick/96.;result.duration=duration/96.;return result;
}
void speakerFixture() {
    const auto root=packageDirectory()/fs::u8path("a2-speaker-fixture-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    require(!fs::exists(root),"Fixture directory collision");fs::create_directory(root);
    struct Cleanup {fs::path root;~Cleanup(){std::error_code error;fs::remove_all(root,error);}} cleanup{root};
    auto write=[&](const char* name,const std::vector<float>& values){std::ofstream output(root/name,std::ios::binary);output.write(reinterpret_cast<const char*>(values.data()),std::streamsize(values.size()*4));};
    write("one.emb",{1,3});write("two.emb",{5,7});VoicePackage voice;voice.root=fs::canonical(root);
    StageConfig stage;stage.source=root/"dsconfig.yaml";stage.values={{"speakers",Json::array({"one","two"})},{"hidden_size",2}};voice.stages["acoustic"]=stage;
    require(speakerChoices(voice).size()==2,"Stable speaker choices missing");require(speakerEmbedding(voice,stage,{{"diffsinger.speaker","two"}})==std::vector<float>({5,7}),"Speaker selection ignored");
    require(speakerEmbedding(voice,stage,{{"diffsinger.speakerWeights",{{"one",1},{"two",3}}}})==std::vector<float>({4,6}),"Speaker weights were not normalized");
    rejects([&]{speakerEmbedding(voice,stage,{{"diffsinger.speaker","missing"}});},"Unknown speaker accepted");
    stage.values["hidden_size"]=3;rejects([&]{speakerEmbedding(voice,stage,Json::object());},"Speaker hidden_size ignored");
    std::cout<<"PASS explicit multi-speaker embedding fixture (not a six-bank multi-speaker claim)\n";
}
void publicApi(const fs::path& plugin,const fs::path& root) {
#ifdef _WIN32
    const auto module=LoadLibraryExW(plugin.c_str(),nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
    if(!module) {throw std::runtime_error("Cannot load deployed native plugin");}
    const auto entry=reinterpret_cast<svs_get_api_fn>(GetProcAddress(module,"svs_get_api"));
    struct Unload {HMODULE module;~Unload(){FreeLibrary(module);}} unload{module};
#else
    auto* module=dlopen(plugin.c_str(),RTLD_NOW|RTLD_LOCAL);if(!module) {throw std::runtime_error(dlerror());}
    const auto entry=reinterpret_cast<svs_get_api_fn>(dlsym(module,"svs_get_api"));struct Unload {void* module;~Unload(){dlclose(module);}} unload{module};
#endif
    svs_sdk::Engine engine(entry);const auto context=Json{{"engineSettings",{{"diffsinger.voicebankDirectories",Json::array({root.u8string()})}}}}.dump();
    const auto catalog=Json::parse(engine.catalog(context.c_str()).c_str());require(catalog["voices"].size()==6,"ABI catalog lost voices");
    for(const auto& voice:catalog["voices"]) {const auto id=voice.at("id").get<std::string>();const auto reading=Json::parse(engine.pronunciation(id.c_str(),R"({"lyric":"你","language":"zh"})").c_str());require(reading.value("generated",false),"ABI pronunciation failed");const auto capabilities=Json::parse(engine.capabilities(id.c_str()).c_str());require(capabilities["languages"].size()==4,"ABI languages narrowed");}
    const auto id=catalog["voices"][0]["id"].get<std::string>();auto session=engine.session(id.c_str());
    std::string lyric="你",noteId="abi-kept-note",document=Json{{"secondsPerTick",1./96.},{"position",192},{"contentOffset",48},{"pronunciations",{{noteId,{{"generated",true},{"source","projectDictionary"},{"phonemes",Json::array({"zh/a"})}}}}}}.dump();
    svs_note source{sizeof(svs_note),noteId.c_str(),0,48,0,.5,60,lyric.c_str(),"zh","","{}","{}"};
    svs_snapshot snapshot{sizeof(svs_snapshot),"clip",1,2,3,id.c_str(),48000,&source,1,.5,document.c_str()};require(session.submit(snapshot)==SVS_OK,"ABI submit failed");
    lyric="☃";noteId="mutated-note";document="{}";
    {auto result=session.render();require(result.status()==SVS_UNSUPPORTED,"A2 unexpectedly claims audio synthesis");require(result.value().audio==nullptr&&result.value().frame_count==0,"A2 fabricated PCM");const auto feedback=Json::parse(result.value().feedback_json);require(feedback["phonemes"][0]["noteId"]=="abi-kept-note"&&feedback["phonemes"][0]["symbol"]=="zh/a","Submit did not copy input / project dictionary was overridden");}
    session.cancel();{auto result=session.render();require(result.status()==SVS_CANCELLED,"ABI cancellation ignored");}
    std::cout<<"PASS deployed plugin pronunciation/session snapshot copy/dictionary priority/cancellation/result ownership\n";
}
}
int main(int argc,char** argv) {
    try {
        if(argc<2||argc>3) {throw std::runtime_error("Usage: DiffSingerDurationTest <external six-package root> [deployed native plugin]");}
        initializeRuntime();Ort::Env environment{ORT_LOGGING_LEVEL_WARNING,"DiffSingerDurationTest"};
        speakerFixture();
        const auto catalog=scan({{"engineSettings",{{"diffsinger.voicebankDirectories",Json::array({fs::absolute(fs::u8path(argv[1])).u8string()})}}}},1);
        require(catalog->voices.size()==6,"Expected six external voices");
        svs_sdk::TempoMap tempo;require(tempo.setPoints({{0,1./96.}}),"Tempo fixture");std::atomic<bool> cancelled{false};
        size_t voiceIndex=0;for(const auto& voice:catalog->voices) {
            Pronunciation pronunciation(voice);
            const auto ni=pronunciation.resolve({{"lyric","你"},{"language","zh"}}),hao=pronunciation.resolve({{"lyric","好"},{"language","zh"}});
            require(ni.value("generated",false)&&hao.value("generated",false),"Automatic Chinese lyric failed: "+ni.dump()+" / "+hao.dump());
            require(!pronunciation.resolve({{"lyric","☃"},{"language","zh"}}).value("generated",true),"OOV silently accepted");
            require(pronunciation.resolve({{"lyric","你好"},{"pronunciation","ni3 hao3"},{"language","zh"}}).value("generated",false),"Explicit reading failed");
            if(voiceIndex++==0) {const auto english=pronunciation.resolve({{"lyric","hello"},{"language","en"}});require(english.value("generated",false),"Large bank English dictionary failed: "+english.dump());std::cout<<"PASS actual 13.5MB English dictionary: "<<english.dump()<<'\n';}
            const auto phrase=pronunciation.resolve({{"lyric","重庆"},{"language","zh"}});require(phrase.value("generated",false)&&phrase["phonemes"][0]=="zh/ch","Phrase-level polyphonic reading ignored");
            require(pronunciation.resolve({{"lyric","绿"},{"language","zh"}}).value("generated",false),"Umlaut pinyin normalization failed");
            Duration duration(environment,voice);const auto original=std::vector<NoteInput>{note("n1","你",0),note("n2","好",48)};
            const auto baseline=duration.predict(original,tempo,0,Json::object(),cancelled);
            const auto syllables=duration.predict({note("phrase","你好",0),note("next","+",48)},tempo,0,Json::object(),cancelled);require(syllables.phones.size()==4&&syllables.phones[1].symbol=="zh/i"&&syllables.phones[2].noteId=="next"&&syllables.phones[2].symbol=="zh/h","Syllable extension grouping failed");
            require(!baseline.predictions.empty()&&!baseline.phones.empty(),"Real duration model produced no output");
            double previous=-INFINITY;for(const auto& phone:baseline.phones) {require(std::isfinite(phone.start)&&std::isfinite(phone.end)&&phone.end-phone.start>=.005-1e-8&&phone.start>=previous-1e-8,"Invalid automatic phoneme time");previous=phone.end;}
            auto continuation=original;continuation.push_back(note("n3","-",96));const auto extended=duration.predict(continuation,tempo,0,Json::object(),cancelled);require(extended.phones.back().end==1.5,"Continuation did not extend preceding vowel");
            auto gap=continuation;gap.back().start+=.1;rejects([&]{duration.predict(gap,tempo,0,Json::object(),cancelled);},"Nonadjacent continuation accepted");
            auto rests=original;rests.push_back(note("rest","",120));const auto silent=duration.predict(rests,tempo,0,Json::object(),cancelled);require(silent.phones.back().symbol=="SP","Rest generated singing phones");
            auto manual=original;manual[0].phonemes["segments"]=Json::array({{{"symbol","zh/n"},{"startTick",-9.6},{"durationTicks",9.6}},{{"symbol","zh/i"},{"startTick",0},{"durationTicks",38.4}}});
            const auto pinned=duration.predict(manual,tempo,0,Json::object(),cancelled);require(pinned.phones[0].manual&&std::abs(pinned.phones[0].start+.1)<1e-8&&std::abs(pinned.phones[1].end-.4)<1e-8,"Pinned phonemes were ignored");
            previous=-INFINITY;for(const auto& phone:pinned.phones) {require(phone.start>=previous-1e-8,"Pinned/automatic adjacent phonemes overlap");previous=phone.end;}
            manual[0].phonemes.erase("segments");const auto reset=duration.predict(manual,tempo,0,Json::object(),cancelled);require(reset.phones.size()==baseline.phones.size()&&!reset.phones[0].manual,"Reset did not restore automatic layout");
            require(!original[0].phonemes.contains("segments"),"Automatic result mutated user segments");
            manual[0].phonemes["segments"]=Json::array();const auto empty=duration.predict(manual,tempo,0,Json::object(),cancelled);require(empty.phones[0].symbol=="SP"&&empty.phones[0].manual,"Explicit zero-phoneme override lost");
            auto shortNote=original;shortNote[0].duration=.001;rejects([&]{duration.predict(shortNote,tempo,0,Json::object(),cancelled);},"Minimum duration ignored");
            cancelled=true;rejects([&]{duration.predict(original,tempo,0,Json::object(),cancelled);},"Cancelled duration accepted");cancelled=false;
            std::cout<<"PASS "<<voice->metadata.at("name").get<std::string>()<<" automatic="<<ni["phonemes"].dump()<<" duration="<<Json(baseline.predictions).dump()<<" phones="<<baseline.feedback.dump()<<'\n';
        }
        if(argc==3) {publicApi(fs::absolute(fs::u8path(argv[2])),fs::absolute(fs::u8path(argv[1])));}
        std::cout<<"PASS six actual duration linguistic/predictor pairs; no PCM fabricated\n";return 0;
    }catch(const std::exception& error) {std::cerr<<error.what()<<'\n';return 1;}
}
