#include "VoiceCatalog.h"
#include "Hash.h"
#include <yaml-cpp/yaml.h>
#include <yaml-cpp/eventhandler.h>
#include <yaml-cpp/parser.h>
#include <algorithm>
#include <codecvt>
#include <cwctype>
#include <fstream>
#include <locale>
#include <random>
#include <set>
#include <sstream>
#ifdef _WIN32
#include <windows.h>
#endif
namespace diffsinger {
namespace {
struct PathLess {
    bool operator()(const fs::path& a,const fs::path& b) const {
#ifdef _WIN32
        return _wcsicmp(a.c_str(),b.c_str())<0;
#else
        return a<b;
#endif
    }
};
bool within(const fs::path& path,const fs::path& root) {
    auto p=path.begin(),r=root.begin();PathLess less;
    for(;r!=root.end();++r,++p) {if(p==path.end()||less(*p,*r)||less(*r,*p)) {return false;}}return true;
}
std::string read(const fs::path& path,uint64_t limit=4*1024*1024) {
    if(fs::file_size(path)>limit) {throw std::runtime_error("File exceeds read limit: "+path.u8string());}
    std::ifstream input(path,std::ios::binary);if(!input) {throw std::runtime_error("Cannot read "+path.u8string());}
    std::string text{std::istreambuf_iterator<char>(input),std::istreambuf_iterator<char>()};if(input.bad()) {throw std::runtime_error("File read failed");}return text;
}
// Reject alias expansion before building the YAML tree; bound nodes and depth.
struct YamlGuard : YAML::EventHandler {
    unsigned depth=0,nodes=0;
    void touch() {if(++nodes>100000) {throw std::runtime_error("YAML node limit exceeded");}}
    void begin() {touch();if(++depth>32) {throw std::runtime_error("YAML depth limit exceeded");}}
    void OnDocumentStart(const YAML::Mark&) override {}
    void OnDocumentEnd() override {}
    void OnNull(const YAML::Mark&,YAML::anchor_t) override {touch();}
    void OnAlias(const YAML::Mark&,YAML::anchor_t) override {throw std::runtime_error("YAML aliases are unsupported");}
    void OnScalar(const YAML::Mark&,const std::string&,YAML::anchor_t,const std::string&) override {touch();}
    void OnSequenceStart(const YAML::Mark&,const std::string&,YAML::anchor_t,YAML::EmitterStyle::value) override {begin();}
    void OnSequenceEnd() override {--depth;}
    void OnMapStart(const YAML::Mark&,const std::string&,YAML::anchor_t,YAML::EmitterStyle::value) override {begin();}
    void OnMapEnd() override {--depth;}
};
Json convert(const YAML::Node& node) {
    if(node.IsNull()) {return nullptr;}
    if(node.IsMap()) {auto value=Json::object();for(const auto& item:node) {const auto key=item.first.as<std::string>();if(value.contains(key)) {throw std::runtime_error("Duplicate YAML key: "+key);}value[key]=convert(item.second);}return value;}
    if(node.IsSequence()) {auto value=Json::array();for(const auto& item:node) {value.push_back(convert(item));}return value;}
    const auto text=node.as<std::string>();
    if(node.Tag()!="!") {auto scalar=Json::parse(text,nullptr,false);if(!scalar.is_discarded()&&!scalar.is_structured()) {return scalar;}}
    return text;
}
fs::path selectConfig(const fs::path& directory,const std::string& stem,Json& diagnostics) {
    fs::path result;for(const auto* suffix:{".json",".yaml",".yml"}) {const auto path=directory/fs::u8path(stem+suffix);if(!fs::is_regular_file(path)) {continue;}if(result.empty()) {result=path;}else {diagnostics.push_back({{"file",path.u8string()},{"message","Configuration shadowed by "+result.filename().u8string()}});}}return result;
}
std::string string(const Json& object,const std::string& key,const std::string& fallback={}) {
    const auto found=object.find(key);return found!=object.end()&&found->is_string()?found->get<std::string>():fallback;
}
std::string trim(std::string value) {const auto a=value.find_first_not_of(" \t\r\n"),b=value.find_last_not_of(" \t\r\n");return a==std::string::npos?std::string{}:value.substr(a,b-a+1);}
Json textMetadata(const fs::path& path,const std::string& encoding) {
    auto bytes=read(path);bool unicode=false;
    if(bytes.size()>=3&&bytes.compare(0,3,"\xef\xbb\xbf")==0) {bytes.erase(0,3);unicode=true;}
    if(bytes.size()>=2&&(uint8_t(bytes[0])==0xff||uint8_t(bytes[0])==0xfe)) {
        const bool little=uint8_t(bytes[0])==0xff;const bool bom=uint8_t(bytes[1])==(little?0xfe:0xff);
        if(bom) {if(bytes.size()%2) {throw std::runtime_error("Invalid UTF-16 TXT");}std::u16string text;for(size_t i=2;i<bytes.size();i+=2) {text+=char16_t(little?(uint8_t(bytes[i])|(uint16_t(uint8_t(bytes[i+1]))<<8)):(uint8_t(bytes[i+1])|(uint16_t(uint8_t(bytes[i]))<<8)));}bytes=std::wstring_convert<std::codecvt_utf8_utf16<char16_t>,char16_t>{}.to_bytes(text);unicode=true;}
    }
    if(!unicode&&encoding!="utf-8"&&encoding!="UTF-8") {
#ifdef _WIN32
        const auto page=encoding=="gbk"||encoding=="GBK"?936:encoding=="gb18030"?54936:encoding=="shift_jis"?932:0;
        if(!page) {throw std::runtime_error("Unsupported declared TXT encoding: "+encoding);}
        const auto count=MultiByteToWideChar(page,MB_ERR_INVALID_CHARS,bytes.data(),int(bytes.size()),nullptr,0);if(!count) {throw std::runtime_error("Invalid declared TXT encoding");}
        std::wstring wide(count,L'\0');MultiByteToWideChar(page,MB_ERR_INVALID_CHARS,bytes.data(),int(bytes.size()),wide.data(),count);const auto size=WideCharToMultiByte(CP_UTF8,0,wide.data(),count,nullptr,0,nullptr,nullptr);bytes.resize(size);WideCharToMultiByte(CP_UTF8,0,wide.data(),count,bytes.data(),size,nullptr,nullptr);
#else
        throw std::runtime_error("Unsupported declared TXT encoding: "+encoding);
#endif
    }
    Json(bytes).dump(); // Validate UTF-8, never silently replace undecodable text.
    Json result=Json::object();std::istringstream lines(bytes);std::string line;
    while(std::getline(lines,line)) {const auto equals=line.find('=');if(equals==std::string::npos) {continue;}const auto key=trim(line.substr(0,equals));if(!key.empty()) {result[key]=trim(line.substr(equals+1));}}
    return result;
}
void mergeMetadata(VoicePackage& voice,const Json& fields,const fs::path& source,Json& diagnostics) {
    for(auto item=fields.begin();item!=fields.end();++item) {auto key=item.key();if(key=="image") {key="avatar";}if(key=="portrait_opacity") {key="opacity";}
        if(!voice.metadata.contains(key)) {voice.metadata[key]=item.value();voice.sources[key]=source.filename().u8string();}else if(voice.metadata[key]!=item.value()) {diagnostics.push_back({{"file",source.u8string()},{"field",key},{"selectedSource",voice.sources[key]},{"message","Metadata field shadowed by higher priority source"}});}}
}
StageConfig stage(const fs::path& path,const std::vector<fs::path>& roots) {
    StageConfig result;result.source=path;result.values=readConfiguration(path);if(!result.values.is_object()) {throw std::runtime_error("Stage config must be a mapping");}
    for(const auto* key:{"acoustic","fs2","aux","denoiser","linguistic","dur","pitch","variance","model"}) {if(result.values.contains(key)) {if(!result.values[key].is_string()) {throw std::runtime_error(std::string("Invalid model filename: ")+key);}result.models[key]=authorizedPath(path.parent_path(),result.values[key].get<std::string>(),roots);}}
    for(const auto* key:{"phonemes","languages"}) {
        if(!result.values.contains(key)) {continue;}const auto value=result.values[key];auto& map=std::string(key)=="phonemes"?result.phonemes:result.languages;
        if(value.is_string()) {const auto filename=value.get<std::string>();const auto candidate=path.parent_path()/fs::u8path(filename);if(fs::is_regular_file(candidate)) {map=readConfiguration(authorizedPath(path.parent_path(),filename,roots));}else if(std::string(key)=="languages"&&filename.find('.')==std::string::npos) {map[filename]=0;}else {throw std::runtime_error("Missing "+std::string(key)+" mapping: "+filename);}}
        else if(value.is_object()) {map=value;}
        else if(value.is_array()&&std::string(key)=="languages") {int id=0;for(const auto& language:value) {if(!language.is_string()) {throw std::runtime_error("Invalid inline language");}map[language.get<std::string>()]=id++;}}
        else {throw std::runtime_error("Invalid "+std::string(key)+" mapping");}
        if(!map.is_object()||map.empty()) {throw std::runtime_error("Empty or invalid "+std::string(key)+" inventory");}
        std::set<int64_t> ids;for(auto item=map.begin();item!=map.end();++item) {if(item.key().empty()||!item.value().is_number_integer()||item.value().get<int64_t>()<0||!ids.insert(item.value().get<int64_t>()).second) {throw std::runtime_error("Invalid/duplicate "+std::string(key)+" ID");}}
    }
    return result;
}
std::shared_ptr<VoicePackage> load(const fs::path& root,const std::vector<fs::path>& roots,Json& diagnostics) {
    auto voice=std::make_shared<VoicePackage>();voice->root=root;
    auto acoustic=stage(selectConfig(root,"dsconfig",diagnostics),{root});
    if(!acoustic.models.count("acoustic")&&!acoustic.models.count("fs2")) {throw std::runtime_error("No acoustic model");}
    if(acoustic.phonemes.empty()) {throw std::runtime_error("Missing acoustic phoneme inventory");}
    voice->stages["acoustic"]=std::move(acoustic);
    for(const auto& role:std::vector<std::pair<std::string,std::string>>{{"duration","dsdur"},{"pitch","dspitch"},{"variance","dsvariance"}}) {const auto path=selectConfig(root/role.second,"dsconfig",diagnostics);if(!path.empty()) {voice->stages[role.first]=stage(path,{root});}}
    auto vocoder=selectConfig(root/"dsvocoder","vocoder",diagnostics);
    if(vocoder.empty()) {const auto name=string(voice->stages.at("acoustic").values,"vocoder");for(const auto& search:roots) {const auto path=selectConfig(search/"Vocoders"/fs::u8path(name),"vocoder",diagnostics);if(path.empty()) {continue;}if(vocoder.empty()) {vocoder=path;}else {diagnostics.push_back({{"file",path.u8string()},{"message","Shared vocoder shadowed by configured root priority"}});}}}
    if(vocoder.empty()) {throw std::runtime_error("Missing bundled/shared vocoder");}
    voice->stages["vocoder"]=stage(vocoder,{fs::canonical(vocoder.parent_path())});
    const auto& a=voice->stages.at("acoustic").values;const auto& v=voice->stages.at("vocoder").values;
    for(const auto* key:{"sample_rate","hop_size","num_mel_bins","fft_size","win_size","mel_fmin","mel_fmax","mel_base","mel_scale"}) {if(!a.contains(key)||!v.contains(key)||a[key]!=v[key]) {throw std::runtime_error(std::string("Acoustic/vocoder mismatch or missing field: ")+key);}}
    for(const auto* key:{"sample_rate","hop_size","num_mel_bins","fft_size","win_size"}) {if(!a[key].is_number_integer()||a[key].get<int64_t>()<=0) {throw std::runtime_error(std::string("Invalid acoustic field: ")+key);}}
    if(!v.contains("model")||!voice->stages.at("vocoder").models.count("model")) {throw std::runtime_error("Missing vocoder model");}
    for(const auto* extension:{".json",".yaml",".yml"}) {const auto path=root/fs::u8path(std::string("character")+extension);if(fs::is_regular_file(path)) {const auto fields=readConfiguration(path);if(!fields.is_object()) {throw std::runtime_error("Character metadata must be a mapping");}mergeMetadata(*voice,fields,path,diagnostics);voice->unknown[path.filename().u8string()]=fields;}}
    const auto txt=root/"character.txt";if(fs::is_regular_file(txt)) {const auto fields=textMetadata(txt,string(voice->metadata,"text_file_encoding","utf-8"));mergeMetadata(*voice,fields,txt,diagnostics);voice->unknown["character.txt"]=fields;}
    if(!voice->metadata.contains("name")) {voice->metadata["name"]=root.filename().u8string();voice->sources["name"]="directory fallback";}
    voice->id=string(a,"voice_id",string(a,"id"));
    if(fs::is_regular_file(root/"package.json")) {auto manifest=readConfiguration(root/"package.json");voice->unknown["package.json"]=manifest;const auto id=string(manifest,"id");if(!id.empty()) {voice->id=id;}}
    if(fs::is_regular_file(root/"comfort.json")) {voice->unknown["comfort.json"]=readConfiguration(root/"comfort.json");diagnostics.push_back({{"file",(root/"comfort.json").u8string()},{"message","Unsupported comfort extension preserved; not applied to inference"}});}
    // Hash every model/external-data/config/dictionary/embedding file, excluding
    // visual resources: image display changes do not invalidate synthesis.
    std::vector<fs::path> files;std::set<fs::path,PathLess> seen;
    std::vector<fs::path> queue{root};while(!queue.empty()) {const auto directory=fs::canonical(queue.back());queue.pop_back();if(!within(directory,root)||!seen.insert(directory).second) {continue;}
        for(const auto& item:fs::directory_iterator(directory)) {if(item.is_directory()) {queue.push_back(item.path());}else if(item.is_regular_file()) {const auto path=fs::canonical(item.path());if(!within(path,root)) {throw std::runtime_error("Package file escapes authorized package");}const auto suffix=path.extension().u8string();if(suffix==".png"||suffix==".jpg"||suffix==".jpeg"||suffix==".svg"||suffix==".webp") {continue;}files.push_back(path);}}
    }
    std::sort(files.begin(),files.end(),PathLess{});Sha256 fingerprint;
    for(const auto& path:files) {fingerprint.add(path.lexically_relative(root).generic_u8string());fingerprint.add("\n"+hashFile(path)+"\n");}
    if(!within(vocoder,root)) {fingerprint.add("shared-vocoder-config\n"+hashFile(vocoder));for(const auto& model:voice->stages.at("vocoder").models) {fingerprint.add(model.first+"\n"+hashFile(model.second));}}
    voice->fingerprint=fingerprint.finish();
    for(const auto* key:{"avatar","portrait"}) {const auto filename=string(voice->metadata,key);if(filename.empty()) {continue;}try {const auto path=authorizedPath(root,filename,{root});const auto bytes=read(path,16*1024*1024);const auto suffix=path.extension().u8string();const auto mime=suffix==".png"?"image/png":suffix==".jpg"||suffix==".jpeg"?"image/jpeg":suffix==".webp"?"image/webp":suffix==".svg"?"image/svg+xml":"";if(!*mime) {throw std::runtime_error("Unsupported image format");}auto resource=std::make_shared<Resource>();resource->sha256=hashText(bytes);resource->id="diffsinger-image:"+resource->sha256;resource->mime=mime;resource->bytes.assign(bytes.begin(),bytes.end());voice->resources[key]=resource;}catch(const std::exception& error) {diagnostics.push_back({{"file",filename},{"stage","resource"},{"message",error.what()}});}}
    return voice;
}
std::string newId() {std::random_device random;Sha256 hash;for(unsigned i=0;i<8;++i) {auto value=random();hash.add(&value,sizeof(value));}return "installed-"+hash.finish().substr(0,32);}
}
Json readConfiguration(const fs::path& path) {
    const auto text=read(path);Json result;
    if(path.extension()==".json") {result=Json::parse(text);}
    else {std::istringstream stream(text);YAML::Parser parser(stream);YamlGuard guard;if(!parser.HandleNextDocument(guard)) {throw std::runtime_error("Empty YAML config");}if(parser.HandleNextDocument(guard)) {throw std::runtime_error("Multiple YAML documents unsupported");}result=convert(YAML::Load(text));}
    result.dump();return result;
}
fs::path authorizedPath(const fs::path& base,const std::string& relative,const std::vector<fs::path>& roots) {
    const auto path=fs::canonical(base/fs::u8path(relative));for(const auto& root:roots) {if(within(path,root)) {return path;}}throw std::runtime_error("Path escapes authorized roots: "+relative);
}
Json VoicePackage::declaration() const {
    auto languages=Json::array();for(auto item=stages.at("acoustic").languages.begin();item!=stages.at("acoustic").languages.end();++item) {languages.push_back(item.key());}if(languages.empty()) {languages.push_back("zh");}
    Json value{{"id",id},{"name",string(metadata,"name")},{"version",fingerprint},{"contentFingerprint",fingerprint},{"author",string(metadata,"author")},
        {"description",string(metadata,"description")},{"languages",languages},{"defaultLanguage",std::find(languages.begin(),languages.end(),Json("zh"))!=languages.end()?"zh":languages[0].get<std::string>()},{"defaultLyric","a"},{"metadata",metadata},{"metadataSources",sources}};
    for(const auto& item:resources) {value[item.first]=item.second->id;}if(metadata.contains("opacity")) {value["portraitOpacity"]=metadata["opacity"];}return value;
}
Json Catalog::declaration() const {auto list=Json::array();for(const auto& voice:voices) {list.push_back(voice->declaration());}return {{"voices",list},{"installations",installations},{"diagnostics",diagnostics},{"catalogRevision",revision}};}
std::shared_ptr<const VoicePackage> Catalog::find(const std::string& id) const {for(const auto& voice:voices) {if(voice->id==id) {return voice;}}return {};}
std::shared_ptr<const Catalog> scan(const Json& context,uint64_t revision) {
    auto result=std::make_shared<Catalog>();result->revision=revision;
    result->installations=context.value("installations",Json::array());if(!result->installations.is_array()) {throw std::runtime_error("Invalid host installation registry");}
    const auto settings=context.value("engineSettings",Json::object());auto directories=settings.value("diffsinger.voicebankDirectories",Json::array());
    if(!directories.is_array()||directories.size()>128) {throw std::runtime_error("Voicebank directories must be a bounded string array");}
    if(directories.empty()&&context.contains("defaultVoicebankDirectory")) {directories.push_back(context["defaultVoicebankDirectory"]);}
    std::vector<fs::path> roots;std::set<fs::path,PathLess> rootSet;
    for(const auto& directory:directories) {if(!directory.is_string()) {throw std::runtime_error("Voicebank directory must be a string");}if(directory.get<std::string>().empty()) {continue;}try {const auto path=fs::canonical(fs::u8path(directory.get<std::string>()));if(!fs::is_directory(path)) {throw std::runtime_error("Not a directory");}if(rootSet.insert(path).second) {roots.push_back(path);}}catch(const std::exception& error) {result->diagnostics.push_back({{"root",directory},{"message",error.what()}});}}
    std::set<fs::path,PathLess> visited;std::map<std::string,std::string> ids,fingerprints;
    for(const auto& root:roots) {std::vector<fs::path> queue{root};while(!queue.empty()) {const auto path=queue.back();queue.pop_back();try {const auto canonical=fs::canonical(path);if(!within(canonical,root)||!visited.insert(canonical).second) {continue;}
            if(visited.size()>100000) {throw std::runtime_error("Scan directory limit exceeded");}
            const auto candidate=selectConfig(canonical,"dsconfig",result->diagnostics);
            if(!candidate.empty()) {try {const auto config=readConfiguration(candidate);if(config.is_object()&&(config.contains("acoustic")||config.contains("fs2"))) {
                auto voice=load(canonical,roots,result->diagnostics);
                if(fingerprints.count(voice->fingerprint)) {result->diagnostics.push_back({{"root",canonical.u8string()},{"message","Identical package copy deduplicated"}});}
                else {
                    if(voice->id.empty()) {std::vector<std::string> matches;for(const auto& row:result->installations) {if(!row.is_object()) {throw std::runtime_error("Invalid installation registry row");}if(string(row,"path")==canonical.u8string()) {voice->id=string(row,"id");break;}if(string(row,"fingerprint")==voice->fingerprint&&!string(row,"id").empty()) {matches.push_back(string(row,"id"));}}
                        if(voice->id.empty()) {std::sort(matches.begin(),matches.end());matches.erase(std::unique(matches.begin(),matches.end()),matches.end());if(matches.size()>1) {throw std::runtime_error("Ambiguous moved package; rebind required");}voice->id=matches.empty()?newId():matches[0];}}
                    if(voice->id.empty()||voice->id.size()>256||voice->id.find('/')!=std::string::npos) {throw std::runtime_error("Invalid explicit/installation voice ID");}
                    if(ids.count(voice->id)) {result->diagnostics.push_back({{"root",canonical.u8string()},{"voiceId",voice->id},{"message","Conflicting same ID with different content; shadowed by root priority"},{"selected",ids[voice->id]}});}
                    else {ids[voice->id]=canonical.u8string();fingerprints[voice->fingerprint]=voice->id;bool updated=false;for(auto& row:result->installations) {if(string(row,"id")==voice->id) {row["path"]=canonical.u8string();row["fingerprint"]=voice->fingerprint;updated=true;break;}}if(!updated) {result->installations.push_back({{"id",voice->id},{"path",canonical.u8string()},{"fingerprint",voice->fingerprint}});}result->voices.push_back(std::move(voice));}
                }
            }}catch(const std::exception& error) {result->diagnostics.push_back({{"root",canonical.u8string()},{"message",error.what()}});}}
            std::vector<fs::path> children;for(const auto& item:fs::directory_iterator(canonical)) {if(item.is_directory()) {children.push_back(item.path());}}std::sort(children.begin(),children.end(),PathLess{});for(auto i=children.rbegin();i!=children.rend();++i) {queue.push_back(*i);}
        }catch(const std::exception& error) {result->diagnostics.push_back({{"root",path.u8string()},{"message",error.what()}});}
    }}
    return result;
}
Json engineSettings() {return {{"schemaVersion",1},{"name","DiffSinger"},{"engineType","ai"},{"engineSettings",Json::array({
    {{"id","diffsinger.renderSteps"},{"name","Rendering steps"},{"type","int"},{"min",1},{"max",100},{"step",1},{"default",20}},
    {{"id","diffsinger.voicebankDirectories"},{"name","Voicebank directories"},{"type","directory-list"},{"default",Json::array()},{"maxItems",128}}
})}};}
}
