#include "packet_bounds.hpp"
extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t *data,std::size_t size){
  // These validators are pure inline functions. Keep their results observable
  // so an optimized fuzz build cannot discard the validation under test.
  static volatile bool envelopeResult,messageResult;
  envelopeResult=aero::validChapter10Envelope({data,size});
  if(size>=16){auto u32=[&](unsigned p){return std::uint32_t(data[p])|(std::uint32_t(data[p+1])<<8)|(std::uint32_t(data[p+2])<<16)|(std::uint32_t(data[p+3])<<24);};messageResult=aero::validDecodedMessageBounds(u32(0),u32(4),u32(8),u32(12));}
  return 0;
}
