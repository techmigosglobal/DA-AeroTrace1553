#include "core.hpp"
#include <chrono>
#include <cstdlib>
#include <sstream>
#include <windows.h>

namespace {
struct Sandbox {
  std::filesystem::path root=std::filesystem::temp_directory_path()/
    ("aerotrace-index-fuzz-"+std::to_string(GetCurrentProcessId())+"-"+
     std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  std::filesystem::path file=root/"input.atd";
  Sandbox(){if(!std::filesystem::create_directory(root))std::abort();}
  ~Sandbox(){std::error_code error;std::filesystem::remove(file,error);std::filesystem::remove(root,error);}
};
}
extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t *data,std::size_t size) {
  if(size>65536)return 0;
  static Sandbox sandbox;
  {std::ofstream output(sandbox.file,std::ios::binary|std::ios::trunc);
    output.write(reinterpret_cast<const char *>(data),std::streamsize(size));output.close();
    if(!output)std::abort();}
  // Exercise the actual filesystem index and random-access reader, including
  // offsets, truncated tails, CRC validation and nondecreasing playback times.
  aero::RecordingIndex index;std::vector<aero::Record> records;
  const auto result=index.open(sandbox.file,[&](const auto &record){records.push_back(record);});
  if(result.count!=records.size())std::abort();
  if(!result.error.empty()){if(index.size()!=0)std::abort();return 0;}
  if(index.size()!=records.size())std::abort();
  std::uint64_t previous=0;
  for(std::size_t n=0;n<records.size();++n){
    const auto playback=index.playbackTimestamp(n);
    if(playback<records[n].timestampNs||(n&&playback<previous))std::abort();
    previous=playback;
  }
  if(!records.empty())for(const auto n:{std::size_t(0),records.size()/2,records.size()-1})
    if(index.at(n)!=records[n])std::abort();
  return 0;
}
