#include "VoiceCatalog.h"
#include "Hash.h"
#include <fstream>
#include <iostream>
#include <set>
#include <random>
#include <stdexcept>
#ifdef _WIN32
#include <windows.h>
#include <winioctl.h>
#include <aclapi.h>
#endif
using namespace diffsinger;
namespace {
void require(bool value,const char* message) {if(!value) {throw std::runtime_error(message);}}
void write(const fs::path& file,const std::string& text) {fs::create_directories(file.parent_path());std::ofstream output(file,std::ios::binary);output<<text;require(bool(output),"Fixture write failed");}
#ifdef _WIN32
struct DeniedDirectory {
    fs::path path;PSECURITY_DESCRIPTOR original=nullptr;PACL previous=nullptr,denied=nullptr;
    explicit DeniedDirectory(const fs::path& directory):path(directory) {
        require(GetNamedSecurityInfoW(const_cast<wchar_t*>(path.c_str()),SE_FILE_OBJECT,DACL_SECURITY_INFORMATION,nullptr,nullptr,&previous,nullptr,&original)==ERROR_SUCCESS,"Cannot preserve fixture ACL");
        BYTE sid[SECURITY_MAX_SID_SIZE];DWORD size=sizeof(sid);require(CreateWellKnownSid(WinWorldSid,nullptr,sid,&size)!=0,"Cannot create fixture SID");
        EXPLICIT_ACCESSW entry{};entry.grfAccessPermissions=FILE_LIST_DIRECTORY;entry.grfAccessMode=DENY_ACCESS;entry.grfInheritance=NO_INHERITANCE;entry.Trustee.TrusteeForm=TRUSTEE_IS_SID;entry.Trustee.TrusteeType=TRUSTEE_IS_WELL_KNOWN_GROUP;entry.Trustee.ptstrName=reinterpret_cast<wchar_t*>(sid);
        require(SetEntriesInAclW(1,&entry,previous,&denied)==ERROR_SUCCESS,"Cannot construct fixture ACL");
        require(SetNamedSecurityInfoW(const_cast<wchar_t*>(path.c_str()),SE_FILE_OBJECT,DACL_SECURITY_INFORMATION,nullptr,nullptr,denied,nullptr)==ERROR_SUCCESS,"Cannot deny fixture listing");
    }
    ~DeniedDirectory() {SetNamedSecurityInfoW(const_cast<wchar_t*>(path.c_str()),SE_FILE_OBJECT,DACL_SECURITY_INFORMATION,nullptr,nullptr,previous,nullptr);LocalFree(denied);LocalFree(original);}
};
void junction(const fs::path& link,const fs::path& target) {
    require(CreateDirectoryW(link.c_str(),nullptr)!=0,"Cannot create junction fixture directory");
    const auto handle=CreateFileW(link.c_str(),GENERIC_WRITE,0,nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT|FILE_FLAG_BACKUP_SEMANTICS,nullptr);
    require(handle!=INVALID_HANDLE_VALUE,"Cannot open junction fixture");
    struct Buffer {DWORD tag;WORD length,reserved,subOffset,subLength,printOffset,printLength;wchar_t text[4096];} buffer{};
    const auto sub=L"\\??\\"+target.wstring(),print=target.wstring();require(sub.size()+print.size()+2<4096,"Junction fixture path too long");
    buffer.tag=IO_REPARSE_TAG_MOUNT_POINT;buffer.subLength=WORD(sub.size()*2);buffer.printOffset=WORD((sub.size()+1)*2);buffer.printLength=WORD(print.size()*2);buffer.length=WORD(8+(sub.size()+print.size()+2)*2);
    std::memcpy(buffer.text,sub.c_str(),(sub.size()+1)*2);std::memcpy(reinterpret_cast<char*>(buffer.text)+buffer.printOffset,print.c_str(),(print.size()+1)*2);
    DWORD written=0;const bool ok=DeviceIoControl(handle,FSCTL_SET_REPARSE_POINT,&buffer,8+buffer.length,nullptr,0,&written,nullptr)!=0;CloseHandle(handle);require(ok,"Cannot install native junction fixture");
}
#endif
Json context(const std::vector<fs::path>& roots,const Json& installations=Json::array()) {auto list=Json::array();for(const auto& path:roots) {list.push_back(path.u8string());}return {{"engineSettings",{{"diffsinger.voicebankDirectories",list},{"diffsinger.renderSteps",20}}},{"installations",installations},{"rescan",true}};}
void fixture(const fs::path& root,const std::string& id={}) {
    Json config{{"acoustic","acoustic.onnx"},{"phonemes","phonemes.json"},{"languages","languages.json"},{"sample_rate",44100},{"hop_size",512},{"num_mel_bins",128},{"fft_size",2048},{"win_size",2048},{"mel_fmin",40},{"mel_fmax",16000},{"mel_base","e"},{"mel_scale","slaney"}};
    if(!id.empty()) {config["id"]=id;}
    write(root/"dsconfig.json",config.dump());write(root/"acoustic.onnx","A1 parser fixture only - not an ONNX inference fixture");
    write(root/"phonemes.json",R"({"SP":0,"zh/a":1})");write(root/"languages.json",R"({"zh":4,"en":1})");
    config.erase("acoustic");config.erase("phonemes");config.erase("languages");config["model"]="vocoder.onnx";
    write(root/"dsvocoder/vocoder.json",config.dump());write(root/"dsvocoder/vocoder.onnx","vocoder fixture");
    write(root/"character.yaml","portrait: character.png\nportrait_opacity: 0.67\ndefault_phonemizer: OpenUtau.Core.DiffSinger.DiffSingerChinesePhonemizer\n");
    write(root/"character.txt","\xef\xbb\xbf" "name=测试角色\nimage=avatar.png\nauthor=作者=协作者\n");
    write(root/"avatar.png","fixture-image-bytes");write(root/"character.png","fixture-portrait-bytes");
    write(root/"dsdur/dsconfig.yaml","linguistic: linguistic.onnx\ndur: duration.onnx\n");write(root/"dsdur/linguistic.onnx","linguistic fixture");write(root/"dsdur/duration.onnx","duration fixture");
}
int run(const fs::path& realRoot,const fs::path& fixtureParent) {
    require(hashText("")=="e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855","SHA256 empty mismatch");
    require(hashText("abc")=="ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad","SHA256 abc mismatch");
    const auto parent=fs::canonical(fixtureParent);const auto temporary=parent/fs::u8path("A1-catalog-fixture-"+hashText(std::to_string(std::random_device{}())).substr(0,12));
    require(!fs::exists(temporary),"Fixture already exists");fs::create_directory(temporary);
    struct Cleanup {fs::path path;~Cleanup() {std::error_code error;fs::remove_all(path,error);}} cleanup{temporary};
    const auto voiceRoot=temporary/fs::u8path("嵌套/深层/声库");fixture(voiceRoot);
    auto first=scan(context({temporary,voiceRoot}),1);require(first->voices.size()==1,"Overlapping roots/predictor/vocoder exclusion failed");
    const auto voice=first->voices[0];require(voice->metadata["name"]=="测试角色"&&voice->metadata["author"]=="作者=协作者","YAML+TXT fallback/equal sign/Unicode failed");
    require(voice->metadata["opacity"]==0.67&&voice->resources.size()==2,"Portrait opacity/resources lost");require(voice->declaration()["languages"]==Json::array({"zh","en"}),"Voice capability languages lost/reordered");
    const auto id=voice->id;auto second=scan(context({temporary},first->installations),2);require(second->voices.size()==1&&second->voices[0]->id==id,"Rescan identity changed");
    const auto copy=temporary/"copy";fs::copy(voiceRoot,copy,fs::copy_options::recursive);auto duplicate=scan(context({voiceRoot,copy},first->installations),3);require(duplicate->voices.size()==1,"Identical copy not deduplicated");
    const auto moved=temporary/"moved";fs::rename(voiceRoot,moved);auto relocation=scan(context({moved},first->installations),4);require(relocation->voices.size()==1&&relocation->voices[0]->id==id,"Moved package did not retain ID");
    const auto oldResource=voice->resources.at("avatar");write(moved/"avatar.png","new-avatar-bytes");auto images=scan(context({moved},relocation->installations),5);require(images->voices[0]->fingerprint==voice->fingerprint,"Image change invalidated model fingerprint");require(oldResource->bytes.size()==19&&images->voices[0]->resources.at("avatar")->sha256!=oldResource->sha256,"Old resource snapshot not retained");
    write(moved/"character.json",R"({"name":"JSON优先","image":"avatar.png","customField":17})");auto json=scan(context({moved},images->installations),6);require(json->voices[0]->metadata["name"]=="JSON优先"&&json->voices[0]->metadata["opacity"]==0.67&&json->voices[0]->unknown["character.json"]["customField"]==17,"JSON priority/unknown/source merge failed");
    const auto one=temporary/"priority1",two=temporary/"priority2";fixture(one,"explicit-id");fixture(two,"explicit-id");write(two/"acoustic.onnx","different model content");auto conflict=scan(context({one,two}),7);require(conflict->voices.size()==1&&conflict->voices[0]->root==fs::canonical(one)&&!conflict->diagnostics.empty(),"Conflicting same ID/root priority failed");
    const auto before=conflict->voices[0]->fingerprint;write(one/"acoustic.onnx","updated content");auto changed=scan(context({one},conflict->installations),8);require(changed->voices[0]->id=="explicit-id"&&changed->voices[0]->fingerprint!=before,"Model update identity/version failed");
    require(!changed->voices[0]->metadata.contains("pitchRanges"),"Missing comfort file declared ranges");
    write(one/"comfort.json",R"({"available":"C2-C6","comfort":"C3-C5","weak":["F#3"]})");auto comfortable=scan(context({one},changed->installations),8);require(comfortable->voices.size()==1&&comfortable->voices[0]->metadata["pitchRanges"]["comfort"]=="C3-C5","Optional comfort file was not exposed as generic metadata");
    write(one/"comfort.json","[");auto invalidComfort=scan(context({one},comfortable->installations),8);require(invalidComfort->voices.size()==1&&!invalidComfort->voices[0]->metadata.contains("pitchRanges")&&!invalidComfort->diagnostics.empty(),"Malformed optional comfort file blocked voice discovery");fs::remove(one/"comfort.json");
    const auto broken=temporary/"broken";write(broken/"dsconfig.yaml","acoustic: [\n");fixture(broken/"child","nested-id");auto bad=scan(context({broken,one,temporary/"missing"}),9);require(bad->voices.size()==2&&bad->diagnostics.size()>=2,"Broken/missing root isolation or nested traversal failed");
    write(temporary/"alias.yaml","value: &a [1, 2]\ncopy: *a\n");bool rejected=false;try {readConfiguration(temporary/"alias.yaml");}catch(...) {rejected=true;}require(rejected,"YAML alias was accepted");
    auto invalid=context({one});invalid["engineSettings"]["diffsinger.voicebankDirectories"]="single-path";rejected=false;try {scan(invalid,10);}catch(...) {rejected=true;}require(rejected,"Single path accepted as multiple directories");
    const auto empty=scan(context({}),11);require(empty->voices.empty()&&empty->installations.empty(),"Empty catalog not supported");
#ifdef _WIN32
    const auto locked=temporary/"permission-denied";fs::create_directory(locked);
    {DeniedDirectory denied(locked);auto permissions=scan(context({locked,one}),12);require(permissions->voices.size()==1&&!permissions->diagnostics.empty(),"Permission failure did not preserve valid sibling root/diagnostic");}
    const auto loop=temporary/"loop";
    junction(loop,temporary);
#else
    fs::create_directory_symlink(temporary,temporary/"loop");
#endif
    auto loops=scan(context({temporary}),12);require(!loops->voices.empty(),"Cycle scan failed");
#ifdef _WIN32
    require(RemoveDirectoryW(loop.c_str())!=0,"Cannot clean up junction fixture");
#endif
    std::cout<<"PASS native fixtures: SHA256 / recursive / Unicode / metadata / JSON / IDs / move / conflict / resources / permissions / cycle / bad configuration\n";
    auto actual=scan(context({realRoot}),13);require(actual->voices.size()==6,"Six actual package scan failed");
    std::set<std::string> names;for(const auto& bank:actual->voices) {names.insert(bank->metadata.at("name").get<std::string>());require(bank->stages.size()==5&&bank->resources.size()==2&&bank->metadata["opacity"]==0.67,"Actual stages/images/opacity missing");require(bank->stages.at("acoustic").languages.size()==4,"Actual optional languages missing");std::cout<<"PASS package="<<bank->root.filename().u8string()<<" id="<<bank->id<<" fingerprint="<<bank->fingerprint<<" models/stages/resources/languages\n";}
    require(names.size()==6,"Actual display names lost");auto rescan=scan(context({realRoot,realRoot/"fu2_ning2_na4-DiffSinger"},actual->installations),14);require(rescan->voices.size()==6,"Actual overlap scan not deduplicated");for(const auto& bank:actual->voices) {require(bool(rescan->find(bank->id)),"Actual stable install ID lost");}
    std::cout<<"PASS six-package native catalog, stable rescan, predictors/vocoders=0 false positives\n";return 0;
}
}
#ifdef _WIN32
int wmain(int argc,wchar_t** argv)
#else
int main(int argc,char** argv)
#endif
{if(argc!=3) {std::cerr<<"Usage: DiffSingerCatalogTest <actual voice root> <existing build directory>\n";return 2;}try {return run(fs::path(argv[1]),fs::path(argv[2]));}catch(const std::exception& error) {std::cerr<<"FAIL: "<<error.what()<<'\n';return 1;}}
