#ifndef DIFFSINGER_VOICE_CATALOG_H
#define DIFFSINGER_VOICE_CATALOG_H
#include <filesystem>
#include <map>
#include <memory>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>
namespace diffsinger {
namespace fs = std::filesystem;
using Json = nlohmann::ordered_json;
struct StageConfig {
    fs::path source;
    Json values, phonemes = Json::object(), languages = Json::object();
    std::map<std::string,fs::path> models;
};
struct Resource {
    std::string id, mime, sha256;
    std::vector<uint8_t> bytes;
};
struct VoicePackage {
    fs::path root;
    std::string id, fingerprint;
    Json metadata = Json::object(), sources = Json::object(), unknown = Json::object();
    std::map<std::string,StageConfig> stages;
    std::map<std::string,std::shared_ptr<const Resource>> resources;
    Json declaration() const;
};
struct Catalog {
    std::vector<std::shared_ptr<const VoicePackage>> voices;
    Json installations = Json::array(), diagnostics = Json::array();
    uint64_t revision = 0;
    Json declaration() const;
    std::shared_ptr<const VoicePackage> find(const std::string& id) const;
};
// Scan input is a frozen list of explicitly authorized roots and host-owned IDs.
std::shared_ptr<const Catalog> scan(const Json& context, uint64_t revision);
Json readConfiguration(const fs::path& path,uint64_t byteLimit=4*1024*1024,unsigned nodeLimit=100000);
fs::path authorizedPath(const fs::path& base, const std::string& relative, const std::vector<fs::path>& roots);
Json engineSettings(const fs::path& defaultVocoderDirectory={});
}
#endif
