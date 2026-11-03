#include "packet_bounds.hpp"
extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t *data,std::size_t size){(void)aero::validChapter10Envelope({data,size});if(size>=16){auto u32=[&](unsigned p){return std::uint32_t(data[p])|(std::uint32_t(data[p+1])<<8)|(std::uint32_t(data[p+2])<<16)|(std::uint32_t(data[p+3])<<24);};(void)aero::validDecodedMessageBounds(u32(0),u32(4),u32(8),u32(12));}return 0;}
