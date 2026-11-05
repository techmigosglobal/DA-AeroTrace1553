#include "ddc_runtime.hpp"
#include <chrono>
#include <iostream>
#include <thread>
#define CHECK(x) do{if(!(x))throw std::runtime_error(#x);}while(false)
int main(){
  const auto root=std::filesystem::temp_directory_path()/("aerotrace-pack-contract-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  try{
    std::filesystem::create_directories(root/"runtime");
    const std::string digest(64,'a');
    const std::string valid="{\"schema\":\"aerotrace.provider-pack.v1\",\"id\":\"synthetic-contract\",\"architecture\":\"x64\",\"sdk\":{\"identity\":\"fixture\",\"header_sha256\":\""+digest+"\"},\"provider\":{\"path\":\"provider.dll\",\"abi\":1,\"sha256\":\""+digest+"\"},\"runtime\":{\"path\":\"runtime/emacepl.dll\",\"sha256\":\""+digest+"\",\"file_version\":\"fixture\"},\"receipts\":{\"compiled\":true,\"contract_tested\":true,\"runtime_inspected\":true},\"compatibility\":{\"matrix\":[]}}";
    auto parse=[&](const std::string &input){const auto file=root/"provider-pack.json";{std::ofstream output(file,std::ios::binary);output<<input;}return aero::readDdcProviderPack(file);};
    const auto pack=parse(valid);CHECK(pack.error.empty()&&pack.receiptsComplete&&pack.matrix.empty());
    CHECK(parse(std::string("\xef\xbb\xbf")+valid).error.empty());
    CHECK(!aero::ddcPackAutomaticApproved(pack,26100,{}));
    auto altered=[&](const std::string &old,const std::string &replacement){auto s=valid;const auto at=s.find(old);CHECK(at!=std::string::npos);s.replace(at,old.size(),replacement);return s;};
    CHECK(!parse(altered("\"path\":\"provider.dll\"","\"path\":\"../provider.dll\"")).error.empty());
    CHECK(!parse(altered("\"abi\":1","\"abi\":2")).error.empty());
    CHECK(!parse(altered("\"architecture\":\"x64\"","\"architecture\":\"x86\"")).error.empty());
    CHECK(!parse(altered("\"compiled\":true","\"compiled\":false")).error.empty());
    CHECK(!parse(altered("\"abi\":1","\"abi\":1,\"abi\":1")).error.empty());
    CHECK(!parse(valid+"junk").error.empty());
    CHECK(!parse(altered("\"id\":\"synthetic-contract\"","\"id\":\"\\ud800\"")).error.empty());
    CHECK(!parse(altered("\"id\":\"synthetic-contract\"",std::string("\"id\":\"")+char(0xc0)+char(0x80)+"\"")).error.empty());
    CHECK(!parse(altered(digest,"bad-hash")).error.empty());
    // Synthetic inputs exercise admission only. No physical receipt is issued.
    const std::string row="{\"windows_min_build\":26000,\"windows_max_build\":26100,\"driver_version\":\"1.2.3.4\",\"hardware_id\":\"USB/VID_TEST/PID_TEST\",\"receipt\":\"synthetic-contract-only\",\"physical_qualified\":true}";
    auto matrix=parse(altered("\"matrix\":[]","\"matrix\":["+row+"]"));CHECK(matrix.error.empty());
    const std::vector<aero::DdcDriverIdentity> matching{{"1.2.3.4",{"usb/vid_test/pid_test"}}};
    CHECK(aero::ddcPackAutomaticApproved(matrix,26100,matching));
    CHECK(!aero::ddcPackAutomaticApproved(matrix,26101,matching));
    CHECK(!aero::ddcPackAutomaticApproved(matrix,25999,matching));
    CHECK(!aero::ddcPackAutomaticApproved(matrix,26100,{{"1.2.3.5",{"USB/VID_TEST/PID_TEST"}}}));
    CHECK(!aero::ddcPackAutomaticApproved(matrix,26100,{{"1.2.3.4",{"USB/VID_TEST/PID_OTHER"}}}));
    CHECK(!aero::ddcPackAutomaticApproved(matrix,26100,{matching.front(),{"9.9.9.9",{"other"}}}));
    CHECK(!aero::ddcPackAutomaticApproved(matrix,0,matching));
    {
      aero::DdcRuntimeProbe one,two;CHECK(one.admitted()&&two.admitted());CHECK(!one.promoteToConnection());
    }
    {
      aero::DdcRuntimeProbe one;CHECK(one.promoteToConnection());CHECK(aero::ddcProviderConnectionActive());
      bool admitted=true;std::thread reader([&]{aero::DdcRuntimeProbe probe;admitted=probe.admitted();});reader.join();CHECK(!admitted);
      aero::releaseDdcProviderConnection();
    }
    CHECK(!aero::ddcProviderConnectionActive());
    {aero::DdcRuntimeProbe one;CHECK(one.admitted());}
    std::filesystem::remove_all(root);
    std::cout<<"PASS: bounded provider-pack parsing, finite matrix admission, retained connection/probe exclusion\n";
  }catch(const std::exception &e){aero::releaseDdcProviderConnection();std::cerr<<e.what()<<"\nFixtures: "<<root<<'\n';return 1;}
}
