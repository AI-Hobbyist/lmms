#ifndef DIFFSINGER_MODEL_SEED_H
#define DIFFSINGER_MODEL_SEED_H
#include "VoiceCatalog.h"
#include <fstream>
#include <string_view>
#include <cstring>
namespace diffsinger {
// ONNX protobuf fields follow onnx/onnx.proto v1.19.0. Only standard random
// operator seed attributes are changed in memory; source packages stay intact.
class ModelSeed {
    struct Field {unsigned number,wire;std::string_view raw,value;};
    uint32_t m_seed;uint64_t m_nodes=0;fs::path m_directory;
    static uint64_t varint(std::string_view text,size_t& offset) {
        uint64_t value=0;for(unsigned i=0;i<10;++i) {if(offset>=text.size()) {throw std::runtime_error("Truncated ONNX protobuf varint");}const auto byte=uint8_t(text[offset++]);if(i==9&&byte>1) {throw std::runtime_error("ONNX protobuf varint overflow");}value|=uint64_t(byte&127)<<(i*7);if(!(byte&128)) {return value;}}throw std::runtime_error("Invalid ONNX protobuf varint");
    }
    static std::vector<Field> fields(std::string_view text) {
        std::vector<Field> result;size_t offset=0;while(offset<text.size()) {if(result.size()>1000000) {throw std::runtime_error("ONNX protobuf field bound exceeded");}const auto begin=offset,tag=varint(text,offset);const unsigned number=unsigned(tag>>3),wire=unsigned(tag&7);if(!number) {throw std::runtime_error("Invalid ONNX protobuf field");}size_t first=offset,length=0;
            if(wire==0) {varint(text,offset);length=offset-first;}else if(wire==1||wire==5) {length=wire==1?8:4;if(length>text.size()-offset) {throw std::runtime_error("Truncated ONNX fixed field");}offset+=length;}else if(wire==2) {const auto bytes=varint(text,offset);first=offset;if(bytes>text.size()-offset) {throw std::runtime_error("Truncated ONNX message");}length=size_t(bytes);offset+=length;}else {throw std::runtime_error("Unsupported ONNX protobuf wire type");}result.push_back({number,wire,text.substr(begin,offset-begin),text.substr(first,length)});}return result;
    }
    static void number(std::string& out,uint64_t value) {while(value>=128) {out.push_back(char((value&127)|128));value>>=7;}out.push_back(char(value));}
    static void message(std::string& out,unsigned field,const std::string& text) {number(out,uint64_t(field)*8+2);number(out,text.size());out.append(text);}
    void tensor(std::string_view text) const {
        for(const auto& field:fields(text)) {if(field.number!=13||field.wire!=2) {continue;}std::string key,value;for(const auto& entry:fields(field.value)) {if(entry.number==1&&entry.wire==2) {key=std::string(entry.value);}if(entry.number==2&&entry.wire==2) {value=std::string(entry.value);}}if(key=="location") {authorizedPath(m_directory,value,{m_directory});}}
    }
    std::string transform(std::string_view text,unsigned kind,unsigned depth=0) {
        if(depth>32) {throw std::runtime_error("ONNX graph depth exceeds bound");}const auto source=fields(text);std::string operation,domain;if(kind==2) {for(const auto& field:source) {if(field.number==4&&field.wire==2) {operation=std::string(field.value);}if(field.number==7&&field.wire==2) {domain=std::string(field.value);}}}
        const bool random=kind==2&&(domain.empty()||domain=="ai.onnx")&&(operation=="RandomNormal"||operation=="RandomNormalLike"||operation=="RandomUniform"||operation=="RandomUniformLike"||operation=="Bernoulli");
        std::string out;out.reserve(text.size()+32);for(const auto& field:source) {
            if(kind==2&&field.number==5&&field.wire==2&&random) {bool seed=false;for(const auto& attribute:fields(field.value)) {if(attribute.number==1&&attribute.value=="seed") {seed=true;}}if(seed) {continue;}}
            int child=-1;if(field.wire==2) {if(kind==0&&field.number==7) {child=1;}else if(kind==0&&field.number==25) {child=4;}else if(kind==1&&field.number==1) {child=2;}else if(kind==2&&field.number==5) {child=3;}else if(kind==3&&(field.number==6||field.number==11)) {child=1;}else if(kind==4&&field.number==7) {child=2;}
                if((kind==1&&field.number==5)||(kind==3&&(field.number==5||field.number==10))) {tensor(field.value);}}
            if(child>=0) {message(out,field.number,transform(field.value,unsigned(child),depth+1));}else {out.append(field.raw);}
        }
        if(random) {std::string attribute;message(attribute,1,"seed");number(attribute,2*8+5);const float seed=float((uint64_t(m_seed)+m_nodes*104729)%16777216);uint32_t bits;std::memcpy(&bits,&seed,4);for(unsigned i=0;i<4;++i) {attribute.push_back(char(bits>>(8*i)));}number(attribute,20*8);number(attribute,1);message(out,5,attribute);++m_nodes;}return out;
    }
public:
    ModelSeed(const fs::path& path,uint32_t seed):m_seed(seed),m_directory(fs::canonical(path.parent_path())) {}
    std::string read(const fs::path& path) {const auto size=fs::file_size(path);if(size>1024ull*1024*1024) {throw std::runtime_error("ONNX model size exceeds bound");}std::ifstream file(path,std::ios::binary);std::string source(size_t(size),'\0');if(!file.read(source.data(),std::streamsize(size))) {throw std::runtime_error("Cannot read ONNX model");}auto result=transform(source,0);return m_nodes?result:std::string();}
};
}
#endif
