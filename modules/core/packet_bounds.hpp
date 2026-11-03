#pragma once
#include <span>
#include <cstdint>
namespace aero {
inline bool validDecodedMessageBounds(std::uint64_t raw,std::uint64_t next,std::uint64_t end,unsigned words){return words<=32&&raw>=28&&raw<=end&&end-raw>=14&&next>raw&&next<=end;}
inline bool validChapter10Envelope(std::span<const std::uint8_t> bytes){
  if(bytes.size()<28||bytes[0]!=0x25||bytes[1]!=0xeb)return false;
  auto u32=[&](unsigned p){return std::uint32_t(bytes[p])|(std::uint32_t(bytes[p+1])<<8)|(std::uint32_t(bytes[p+2])<<16)|(std::uint32_t(bytes[p+3])<<24);};
  const auto size=u32(4),data=u32(8);return size>=28&&size<=65536&&size<=bytes.size()&&data>=4&&data<=size-24;
}
}
