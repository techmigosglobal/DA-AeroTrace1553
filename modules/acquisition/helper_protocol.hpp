#pragma once
#include "source.hpp"
#include <algorithm>
#include <limits>
#include <span>
#include <stdexcept>

namespace aero::helper {
inline constexpr std::uint16_t ProtocolVersion=1;
inline constexpr std::size_t HeaderBytes=32,MaxFrameBytes=1024*1024,MaxQueuedBytes=32*1024*1024;
inline constexpr unsigned MaxRecords=4096,MaxPackets=4,MaxPacketBytes=65536;
enum class Type : std::uint16_t {Hello=1,Open,Start,Stop,Close,Cancel,Health,Reply,Diagnostic,DeviceState,Data,End,Fatal};
struct Frame {Type type{};std::uint64_t request{},sequence{};std::vector<std::uint8_t> payload;};
inline std::uint32_t checksum(std::span<const std::uint8_t> bytes){std::uint32_t c=~0u;for(auto b:bytes){c^=b;for(unsigned i=0;i<8;++i)c=(c>>1)^(0xedb88320u&(~(c&1)+1));}return ~c;}
struct Writer {
  std::vector<std::uint8_t> bytes;
  template<class T>void number(T value){using U=std::make_unsigned_t<T>;auto u=static_cast<U>(value);for(unsigned i=0;i<sizeof(T);++i){bytes.push_back(std::uint8_t(u));u>>=8;}}
  void text(const std::string &value){if(value.size()>8192)throw std::runtime_error("Helper text exceeds 8192 bytes");number<std::uint32_t>(std::uint32_t(value.size()));bytes.insert(bytes.end(),value.begin(),value.end());}
  void raw(std::span<const std::uint8_t> value){bytes.insert(bytes.end(),value.begin(),value.end());}
};
struct Reader {
  std::span<const std::uint8_t> bytes;std::size_t at{};
  template<class T>T number(){if(sizeof(T)>bytes.size()-at)throw std::runtime_error("Truncated helper field");using U=std::make_unsigned_t<T>;U u{};for(unsigned i=0;i<sizeof(T);++i)u|=U(bytes[at++])<<(i*8);return static_cast<T>(u);}
  std::span<const std::uint8_t> raw(std::size_t n){if(n>bytes.size()-at)throw std::runtime_error("Truncated helper payload");auto out=bytes.subspan(at,n);at+=n;return out;}
  std::string text(){auto n=number<std::uint32_t>();if(n>8192)throw std::runtime_error("Invalid helper text length");auto b=raw(n);return {reinterpret_cast<const char *>(b.data()),b.size()};}
  void done()const{if(at!=bytes.size())throw std::runtime_error("Trailing helper fields");}
};
inline std::vector<std::uint8_t> encode(const Frame &f){
  if(f.payload.size()>MaxFrameBytes-HeaderBytes)throw std::runtime_error("Helper frame exceeds 1 MiB");
  Writer w;w.number<std::uint32_t>(0x50485441);w.number<std::uint16_t>(ProtocolVersion);w.number<std::uint16_t>(static_cast<std::uint16_t>(f.type));w.number(f.request);w.number(f.sequence);w.number<std::uint32_t>(std::uint32_t(f.payload.size()));w.number<std::uint32_t>(0);w.raw(f.payload);
  const auto crc=checksum(w.bytes);for(unsigned i=0;i<4;++i)w.bytes[28+i]=std::uint8_t(crc>>(i*8));return w.bytes;
}
inline std::uint32_t payloadLength(std::span<const std::uint8_t> header){
  if(header.size()!=HeaderBytes)throw std::runtime_error("Invalid helper header size");Reader r{header};if(r.number<std::uint32_t>()!=0x50485441||r.number<std::uint16_t>()!=ProtocolVersion)throw std::runtime_error("Helper protocol magic/version mismatch");auto type=r.number<std::uint16_t>();if(type<1||type>13)throw std::runtime_error("Unknown helper frame type");r.number<std::uint64_t>();r.number<std::uint64_t>();auto n=r.number<std::uint32_t>();if(n>MaxFrameBytes-HeaderBytes)throw std::runtime_error("Helper frame length exceeds bound");return n;
}
inline Frame decode(std::span<const std::uint8_t> input,std::uint64_t expected){
  if(input.size()<HeaderBytes)throw std::runtime_error("Truncated helper frame header");const auto n=payloadLength(input.first(HeaderBytes));if(input.size()!=HeaderBytes+n)throw std::runtime_error("Helper frame length mismatch");
  Reader r{input};r.number<std::uint32_t>();r.number<std::uint16_t>();Frame f;f.type=static_cast<Type>(r.number<std::uint16_t>());f.request=r.number<std::uint64_t>();f.sequence=r.number<std::uint64_t>();if(f.sequence!=expected)throw std::runtime_error("Helper frame sequence gap");r.number<std::uint32_t>();auto crc=r.number<std::uint32_t>();auto copy=std::vector<std::uint8_t>(input.begin(),input.end());std::fill(copy.begin()+28,copy.begin()+32,0);if(checksum(copy)!=crc)throw std::runtime_error("Helper frame CRC mismatch");f.payload.assign(input.begin()+HeaderBytes,input.end());return f;
}
inline void failure(Writer &w,const CaptureFailure &f){w.number<std::uint32_t>(static_cast<unsigned>(f.category));w.number<std::int32_t>(f.code);w.number<std::int32_t>(f.channel);w.number<std::int32_t>(f.logicalDevice);w.number(f.windowsCode);w.number<std::uint8_t>(f.supplementaryWindows);w.number<std::uint8_t>(f.cleanupFailed);w.text(f.stage);w.text(f.operation);w.text(f.detail);if(f.recovery.size()>16)throw std::runtime_error("Too many helper recovery actions");w.number<std::uint32_t>(std::uint32_t(f.recovery.size()));for(const auto &x:f.recovery)w.text(x);}
inline CaptureFailure failure(Reader &r){CaptureFailure f;auto category=r.number<std::uint32_t>();if(category>static_cast<unsigned>(CaptureFailureCategory::Unknown))throw std::runtime_error("Invalid helper failure category");f.category=static_cast<CaptureFailureCategory>(category);f.code=r.number<std::int32_t>();f.channel=r.number<std::int32_t>();f.logicalDevice=r.number<std::int32_t>();f.windowsCode=r.number<std::uint32_t>();f.supplementaryWindows=r.number<std::uint8_t>()!=0;f.cleanupFailed=r.number<std::uint8_t>()!=0;f.stage=r.text();f.operation=r.text();f.detail=r.text();auto n=r.number<std::uint32_t>();if(n>16)throw std::runtime_error("Too many helper recovery actions");for(unsigned i=0;i<n;++i)f.recovery.push_back(r.text());return f;}
inline void metrics(Writer &w,const SourceBatch &b){w.number(b.hardwareLost);w.number(b.hardwareOverflows);w.number(b.pollNs);w.number(b.packetProcessingNs);for(auto v:b.channelLost)w.number(v);for(auto v:b.channelOverflows)w.number(v);}
inline void metrics(Reader &r,SourceBatch &b){b.hardwareLost=r.number<std::uint64_t>();b.hardwareOverflows=r.number<std::uint64_t>();b.pollNs=r.number<std::uint64_t>();b.packetProcessingNs=r.number<std::uint64_t>();for(auto &v:b.channelLost)v=r.number<std::uint64_t>();for(auto &v:b.channelOverflows)v=r.number<std::uint64_t>();}
inline void record(Writer &w,const Record &v){
  if(v.words.size()>32||v.channel<1||v.channel>2||v.bus>1||v.source>2)throw std::runtime_error("Invalid helper record");
  w.number(v.id);w.number(v.timestampNs);w.number(v.deviceTicks);w.number(v.hostMonotonicNs);w.number(v.tickResolutionNs);
  for(auto value:{v.commandWord,v.statusWord,v.flags,v.vendorStatus,v.commandWord2,v.statusWord2,v.validity,v.messageType})w.number(value);
  w.number<std::uint8_t>(v.channel);w.number<std::uint8_t>(v.bus);w.number<std::uint8_t>(v.source);w.number<std::uint8_t>(std::uint8_t(v.words.size()));for(auto value:v.words)w.number(value);
}
inline Record record(Reader &r){Record v;v.id=r.number<std::uint64_t>();v.timestampNs=r.number<std::uint64_t>();v.deviceTicks=r.number<std::uint64_t>();v.hostMonotonicNs=r.number<std::uint64_t>();v.tickResolutionNs=r.number<std::uint32_t>();v.commandWord=r.number<std::uint16_t>();v.statusWord=r.number<std::uint16_t>();v.flags=r.number<std::uint16_t>();v.vendorStatus=r.number<std::uint16_t>();v.commandWord2=r.number<std::uint16_t>();v.statusWord2=r.number<std::uint16_t>();v.validity=r.number<std::uint16_t>();v.messageType=r.number<std::uint16_t>();v.channel=r.number<std::uint8_t>();v.bus=r.number<std::uint8_t>();v.source=r.number<std::uint8_t>();auto n=r.number<std::uint8_t>();if(n>32||v.channel<1||v.channel>2||v.bus>1||v.source>2)throw std::runtime_error("Invalid helper record identity/count");for(unsigned i=0;i<n;++i)v.words.push_back(r.number<std::uint16_t>());return v;}
inline std::vector<std::uint8_t> batch(const SourceBatch &b){
  if(b.records.size()>MaxRecords||b.packets.size()>MaxPackets)throw std::runtime_error("Helper batch exceeds chunk limit");Writer w;metrics(w,b);w.number<std::uint32_t>(std::uint32_t(b.records.size()));w.number<std::uint32_t>(std::uint32_t(b.packets.size()));for(const auto &v:b.records)record(w,v);for(const auto &p:b.packets){if(p.channel<1||p.channel>2||p.bytes.size()>MaxPacketBytes)throw std::runtime_error("Invalid helper raw packet");w.number<std::uint32_t>(p.channel);w.number<std::uint8_t>(p.chapter10);w.number<std::uint32_t>(std::uint32_t(p.bytes.size()));w.raw(p.bytes);}if(w.bytes.size()>MaxFrameBytes-HeaderBytes)throw std::runtime_error("Helper chunk exceeds frame limit");return std::move(w.bytes);
}
inline SourceBatch batch(std::span<const std::uint8_t> bytes){Reader r{bytes};SourceBatch b;metrics(r,b);const auto nr=r.number<std::uint32_t>(),np=r.number<std::uint32_t>();if(nr>MaxRecords||np>MaxPackets)throw std::runtime_error("Invalid helper chunk counts");b.records.reserve(nr);for(unsigned i=0;i<nr;++i)b.records.push_back(record(r));for(unsigned i=0;i<np;++i){SourceBatch::Packet p;p.channel=r.number<std::uint32_t>();p.chapter10=r.number<std::uint8_t>()!=0;auto n=r.number<std::uint32_t>();if(p.channel<1||p.channel>2||n>MaxPacketBytes)throw std::runtime_error("Invalid helper packet length/channel");auto raw=r.raw(n);p.bytes.assign(raw.begin(),raw.end());b.packets.push_back(std::move(p));}r.done();return b;}
template<class Consumer>void chunks(const SourceBatch &b,Consumer consume){
  std::size_t ri=0,pi=0;do{SourceBatch part;part.hardwareLost=b.hardwareLost;part.hardwareOverflows=b.hardwareOverflows;part.channelLost=b.channelLost;part.channelOverflows=b.channelOverflows;part.pollNs=b.pollNs;part.packetProcessingNs=b.packetProcessingNs;
    auto re=std::min(ri+MaxRecords,b.records.size()),pe=std::min(pi+MaxPackets,b.packets.size());part.records.insert(part.records.end(),b.records.begin()+ri,b.records.begin()+re);part.packets.insert(part.packets.end(),b.packets.begin()+pi,b.packets.begin()+pe);consume(std::move(part));ri=re;pi=pe;
  }while(ri<b.records.size()||pi<b.packets.size());
}
}
