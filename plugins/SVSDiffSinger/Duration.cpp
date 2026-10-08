/* Word grouping follows DiffSingerForTuneLab; Copyright (c) 2026 Jingang, MIT. */
#include "Duration.h"
#include "Speaker.h"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <set>
namespace diffsinger {
namespace {
Tensor ints(const std::vector<int64_t>& values) {return Tensor::make(ONNX_TENSOR_ELEMENT_DATA_TYPE_INT64,{1,int64_t(values.size())},values);}
}
Duration::Duration(Ort::Env& environment,std::shared_ptr<const VoicePackage> voice):m_voice(std::move(voice)),m_pronunciation(m_voice) {
    const auto found=m_voice->stages.find("duration");if(found!=m_voice->stages.end()&&found->second.values.value("predict_dur",true)) {
        const auto& stage=found->second;m_linguistic=std::make_unique<CpuModel>(environment,stage.models.at("linguistic"),"duration.linguistic");m_duration=std::make_unique<CpuModel>(environment,stage.models.at("dur"),"duration");
    }
}
DurationPlan Duration::predict(const std::vector<NoteInput>& input,const svs_sdk::TempoMap& tempo,double origin,const Json& parameters,const std::atomic<bool>& cancelled) {
    if(input.size()>4096) {throw std::runtime_error("Duration note count exceeds bound");}
    DurationPlan result;if(input.empty()) {return result;}
    auto notes=input;std::stable_sort(notes.begin(),notes.end(),[](const auto& a,const auto& b){return a.start<b.start;});
    struct Resolved {NoteInput note;std::vector<std::string> symbols;size_t leading=0,flat=0;double end=0;};
    std::vector<Resolved> resolved;std::set<std::string> ids;
    for(auto note:notes) {
        if(note.id.empty()||!ids.insert(note.id).second||!std::isfinite(note.start)||std::abs(note.start)>600||!std::isfinite(note.duration)||note.duration<=0||note.duration>600||!std::isfinite(note.pitch)||note.pitch<0||note.pitch>127) {throw std::runtime_error("Invalid duration note: "+note.id);}
        if(note.language.empty()) {note.language=m_voice->declaration().value("defaultLanguage",std::string("zh"));}
        if(note.lyric=="-"&&note.reading.empty()&&!note.phonemes.contains("segments")) {
            if(resolved.empty()||resolved.back().symbols.empty()||resolved.back().note.id.empty()||resolved.back().note.lyric.empty()||(resolved.back().note.phonemes.contains("segments")&&resolved.back().note.phonemes["segments"].empty())||std::abs(resolved.back().end-note.start)>1e-6) {throw std::runtime_error("Non-adjacent or silent continuation note: "+note.id);}
            resolved.back().end=note.start+note.duration;result.feedback.push_back({{"noteId",note.id},{"continuation",true},{"phonemes",Json::array()}});continue;
        }
        std::vector<std::string> symbols;
        if(note.lyric=="+"&&note.reading.empty()&&!note.phonemes.contains("segments")&&!note.phonemes.contains("symbols")) {
            if(resolved.empty()||resolved.back().note.phonemes.contains("segments")||std::abs(resolved.back().end-note.start)>1e-6) {throw std::runtime_error("Syllable extension has no adjacent automatic source: "+note.id);}
            auto& previous=resolved.back();size_t next=previous.leading+1;while(next<previous.symbols.size()&&m_pronunciation.type(previous.symbols[next],previous.note.language)=="consonant") {++next;}
            if(next>=previous.symbols.size()) {throw std::runtime_error("No remaining syllable for extension: "+note.id);}
            const auto boundary=previous.leading+1;symbols.assign(previous.symbols.begin()+boundary,previous.symbols.end());previous.symbols.resize(boundary);note.language=previous.note.language;
        }
        else if(note.phonemes.contains("segments")) {if(!note.phonemes["segments"].is_array()) {throw std::runtime_error("Manual segments must be an array: "+note.id);}for(const auto& segment:note.phonemes["segments"]) {symbols.push_back(segment.at("symbol").get<std::string>());}symbols=m_pronunciation.map(symbols,note.language,"acoustic");}
        else if(note.phonemes.contains("symbols")) {symbols=m_pronunciation.map(note.phonemes.at("symbols").get<std::vector<std::string>>(),note.language,"acoustic");}
        else {
            auto answer=note.pronunciation;
            if(!answer.value("generated",false)) {answer=m_pronunciation.resolve({{"lyric",note.lyric},{"pronunciation",note.reading},{"language",note.language}});}
            if(!answer.value("generated",false)) {throw std::runtime_error("Pronunciation / note "+note.id+": "+answer.value("diagnostic",std::string("Unknown lyric")));}
            symbols=m_pronunciation.map(answer.value("phonemes",std::vector<std::string>{}),note.language,"acoustic");
        }
        size_t leading=0;while(leading<symbols.size()&&m_pronunciation.type(symbols[leading],note.language)=="consonant") {++leading;}
        if(!symbols.empty()&&leading==symbols.size()) {throw std::runtime_error("Pronunciation has no vowel/glide: "+note.id);}
        if(!resolved.empty()&&resolved.back().end>note.start+1e-6) {throw std::runtime_error("Overlapping sounding notes: "+note.id);}
        if(!resolved.empty()&&resolved.back().end<note.start-1e-6) {NoteInput rest;rest.id="";rest.start=resolved.back().end;rest.duration=note.start-rest.start;rest.language=note.language;resolved.push_back({rest,{"SP"},0,0,note.start});}
        if(symbols.empty()) {symbols={"SP"};leading=0;}
        resolved.push_back({note,std::move(symbols),leading,0,note.start+note.duration});
    }
    if(resolved.empty()) {return result;}
    const auto& acoustic=m_voice->stages.at("acoustic").values;
    const auto stageIt=m_voice->stages.find("duration");const auto& stage=stageIt==m_voice->stages.end()?m_voice->stages.at("acoustic"):stageIt->second;
    const double frame=double(stage.values.value("hop_size",acoustic.at("hop_size").get<int64_t>()))/stage.values.value("sample_rate",acoustic.at("sample_rate").get<int64_t>());
    if(!(frame>0&&frame<=1)) {throw std::runtime_error("Invalid duration frame period");}
    std::vector<int64_t> tokens,divisions{1},wordDurations,pitches{int64_t(std::llround(resolved.front().note.pitch))},languages;
    tokens.push_back(stage.phonemes.at("SP").get<int64_t>());languages.push_back(0);
    const auto frameAt=[&](double seconds){return int64_t(std::floor(seconds/frame));};
    const double paddedStart=resolved.front().note.start-.5;
    wordDurations.push_back(std::max(int64_t(1),frameAt(resolved.front().note.start)-frameAt(paddedStart)));
    for(auto& item:resolved) {
        item.flat=tokens.size();const auto symbols=m_pronunciation.map(item.symbols,item.note.language,stageIt==m_voice->stages.end()?"acoustic":"duration");
        divisions.back()+=int64_t(item.leading);divisions.push_back(int64_t(symbols.size()-item.leading));wordDurations.push_back(std::max(int64_t(1),frameAt(item.end)-frameAt(item.note.start)));
        for(const auto& symbol:symbols) {tokens.push_back(stage.phonemes.at(symbol).get<int64_t>());pitches.push_back(int64_t(std::llround(item.note.pitch)));languages.push_back(symbol=="SP"||symbol=="AP"?0:stage.languages.at(item.note.language).get<int64_t>());}
    }
    tokens.push_back(stage.phonemes.at("SP").get<int64_t>());pitches.push_back(pitches.back());languages.push_back(0);divisions.push_back(1);wordDurations.push_back(std::max(int64_t(1),frameAt(resolved.back().end+.5)-frameAt(resolved.back().end)));
    if(tokens.size()>32768) {throw std::runtime_error("Duration phoneme count exceeds supported bound");}
    if(m_linguistic) {
        Tensors inputs{{"tokens",ints(tokens)},{"word_div",ints(divisions)},{"word_dur",ints(wordDurations)}};
        if(m_linguistic->accepts("languages")) {inputs["languages"]=ints(languages);}
        if(m_linguistic->accepts("tokens_b")) {inputs["tokens_b"]=ints(tokens);inputs["blend"]=Tensor::make(ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT,{1,int64_t(tokens.size())},std::vector<float>(tokens.size(),0));}
        auto encoded=m_linguistic->run(inputs,cancelled);Tensors dur{{"encoder_out",std::move(encoded.at("encoder_out"))},{"x_masks",std::move(encoded.at("x_masks"))},{"ph_midi",ints(pitches)}};
        if(m_duration->accepts("spk_embed")) {const auto embedding=speakerEmbedding(*m_voice,stage,parameters);std::vector<float> repeated;repeated.reserve(tokens.size()*embedding.size());for(size_t i=0;i<tokens.size();++i) {repeated.insert(repeated.end(),embedding.begin(),embedding.end());}dur["spk_embed"]=Tensor::make(ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT,{1,int64_t(tokens.size()),int64_t(embedding.size())},repeated);}
        result.predictions=m_duration->run(dur,cancelled).at("ph_dur_pred").values<float>();if(result.predictions.size()!=tokens.size()) {throw std::runtime_error("Duration output length does not match phonemes");}
    }else {result.predictions.assign(tokens.size(),1.f);}
    const double minimum=.005,maximumLead=.15;
    const auto manualBoundary=[&](const Resolved& item,bool end) {
        const auto& segments=item.note.phonemes.at("segments");if(segments.empty()) {return end?item.end:item.note.start;}
        const auto& segment=end?segments.back():segments.front();const double tick=item.note.tick+segment.at("startTick").get<double>()+(end?segment.at("durationTicks").get<double>():0.);
        return tempo.secondsAt(origin+tick)-tempo.secondsAt(origin);
    };
    std::vector<double> starts(resolved.size()),heads(resolved.size());
    for(size_t n=0;n<resolved.size();++n) {const auto& item=resolved[n];double leading=0;for(size_t i=0;i<item.leading;++i) {leading+=std::max(minimum,double(result.predictions[item.flat+i])*frame);}
        const bool manual=item.note.phonemes.contains("segments");starts[n]=manual?manualBoundary(item,false):item.note.start-std::min(maximumLead,leading);
        if(n) {const auto& previous=resolved[n-1];const double boundary=previous.note.phonemes.contains("segments")?manualBoundary(previous,true):previous.note.start;
            if(manual&&starts[n]<boundary-1e-8) {throw std::runtime_error("Pinned phonemes overlap adjacent note: "+item.note.id);}if(!manual) {starts[n]=std::max(boundary,starts[n]);}}
        heads[n]=manual?item.note.start:std::max(item.note.start,starts[n]+item.leading*minimum);
    }
    for(size_t n=0;n<resolved.size();++n) {
        const auto& item=resolved[n];const double finish=n+1<resolved.size()?std::min(item.end,starts[n+1]):item.end;
        std::vector<Phone> local;
        if(item.note.phonemes.contains("segments")) {
            double previous=-INFINITY;size_t i=0;
            for(const auto& segment:item.note.phonemes.at("segments")) {
                const auto startTick=item.note.tick+segment.at("startTick").get<double>();const auto endTick=startTick+segment.at("durationTicks").get<double>();
                const double start=tempo.secondsAt(origin+startTick)-tempo.secondsAt(origin),end=tempo.secondsAt(origin+endTick)-tempo.secondsAt(origin);
                if(!std::isfinite(start)||!std::isfinite(end)||start<previous-1e-8||start<item.note.start-maximumLead-1e-8||end-start<minimum-1e-8||end>item.end+1e-8) {throw std::runtime_error("Manual phoneme timing violates limits: "+item.note.id);}
                local.push_back({item.symbols.at(i++),item.note.language,item.note.id,start,end,item.note.pitch,true});previous=end;
            }
            // An explicit empty list is intentional silence, never automatic fallback.
            if(local.empty()) {local.push_back({"SP",item.note.language,item.note.id,item.note.start,item.end,item.note.pitch,true});}
        }else {
            if(item.leading*minimum>heads[n]-starts[n]+1e-8||(item.symbols.size()-item.leading)*minimum>finish-heads[n]+1e-8) {throw std::runtime_error("Note is too short for minimum phoneme duration: "+item.note.id);}
            double cursor=starts[n];
            for(size_t first=0;first<item.symbols.size();) {const size_t last=first==0&&item.leading?item.leading:item.symbols.size();const double end=last==item.leading?heads[n]:finish;
                double weight=0;for(size_t i=first;i<last;++i) {weight+=std::max(.001,double(result.predictions[item.flat+i]));}
                const double extra=std::max(0.,end-cursor-minimum*(last-first));
                for(size_t i=first;i<last;++i) {const double next=i+1==last?end:cursor+minimum+extra*std::max(.001,double(result.predictions[item.flat+i]))/weight;local.push_back({item.symbols[i],item.note.language,item.note.id,cursor,next,item.note.pitch,false});cursor=next;}
                first=last;
            }
        }
        Json feedback=Json::array();for(const auto& phone:local) {result.phones.push_back(phone);feedback.push_back({{"symbol",phone.symbol},{"startSeconds",phone.start},{"durationSeconds",phone.end-phone.start},{"manual",phone.manual}});}
        if(!item.note.id.empty()) {result.feedback.push_back({{"noteId",item.note.id},{"phonemes",feedback},{"generated",!item.note.phonemes.contains("segments")}});}
    }
    return result;
}
}
