#ifndef DIFFSINGER_PRONUNCIATION_H
#define DIFFSINGER_PRONUNCIATION_H
#include "VoiceCatalog.h"
#include <mutex>
namespace diffsinger {
struct Dictionary {
    std::map<std::string,std::vector<std::string>> entries;
    std::map<std::string,std::string> types, replacements;
};
class Pronunciation {
public:
    explicit Pronunciation(std::shared_ptr<const VoicePackage> voice);
    Json resolve(const Json& request) const;
    std::vector<std::string> map(const std::vector<std::string>& symbols,const std::string& language,const std::string& stage) const;
    std::string type(const std::string& symbol,const std::string& language,const std::string& stage="acoustic") const;
private:
    std::shared_ptr<const VoicePackage> m_voice;
    std::shared_ptr<const Dictionary> dictionary(const std::string& stage,const std::string& language) const;
    mutable std::mutex m_mutex;
    mutable std::map<std::string,std::shared_ptr<const Dictionary>> m_dictionaries;
};
}
#endif
