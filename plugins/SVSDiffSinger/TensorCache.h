#ifndef DIFFSINGER_TENSOR_CACHE_H
#define DIFFSINGER_TENSOR_CACHE_H
#include "CpuModel.h"
namespace diffsinger {
// Host-supplied cache/SVS/DiffSinger; only this codec's .tensor files are owned.
class TensorCache {
public:
    explicit TensorCache(fs::path directory,uint64_t maximumBytes=512*1024*1024);
    static std::string key(const std::string& identity,const Tensors& inputs);
    bool load(const std::string& key,Tensors& outputs) const;
    void save(const std::string& key,const Tensors& outputs) const;
private:
    fs::path m_directory;
    uint64_t m_maximum;
    void trim() const;
};
}
#endif
