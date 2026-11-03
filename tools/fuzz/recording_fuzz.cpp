#include "core.hpp"
#include <sstream>
#include <cstdlib>
extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t *data,std::size_t size){
  if(size>65536)return 0;std::istringstream input(std::string(reinterpret_cast<const char *>(data),size));std::uint64_t visited=0;
  const auto result=aero::scanRecordingStream(input,[&](const aero::Record &r,std::uint64_t offset){if(r.words.size()>32||r.channel<1||r.channel>2||r.bus>1||offset>=size)std::abort();++visited;return true;});
  if(result.count!=visited)std::abort();
  // Repair CRCs in a second pass so payload mutations reach the production decoder.
  std::vector<std::uint8_t> fixed(data,data+size);for(std::size_t at=8;at+8<=size;){const auto len=std::uint32_t(fixed[at])|(std::uint32_t(fixed[at+1])<<8)|(std::uint32_t(fixed[at+2])<<16)|(std::uint32_t(fixed[at+3])<<24);if(len>121||len<26||len>size-at-8)break;const std::vector<std::uint8_t> body(fixed.begin()+at+8,fixed.begin()+at+8+len);const auto crc=aero::crc32(body);for(unsigned k=0;k<4;++k)fixed[at+4+k]=std::uint8_t(crc>>(8*k));at+=8+len;}
  std::istringstream repaired(std::string(fixed.begin(),fixed.end()));(void)aero::scanRecordingStream(repaired,[](const auto &,auto){return true;});return 0;
}
