#include "Speaker.h"
#include <cmath>
#include <fstream>
namespace diffsinger {
Json speakerChoices(const VoicePackage& voice) {
    Json result=Json::array();const auto speakers=voice.stages.at("acoustic").values.value("speakers",Json::array());
    if(!speakers.is_array()) {throw std::runtime_error("Speakers must be an array");}
    for(const auto& speaker:speakers) {const auto id=speaker.get<std::string>();if(id.empty()) {throw std::runtime_error("Empty speaker ID");}result.push_back({{"id",id},{"name",id}});}
    if(result.empty()) {for(const auto& bank:voice.metadata.value("subbanks",Json::array())) {const auto id=bank.value("suffix",std::string{});if(!id.empty()) {result.push_back({{"id",id},{"name",bank.value("color",id)}});}}}
    return result;
}
std::vector<float> speakerEmbedding(const VoicePackage& voice,const StageConfig& stage,const Json& parameters) {
    const auto speakers=stage.values.value("speakers",Json::array());
    if(!speakers.is_array()||speakers.empty()) {throw std::runtime_error("Model requires speaker embedding but no speakers are declared");}
    const auto hidden=stage.values.value("hidden_size",int64_t(0));if(hidden<=0||hidden>8192) {throw std::runtime_error("Invalid speaker hidden_size");}
    const auto selected=parameters.value("diffsinger.speaker",speakers[0].get<std::string>());
    auto weights=parameters.value("diffsinger.speakerWeights",Json::object());
    if(!weights.is_object()) {throw std::runtime_error("Speaker weights must be an object");}if(weights.empty()) {weights[selected]=1.;}
    double total=0;for(auto item=weights.begin();item!=weights.end();++item) {if(!item.value().is_number()||!std::isfinite(item.value().get<double>())||item.value().get<double>()<0) {throw std::runtime_error("Invalid speaker weight");}total+=item.value().get<double>();}
    if(!(total>0&&std::isfinite(total))) {throw std::runtime_error("Speaker weights must have positive finite sum");}
    std::vector<float> output(size_t(hidden),0);
    for(auto item=weights.begin();item!=weights.end();++item) {
        bool available=false;for(const auto& speaker:speakers) {if(speaker.get<std::string>()==item.key()) {available=true;}}
        if(!available) {throw std::runtime_error("Unknown speaker ID: "+item.key());}
        const auto path=authorizedPath(stage.source.parent_path(),item.key()+".emb",{voice.root});
        if(fs::file_size(path)!=uint64_t(hidden)*4) {throw std::runtime_error("Speaker embedding size does not match hidden_size: "+item.key());}
        std::vector<float> values(size_t(hidden),0);std::ifstream file(path,std::ios::binary);file.read(reinterpret_cast<char*>(values.data()),std::streamsize(hidden*4));if(!file) {throw std::runtime_error("Cannot read speaker embedding");}
        for(size_t i=0;i<values.size();++i) {if(!std::isfinite(values[i])) {throw std::runtime_error("Non-finite speaker embedding");}output[i]+=float(values[i]*(item.value().get<double>()/total));}
    }
    return output;
}
}
