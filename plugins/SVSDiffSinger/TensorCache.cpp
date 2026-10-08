#include "TensorCache.h"
#include "Hash.h"
#include <algorithm>
#include <chrono>
#include <fstream>
#include <mutex>
#include <cmath>
namespace diffsinger {
namespace {
constexpr uint64_t MaximumPayload=128*1024*1024,MaximumMetadata=65536;
std::mutex cacheMutex;
bool validKey(const std::string& key) {return key.size()==64&&std::all_of(key.begin(),key.end(),[](char c){return (c>='0'&&c<='9')||(c>='a'&&c<='f');});}
size_t tensorBytes(const Tensor& tensor) {
    uint64_t count=1;if(tensor.dimensions.size()>8) {throw std::runtime_error("Cached tensor rank exceeds bound");}
    for(const auto value:tensor.dimensions) {if(value<0||uint64_t(value)>MaximumPayload||count>MaximumPayload/std::max(int64_t(1),value)) {throw std::runtime_error("Cached tensor dimensions exceed bound");}count*=uint64_t(value);}
    const auto width=tensor.type==ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT?4:tensor.type==ONNX_TENSOR_ELEMENT_DATA_TYPE_INT64?8:tensor.type==ONNX_TENSOR_ELEMENT_DATA_TYPE_BOOL?1:0;
    if(!width||count>MaximumPayload/width) {throw std::runtime_error("Invalid cached tensor type/size");}return size_t(count*width);
}
void validate(const Tensor& tensor) {
    if(tensorBytes(tensor)!=tensor.bytes.size()) {throw std::runtime_error("Invalid cached tensor byte count");}
    if(tensor.type==ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT) {for(const auto value:tensor.values<float>()) {if(!std::isfinite(value)) {throw std::runtime_error("Non-finite cached tensor");}}}
    if(tensor.type==ONNX_TENSOR_ELEMENT_DATA_TYPE_BOOL) {for(const auto value:tensor.bytes) {if(value>1) {throw std::runtime_error("Invalid cached boolean");}}}
}
Json metadata(const Tensors& tensors) {
    if(tensors.empty()||tensors.size()>32) {throw std::runtime_error("Invalid cached tensor count");}
    Json result=Json::array();for(const auto& item:tensors) {if(item.first.empty()||item.first.size()>256) {throw std::runtime_error("Invalid cached tensor name");}validate(item.second);result.push_back({{"name",item.first},{"type",int(item.second.type)},{"shape",item.second.dimensions},{"bytes",item.second.bytes.size()}});}return result;
}
void writeNumber(std::ostream& out,uint64_t number) {for(unsigned i=0;i<8;++i) {out.put(char(number>>(i*8)));}}
uint64_t readNumber(std::istream& in) {uint64_t number=0;for(unsigned i=0;i<8;++i) {const auto value=in.get();if(value==EOF) {throw std::runtime_error("Truncated tensor cache header");}number|=uint64_t(uint8_t(value))<<(i*8);}return number;}
}
TensorCache::TensorCache(fs::path directory,uint64_t maximumBytes):m_maximum(std::min(maximumBytes,uint64_t(512*1024*1024))) {
    if(directory.empty()) {return;}
    // The caller controls the root; refuse a differently named accidental cache.
    if(directory.filename()!="DiffSinger"||directory.parent_path().filename()!=fs::path("SVS")) {throw std::runtime_error("Tensor cache must use cache/SVS/DiffSinger");}
    try {fs::create_directories(directory);m_directory=fs::canonical(directory);}catch(const fs::filesystem_error&) {m_directory.clear();}
}
std::string TensorCache::key(const std::string& identity,const Tensors& inputs) {
    Sha256 hash;hash.add("DiffSinger.tensor.v1\n");hash.add(Json{{"identity",identity},{"inputs",metadata(inputs)}}.dump());
    for(const auto& item:inputs) {hash.add(item.second.bytes.data(),item.second.bytes.size());}return hash.finish();
}
bool TensorCache::load(const std::string& key,Tensors& outputs) const {
    outputs.clear();if(m_directory.empty()||!validKey(key)) {return false;}std::lock_guard lock(cacheMutex);const auto path=m_directory/(key+".tensor");
    try {
        const auto status=fs::symlink_status(path);if(!fs::is_regular_file(status)||fs::is_symlink(status)) {return false;}
        const auto size=fs::file_size(path);if(size>MaximumPayload+MaximumMetadata+96||size<96) {throw std::runtime_error("Tensor cache file exceeds bound");}
        std::ifstream file(path,std::ios::binary);if(!file) {return false;}char magic[12];file.read(magic,12);if(std::string(magic,12)!="SVSTENSOR1\r\n") {throw std::runtime_error("Unknown tensor cache codec");}
        const auto metaSize=readNumber(file),payloadSize=readNumber(file);if(metaSize>MaximumMetadata||payloadSize>MaximumPayload||12+16+64+metaSize+payloadSize!=size) {throw std::runtime_error("Invalid tensor cache length");}
        std::string expected(64,'\0'),text(size_t(metaSize),'\0');file.read(expected.data(),64);file.read(text.data(),std::streamsize(text.size()));const auto ports=Json::parse(text);if(!ports.is_array()||ports.empty()||ports.size()>32) {throw std::runtime_error("Invalid tensor cache ports");}
        Sha256 hash;hash.add(text);uint64_t used=0;Tensors loaded;
        for(const auto& port:ports) {const auto name=port.at("name").get<std::string>();if(name.empty()||name.size()>256||loaded.count(name)) {throw std::runtime_error("Invalid tensor cache port name");}
            Tensor tensor{ONNXTensorElementDataType(port.at("type").get<int>()),port.at("shape").get<std::vector<int64_t>>(),{}};const auto bytes=port.at("bytes").get<uint64_t>();if(bytes!=tensorBytes(tensor)||bytes>payloadSize-used) {throw std::runtime_error("Invalid tensor cache port bytes");}
            tensor.bytes.resize(size_t(bytes));file.read(reinterpret_cast<char*>(tensor.bytes.data()),std::streamsize(bytes));if(!file) {throw std::runtime_error("Truncated tensor cache payload");}validate(tensor);hash.add(tensor.bytes.data(),tensor.bytes.size());used+=bytes;loaded.emplace(name,std::move(tensor));}
        if(used!=payloadSize||hash.finish()!=expected) {throw std::runtime_error("Tensor cache checksum mismatch");}
        outputs=std::move(loaded);std::error_code error;fs::last_write_time(path,fs::file_time_type::clock::now(),error);return true;
    }catch(const fs::filesystem_error&) {return false;}catch(const std::exception&) {std::error_code error;fs::remove(path,error);return false;}
}
void TensorCache::save(const std::string& key,const Tensors& outputs) const {
    if(m_directory.empty()||!validKey(key)) {return;}std::lock_guard lock(cacheMutex);fs::path temporary;
    try {
        const auto text=metadata(outputs).dump();if(text.size()>MaximumMetadata) {return;}uint64_t payload=0;Sha256 hash;hash.add(text);
        for(const auto& item:outputs) {if(item.second.bytes.size()>MaximumPayload-payload) {return;}payload+=item.second.bytes.size();hash.add(item.second.bytes.data(),item.second.bytes.size());}
        if(payload+text.size()+92>m_maximum) {return;}
        const auto destination=m_directory/(key+".tensor");temporary=m_directory/(key+".tmp-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        {std::ofstream file(temporary,std::ios::binary);if(!file) {return;}file.write("SVSTENSOR1\r\n",12);writeNumber(file,text.size());writeNumber(file,payload);const auto digest=hash.finish();file.write(digest.data(),64);file.write(text.data(),std::streamsize(text.size()));for(const auto& item:outputs) {file.write(reinterpret_cast<const char*>(item.second.bytes.data()),std::streamsize(item.second.bytes.size()));}file.flush();if(!file) {throw std::runtime_error("Cannot write tensor cache");}}
        std::error_code error;fs::rename(temporary,destination,error);if(error) {fs::remove(temporary,error);}trim();
    }catch(const std::exception&) {if(!temporary.empty()) {std::error_code error;fs::remove(temporary,error);}}
}
void TensorCache::trim() const {
    std::vector<fs::directory_entry> files;uint64_t total=0;
    for(const auto& entry:fs::directory_iterator(m_directory)) {if(entry.path().extension()!=".tensor"||!validKey(entry.path().stem().u8string())||!fs::is_regular_file(entry.symlink_status())||fs::is_symlink(entry.symlink_status())) {continue;}total+=entry.file_size();files.push_back(entry);}
    std::sort(files.begin(),files.end(),[](const auto& a,const auto& b){return a.last_write_time()<b.last_write_time();});
    for(const auto& entry:files) {if(total<=m_maximum) {break;}std::error_code error;const auto size=entry.file_size();if(fs::remove(entry.path(),error)) {total-=size;}}
}
}
