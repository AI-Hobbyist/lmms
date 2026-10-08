/* A0 read-only fixture inventory. Does not run inference or modify voice packages. */
#include <onnxruntime_cxx_api.h>
#include <nlohmann/json.hpp>
#include <yaml-cpp/yaml.h>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <set>
#include <string>
#include <vector>
namespace fs = std::filesystem;
using Json = nlohmann::ordered_json;

namespace {
Json yaml(const YAML::Node& node, unsigned depth = 0)
{
    if (depth > 32) { throw std::runtime_error("YAML nesting/alias limit exceeded"); }
    if (node.IsMap()) {
        auto value = Json::object();
        for (const auto& item : node) { value[item.first.as<std::string>()] = yaml(item.second, depth + 1); }
        return value;
    }
    if (node.IsSequence()) {
        auto value = Json::array();
        for (const auto& item : node) { value.push_back(yaml(item, depth + 1)); }
        return value;
    }
    return node.IsNull() ? Json(nullptr) : Json(node.as<std::string>());
}
YAML::Node config(const fs::path& path)
{
    if (fs::file_size(path) > 4 * 1024 * 1024) { throw std::runtime_error("Configuration exceeds 4 MiB"); }
    std::ifstream input(path, std::ios::binary);
    if (!input) { throw std::runtime_error("Cannot read " + path.u8string()); }
    auto node = YAML::Load(input);
    if (!node.IsMap()) { throw std::runtime_error("Configuration must be a mapping: " + path.u8string()); }
    return node;
}
Json tensors(Ort::Session& session, bool input)
{
    auto result = Json::array();
    Ort::AllocatorWithDefaultOptions allocator;
    const auto count = input ? session.GetInputCount() : session.GetOutputCount();
    for (size_t i = 0; i < count; ++i) {
        auto name = input ? session.GetInputNameAllocated(i, allocator) : session.GetOutputNameAllocated(i, allocator);
        auto type = input ? session.GetInputTypeInfo(i) : session.GetOutputTypeInfo(i);
        if (type.GetONNXType() != ONNX_TYPE_TENSOR) { throw std::runtime_error("Non-tensor model interface: " + std::string(name.get())); }
        const auto info = type.GetTensorTypeAndShapeInfo();
        const auto shape = info.GetShape();
        std::vector<const char*> symbols(shape.size());
        info.GetSymbolicDimensions(symbols.data(), symbols.size());
        auto dimensions = Json::array();
        for (size_t dim = 0; dim < shape.size(); ++dim) {
            dimensions.push_back({{"value", shape[dim]}, {"symbol", symbols[dim] ? symbols[dim] : ""}});
        }
        result.push_back({{"name", name.get()}, {"dtype", int(info.GetElementType())}, {"rank", shape.size()}, {"dimensions", dimensions}});
    }
    return result;
}
int run(const fs::path& root, const fs::path& output, unsigned expected)
{
    if (!fs::is_directory(root)) { throw std::runtime_error("Missing fixture root: " + root.u8string()); }
    Ort::Env environment(ORT_LOGGING_LEVEL_ERROR, "DiffSingerModelProbe");
    Ort::SessionOptions options;
    options.SetExecutionMode(ExecutionMode::ORT_SEQUENTIAL);
    options.SetIntraOpNumThreads(2);
    options.SetInterOpNumThreads(1);
    options.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_DISABLE_ALL);
    std::set<fs::path> packages;
    Json diagnostics = Json::array();
    unsigned failures = 0;
    for (const auto& item : fs::recursive_directory_iterator(root, fs::directory_options::skip_permission_denied)) {
        if (!item.is_regular_file() || item.path().filename() != "dsconfig.yaml") { continue; }
        try {
            const auto node = config(item.path());
            if (node["acoustic"] || node["fs2"]) { packages.insert(fs::canonical(item.path().parent_path())); }
        } catch (const std::exception& error) {
            diagnostics.push_back({{"file", fs::relative(item.path(), root).generic_u8string()}, {"error", error.what()}});
            ++failures;
        }
    }
    auto voices = Json::array();
    unsigned models = 0;
    for (const auto& package : packages) {
        const auto relative = fs::relative(package, root).generic_u8string();
        Json voice{{"root", relative}, {"configs", Json::object()}, {"models", Json::array()}};
        for (const auto& role : std::vector<std::pair<std::string, fs::path>>{
            {"acoustic", "dsconfig.yaml"}, {"duration", "dsdur/dsconfig.yaml"},
            {"pitch", "dspitch/dsconfig.yaml"}, {"variance", "dsvariance/dsconfig.yaml"},
            {"vocoder", "dsvocoder/vocoder.yaml"}, {"character", "character.yaml"}}) {
            const auto path = package / role.second;
            if (!fs::exists(path)) { continue; }
            try {
                const auto node = config(path);
                voice["configs"][role.first] = yaml(node);
                if (node["languages"] && node["languages"].IsScalar()) {
                    std::ifstream languageFile(path.parent_path() / fs::u8path(node["languages"].as<std::string>()));
                    if (!languageFile) { throw std::runtime_error("Missing language ID mapping"); }
                    voice["configs"][role.first]["resolvedLanguageIds"] = Json::parse(languageFile);
                }
                if (node["phonemes"] && node["phonemes"].IsScalar()) {
                    std::ifstream phonemeFile(path.parent_path() / fs::u8path(node["phonemes"].as<std::string>()));
                    if (!phonemeFile) { throw std::runtime_error("Missing phoneme ID mapping"); }
                    const auto inventory = Json::parse(phonemeFile);
                    if (!inventory.is_object()) { throw std::runtime_error("Invalid phoneme ID mapping"); }
                    voice["configs"][role.first]["phonemeCount"] = inventory.size();
                }
                for (const auto* key : {"acoustic", "linguistic", "dur", "pitch", "variance", "fs2", "aux", "denoiser", "model"}) {
                    if (!node[key] || !node[key].IsScalar()) { continue; }
                    const auto modelPath = fs::canonical(path.parent_path() / fs::u8path(node[key].as<std::string>()));
                    const auto contained = modelPath.lexically_relative(package);
                    if (contained.empty() || *contained.begin() == "..") { throw std::runtime_error("Model escapes fixture package"); }
                    std::cout << "PROBE " << relative << ' ' << role.first << '/' << key << ' ' << contained.generic_u8string() << std::endl;
                    Ort::Session session(environment, modelPath.c_str(), options);
                    voice["models"].push_back({{"stage", role.first}, {"role", key}, {"file", contained.generic_u8string()},
                        {"bytes", fs::file_size(modelPath)}, {"inputs", tensors(session, true)}, {"outputs", tensors(session, false)},
                        {"cpuSession", "PASS"}});
                    ++models;
                }
            } catch (const std::exception& error) {
                diagnostics.push_back({{"voice", relative}, {"stage", role.first}, {"error", error.what()}});
                std::cerr << "FAIL " << relative << ' ' << role.first << ": " << error.what() << std::endl;
                ++failures;
            }
        }
        voices.push_back(std::move(voice));
    }
    if (packages.size() != expected) { diagnostics.push_back({{"error", "Unexpected acoustic package count"}, {"expected", expected}, {"actual", packages.size()}}); ++failures; }
    const Json report{{"schemaVersion", 1}, {"runtime", OrtGetApiBase()->GetVersionString()}, {"provider", "CPUExecutionProvider"},
        {"purpose", "Signature/session validation only; no inference claim"}, {"voices", voices}, {"diagnostics", diagnostics}};
    std::ofstream file(output, std::ios::binary);
    if (!file) { throw std::runtime_error("Cannot write matrix"); }
    file << report.dump(2) << '\n';
    if (!file) { throw std::runtime_error("Failed to write matrix"); }
    std::cout << (failures ? "FAIL" : "PASS") << " packages=" << packages.size() << " models=" << models << " errors=" << failures << std::endl;
    return failures ? 1 : 0;
}
}
#ifdef _WIN32
int wmain(int argc, wchar_t** argv)
#else
int main(int argc, char** argv)
#endif
{
    if (argc != 4) { std::cerr << "Usage: DiffSingerModelProbe <fixture-root> <matrix.json> <expected-packages>\n"; return 2; }
    try { return run(fs::path(argv[1]), fs::path(argv[2]), unsigned(std::stoul(argv[3]))); }
    catch (const std::exception& error) { std::cerr << "FAIL: " << error.what() << '\n'; return 1; }
}
