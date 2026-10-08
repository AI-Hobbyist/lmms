#ifndef DIFFSINGER_CPU_MODEL_H
#define DIFFSINGER_CPU_MODEL_H
#include "NativeRuntime.h"
#include "VoiceCatalog.h"
#include <atomic>
#include <cstring>
#include <stdexcept>
namespace diffsinger {
struct Tensor {
    ONNXTensorElementDataType type=ONNX_TENSOR_ELEMENT_DATA_TYPE_UNDEFINED;
    std::vector<int64_t> dimensions;
    std::vector<uint8_t> bytes;
    template<class T> static Tensor make(ONNXTensorElementDataType type,std::vector<int64_t> shape,const std::vector<T>& values) {
        Tensor result{type,std::move(shape),std::vector<uint8_t>(values.size()*sizeof(T))};
        if(!values.empty()) {std::memcpy(result.bytes.data(),values.data(),result.bytes.size());}return result;
    }
    template<class T> std::vector<T> values() const {if(bytes.size()%sizeof(T)) {throw std::runtime_error("Invalid tensor byte count");}std::vector<T> result(bytes.size()/sizeof(T));if(!bytes.empty()) {std::memcpy(result.data(),bytes.data(),bytes.size());}return result;}
};
using Tensors=std::map<std::string,Tensor>;
class CpuModel {
public:
    CpuModel(Ort::Env& environment,const fs::path& path,std::string stage);
    bool accepts(const std::string& name) const;
    Tensors run(const Tensors& inputs,const std::atomic<bool>& cancelled);
private:
    struct Port {std::string name;ONNXTensorElementDataType type;std::vector<int64_t> dimensions;};
    std::string m_stage;
    fs::path m_path;
    Ort::Session m_session{nullptr};
    std::vector<Port> m_inputs,m_outputs;
};
}
#endif
