/* Dictionary precedence and symbol remapping adapted from DiffSingerForTuneLab.
   Copyright (c) 2026 Jingang. MIT; see licenses/tlds-MIT.txt. */
#include "Pronunciation.h"
#include "NativeRuntime.h"
#include <cctype>
#include <fstream>
#include <sstream>
#include <stdexcept>
namespace diffsinger {
namespace {
std::string trim(std::string value) {
    const auto first=value.find_first_not_of(" \t\r\n");if(first==std::string::npos) {return {};}
    return value.substr(first,value.find_last_not_of(" \t\r\n")-first+1);
}
std::string lower(std::string value) {for(auto& c:value) {if(static_cast<unsigned char>(c)<128) {c=char(std::tolower(static_cast<unsigned char>(c)));}}return value;}
std::vector<uint32_t> codepoints(const std::string& value) {
    std::vector<uint32_t> result;
    for(size_t i=0;i<value.size();) {
        const uint8_t first=value[i++];uint32_t cp=first;unsigned extra=0;
        if(first>=0xc2&&first<=0xdf) {cp=first&31;extra=1;}else if(first>=0xe0&&first<=0xef) {cp=first&15;extra=2;}
        else if(first>=0xf0&&first<=0xf4) {cp=first&7;extra=3;}else if(first>=128) {throw std::runtime_error("Invalid UTF-8 lyric");}
        if(i+extra>value.size()) {throw std::runtime_error("Truncated UTF-8 lyric");}
        for(unsigned n=0;n<extra;++n) {const uint8_t next=value[i++];if((next&0xc0)!=0x80) {throw std::runtime_error("Invalid UTF-8 lyric");}cp=(cp<<6)|(next&63);}
        if((extra==1&&cp<128)||(extra==2&&cp<2048)||(extra==3&&cp<65536)||cp>0x10ffff||(cp>=0xd800&&cp<=0xdfff)) {throw std::runtime_error("Invalid UTF-8 lyric");}
        result.push_back(cp);
    }
    return result;
}
std::string untone(const std::string& syllable) {
    std::string result;
    for(const auto cp:codepoints(syllable)) {
        if(cp<128) {if(cp<'0'||cp>'5') {result+=char(std::tolower(static_cast<unsigned char>(cp)));}}
        else if(cp==0xfc||cp==0x1d6||cp==0x1d8||cp==0x1da||cp==0x1dc) {result+='v';}
        else if(cp==0x101||cp==0xe1||cp==0x1ce||cp==0xe0) {result+='a';}
        else if(cp==0x113||cp==0xe9||cp==0x11b||cp==0xe8||cp==0xea) {result+='e';}
        else if(cp==0x12b||cp==0xed||cp==0x1d0||cp==0xec) {result+='i';}
        else if(cp==0x14d||cp==0xf3||cp==0x1d2||cp==0xf2) {result+='o';}
        else if(cp==0x16b||cp==0xfa||cp==0x1d4||cp==0xf9) {result+='u';}
        else if(cp==0x144||cp==0x148||cp==0x1f9) {result+='n';}
        else if(cp==0x1e3f) {result+='m';}
        else if(cp<0x300||cp>0x36f) {throw std::runtime_error("Unsupported pinyin character");}
    }
    return result;
}
std::vector<std::string> words(const std::string& value) {std::istringstream in(value);std::vector<std::string> out;std::string word;while(in>>word) {out.push_back(untone(word));}return out;}
struct Mandarin {
    std::map<uint32_t,std::string> characters;
    std::map<std::vector<uint32_t>,std::vector<std::string>> phrases;
    size_t longest=1;
    Mandarin() {
        const auto root=packageDirectory()/"data";
        std::ifstream chars(root/"pinyin.txt"),phraseFile(root/"phrases.txt");
        if(!chars||!phraseFile) {throw std::runtime_error("Packaged Mandarin pronunciation data is missing");}
        std::string line;
        while(std::getline(chars,line)) {if(line.rfind("U+",0)!=0) {continue;}const auto colon=line.find(':');if(colon==std::string::npos) {continue;}
            const auto cp=uint32_t(std::stoul(line.substr(2,colon-2),nullptr,16));const auto end=line.find_first_of(",#",colon+1);characters[cp]=untone(trim(line.substr(colon+1,end-colon-1)));}
        while(std::getline(phraseFile,line)) {if(line.empty()||line[0]=='#') {continue;}const auto colon=line.find(':');if(colon==std::string::npos) {continue;}
            auto text=codepoints(trim(line.substr(0,colon)));const auto comment=line.find('#',colon+1);auto reading=words(line.substr(colon+1,comment-colon-1));if(text.size()==reading.size()&&text.size()<=64) {longest=std::max(longest,text.size());phrases.emplace(std::move(text),std::move(reading));}}
    }
    std::vector<std::string> read(const std::string& lyric) const {
        const auto points=codepoints(lyric);std::vector<std::string> result;
        for(size_t i=0;i<points.size();) {if(points[i]==32||points[i]==9) {++i;continue;}bool matched=false;
            for(size_t length=std::min(longest,points.size()-i);length>1;--length) {const std::vector<uint32_t> key(points.begin()+i,points.begin()+i+length);const auto found=phrases.find(key);if(found!=phrases.end()) {result.insert(result.end(),found->second.begin(),found->second.end());i+=length;matched=true;break;}}
            if(matched) {continue;}const auto found=characters.find(points[i]);if(found==characters.end()) {throw std::runtime_error("Unknown Mandarin lyric; enter an explicit reading or phonemes");}result.push_back(found->second);++i;
        }
        return result;
    }
};
void load(Dictionary& result,const fs::path& path,bool entries) {
    const auto data=readConfiguration(path,32*1024*1024,4000000);
    if(entries&&data.contains("entries")) {if(!data["entries"].is_array()) {throw std::runtime_error("Dictionary entries must be an array");}
        for(const auto& entry:data["entries"]) {const auto grapheme=entry.at("grapheme").get<std::string>();auto phones=entry.at("phonemes").get<std::vector<std::string>>();if(grapheme.empty()||phones.empty()||phones.size()>128) {throw std::runtime_error("Invalid dictionary entry");}result.entries[grapheme]=std::move(phones);}}
    if(data.contains("symbols")) {for(const auto& symbol:data["symbols"]) {result.types[symbol.at("symbol").get<std::string>()]=symbol.at("type").get<std::string>();}}
    if(data.contains("replacements")) {for(const auto& item:data["replacements"]) {result.replacements[item.at("from").get<std::string>()]=item.at("to").get<std::string>();}}
}
}
Pronunciation::Pronunciation(std::shared_ptr<const VoicePackage> voice):m_voice(std::move(voice)) {}
std::shared_ptr<const Dictionary> Pronunciation::dictionary(const std::string& role,const std::string& language) const {
    std::lock_guard lock(m_mutex);const auto key=role+":"+language;const auto cached=m_dictionaries.find(key);if(cached!=m_dictionaries.end()) {return cached->second;}
    const auto& stage=m_voice->stages.at(role);if(!stage.languages.contains(language)) {throw std::runtime_error("Language is unavailable in stage "+role+": "+language);}
            auto dictionary=std::make_shared<Dictionary>();auto directory=stage.source.parent_path();
            // Acoustic packages commonly keep the shared dictionary with duration.
            if(role=="acoustic"&&!fs::exists(directory/"dsdict.yaml")&&m_voice->stages.count("duration")) {directory=m_voice->stages.at("duration").source.parent_path();}
            const auto common=directory/"dsdict.yaml";if(fs::exists(common)) {load(*dictionary,authorizedPath(directory,"dsdict.yaml",{m_voice->root}),false);}
            std::vector<std::string> names;
            const auto external=stage.values.value("dictionary",std::string{});if(!external.empty()) {names.push_back(external);}
            names.insert(names.end(),{"dsdict-"+language+".yaml","dsdict-zh-"+language+".yaml","dsdict.yaml"});
            for(const auto& name:names) {if(fs::exists(directory/fs::u8path(name))) {load(*dictionary,authorizedPath(directory,name,{m_voice->root}),true);break;}}
            m_dictionaries[key]=dictionary;return dictionary;
}
std::vector<std::string> Pronunciation::map(const std::vector<std::string>& symbols,const std::string& language,const std::string& stage) const {
    const auto& configuration=m_voice->stages.at(stage);const auto loaded=dictionary(stage,language);
    const auto& dict=*loaded;std::vector<std::string> result;
    for(auto symbol:symbols) {const auto replaced=dict.replacements.find(symbol);if(replaced!=dict.replacements.end()) {symbol=replaced->second;}
        if(!configuration.phonemes.contains(symbol)) {
            const auto slash=symbol.find('/');const auto bare=slash==std::string::npos?symbol:symbol.substr(slash+1);
            if(configuration.phonemes.contains(language+"/"+bare)) {symbol=language+"/"+bare;}
            else if(configuration.phonemes.contains(bare)) {symbol=bare;}
            else {throw std::runtime_error("Unknown phoneme in "+stage+": "+symbol);}
        }
        result.push_back(symbol);
    }
    return result;
}
std::string Pronunciation::type(const std::string& symbol,const std::string& language,const std::string& stage) const {
    if(symbol=="SP"||symbol=="AP"||symbol=="ExAP") {return "vowel";}
    const auto loaded=dictionary(stage,language);const auto& dict=*loaded;const auto found=dict.types.find(symbol);
    if(found==dict.types.end()) {return "consonant";}const auto value=lower(found->second);
    return value=="vowel"?"vowel":(value=="semivowel"||value=="liquid"||value=="glide")?"glide":"consonant";
}
Json Pronunciation::resolve(const Json& request) const {
    const auto lyric=request.value("lyric",std::string{});const auto language=request.value("language",std::string("zh"));
    Json result{{"text",lyric},{"source","nativeDiffSinger"},{"phonemeSet","diffsinger:"+m_voice->fingerprint},{"generated",false},{"continuation",false},{"phonemes",Json::array()},{"diagnostic",""}};
    try {
        if(lyric.size()>4096) {throw std::runtime_error("Lyric exceeds supported length");}
        const auto loaded=dictionary("acoustic",language);
        const auto reading=trim(request.value("pronunciation",std::string{}));const auto text=trim(reading.empty()?lyric:reading);
        if(text=="-") {throw std::runtime_error("Continuation must be adjacent to a preceding sounding note");}
        if(text.empty()) {result["source"]="rest";result["generated"]=true;return result;}
        const auto& dict=*loaded;auto entry=dict.entries.find(text);if(entry==dict.entries.end()) {entry=dict.entries.find(lower(text));}
        std::vector<std::string> symbols;
        if(entry!=dict.entries.end()) {symbols=entry->second;result["source"]="voiceDictionary";result["text"]=text;}
        else {
            std::vector<std::string> syllables;
            if(!reading.empty()) {syllables=words(text);result["source"]="manualPronunciation";}
            else {
                const auto phonemizer=m_voice->metadata.value("default_phonemizer",std::string{});
                if(language!="zh"||(!phonemizer.empty()&&phonemizer!="OpenUtau.Core.DiffSinger.DiffSingerChinesePhonemizer")) {throw std::runtime_error("No automatic phonemizer for "+language+" / "+phonemizer+"; bank dictionary and explicit readings remain available");}
                static const Mandarin mandarin;syllables=mandarin.read(text);
            }
            std::string display;for(const auto& syllable:syllables) {const auto word=dict.entries.find(syllable);if(word==dict.entries.end()) {throw std::runtime_error("Reading is missing from the voice dictionary: "+syllable);}symbols.insert(symbols.end(),word->second.begin(),word->second.end());if(!display.empty()) display+=" ";display+=syllable;}result["text"]=display;
        }
        result["phonemes"]=map(symbols,language,"acoustic");result["generated"]=true;
    }catch(const std::exception& error) {result["diagnostic"]=error.what();}
    return result;
}
}
