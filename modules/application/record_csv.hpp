#pragma once
#include "core.hpp"
#include "protocol.hpp"
#include "capture_time.hpp"
#include <sstream>
#include <iomanip>
#include <locale>
#include <charconv>
namespace aero {
inline std::string csvHex(std::uint16_t value) {
  constexpr char digits[]="0123456789ABCDEF";
  const char text[]{digits[(value>>12)&15],digits[(value>>8)&15],digits[(value>>4)&15],digits[value&15]};
  return {text,4};
}
inline std::string rawCsvHeader() {
  std::string s="record,timestamp_ns,channel,bus,command,status,flags,words,source,device_ticks,tick_resolution_ns,vendor_status,command2,status2,validity,host_monotonic_ns,time,rt,sa,direction,word_count,quality,command_hex,status_hex,vendor_status_hex,command2_hex,status2_hex";
  for(unsigned i=0;i<32;++i)s+=",word_"+std::to_string(i)+"_hex";
  return s+",session_id,transaction,transmitter_rt,receiver_rt,mode_code,mode_name,expected_payload_words,actual_payload_words,payload_eligible,protocol_reason,status_meanings,command_present,command2_present,status1_present,status2_present,sdk_message_type,timing_verdict,interpretation_evidence\n";
}
// Locale-independent append buffer avoids per-cell stream construction on large captures.
struct CsvBuffer {
  std::string text;
  CsvBuffer(){text.reserve(768);}
  CsvBuffer &operator<<(char c){text+=c;return *this;}
  CsvBuffer &operator<<(std::string_view s){text+=s;return *this;}
  template<class T> requires std::is_integral_v<T>
  CsvBuffer &operator<<(T n){char bytes[32];const auto end=std::to_chars(bytes,bytes+sizeof(bytes),std::uint64_t(n)).ptr;text.append(bytes,end);return *this;}
};
inline std::string rawCsvRecord(const Record &r,bool backwards=false,const std::string &session={}) {
  CsvBuffer s;const auto p=interpret(r);const auto c=p.payloadCommand;
  s<<r.id<<','<<r.timestampNs<<','<<unsigned(r.channel)<<','<<(r.bus?'B':'A')<<','<<csvHex(r.commandWord)<<','<<csvHex(r.statusWord)<<','<<r.flags<<',';
  for(unsigned i=0;i<r.words.size();++i){if(i)s<<' ';s<<csvHex(r.words[i]);}
  s<<','<<(r.source==2?"DDC_MT":r.source?"DDC_MTI":"SIMULATED")<<','<<r.deviceTicks<<','<<r.tickResolutionNs<<','<<r.vendorStatus<<','<<r.commandWord2<<','<<r.statusWord2<<','<<r.validity<<','<<r.hostMonotonicNs
   <<','<<formatCardTime(r.timestampNs)<<','<<c.rt<<','<<c.sa<<','<<p.direction<<',';
  if(!p.mode&&p.supported&&p.commandPresent)s<<c.count;
  s<<','<<protocolQuality(r,p,backwards)
   <<",0x"<<csvHex(r.commandWord)<<",0x"<<csvHex(r.statusWord)<<",0x"<<csvHex(r.vendorStatus)<<",0x"<<csvHex(r.commandWord2)<<",0x"<<csvHex(r.statusWord2);
  for(unsigned i=0;i<32;++i){s<<',';if(i<r.words.size())s<<"0x"<<csvHex(r.words[i]);}
  auto quote=[&](std::string_view value){s<<'"';for(char ch:value){if(ch=='"')s<<'"';s<<ch;}s<<'"';};
  s<<',';quote(session);s<<','<<p.transaction<<',';if(p.transmitterRt>=0)s<<p.transmitterRt;
  s<<',';if(p.receiverRt>=0)s<<p.receiverRt;s<<',';if(p.mode&&p.supported&&p.commandPresent)s<<p.modeCode;
  s<<',';quote(p.modeName);s<<',';if(p.supported&&p.commandPresent)s<<p.expectedWords;
  s<<','<<p.actualWords<<','<<(p.payloadEligible?"true":"false")<<',';
  quote(protocolDiagnostics(r,p));s<<',';quote(statusDescription(p));
  s<<','<<p.commandPresent<<','<<p.command2Present<<','<<p.statuses[0].present<<','<<p.statuses[1].present<<',';
  if(r.source)s<<r.messageType;
  s<<",not measured,"<<(r.source?"SDK type and presence flags":"Inferred; SDK presence/type unavailable")<<'\n';return std::move(s.text);
}
// Vendor-style session CSV: the 42 named columns in the supplied EXT/INT samples.
inline std::string sessionCsvHex(std::uint16_t value) {
  auto out=csvHex(value);for(auto &c:out)if(c>='A'&&c<='F')c=char(c-'A'+'a');return out;
}
inline std::string sessionCsvTime(std::uint64_t ns) {
  const auto us=ns/1000;const auto hours=us/3600000000ULL;
  std::ostringstream s;s.imbue(std::locale::classic());s<<std::setfill('0')<<std::setw(2)<<hours<<':'
    <<std::setw(2)<<(us/60000000)%60<<':'<<std::setw(2)<<(us/1000000)%60<<' '
    <<std::setw(3)<<(us/1000)%1000<<'.'<<std::setw(3)<<us%1000;return s.str();
}
inline std::string sessionCsvCommand(std::uint16_t word) {
  const auto c=command(word);std::ostringstream s;s.imbue(std::locale::classic());
  // The final field is the encoded 5-bit word-count/mode field (00 means 32 data words).
  s<<sessionCsvHex(word)<<" --> "<<std::setfill('0')<<std::setw(2)<<c.rt<<'-'<<(c.transmit?'T':'R')
   <<'-'<<std::setw(2)<<c.sa<<'-'<<std::setw(2)<<unsigned(word&31);return s.str();
}
inline std::string sessionCsvHeader() {
  std::string s="DateTime,DeltaT,BlockStatus,MessageType,Bus,Rx_Cmd,Tx_Cmd,Tx_Status,Rx_Status,WordCount";
  for(unsigned i=0;i<32;++i)s+=",DataWord_"+std::to_string(i);return s+"\n";
}
inline std::string sessionCsvRecord(const Record &r,std::optional<std::uint64_t> previous={}) {
  const auto p=interpret(r);std::string rx,tx,txStatus,rxStatus;
  if(p.commandPresent)(p.first.transmit?tx:rx)=sessionCsvCommand(r.commandWord);
  if(p.command2Present)(p.second.transmit?tx:rx)=sessionCsvCommand(r.commandWord2);
  // First status belongs to the transmitter in RT-to-RT, otherwise to the sole RT.
  if(p.statuses[0].present)(p.rtToRt||p.first.transmit?txStatus:rxStatus)=sessionCsvHex(r.statusWord);
  if(p.statuses[1].present)rxStatus=sessionCsvHex(r.statusWord2);
  std::string type(p.direction);for(auto pos=type.find("→");pos!=std::string::npos;pos=type.find("→",pos+2))type.replace(pos,3,"->");
  const auto difference=previous?(r.timestampNs>=*previous?r.timestampNs-*previous:*previous-r.timestampNs)/1000:0;
  const auto delta=(previous && r.timestampNs<*previous && difference?"-":"")+std::to_string(difference);
  CsvBuffer out;out<<sessionCsvTime(r.timestampNs)<<','<<delta<<','<<sessionCsvHex(r.vendorStatus)<<','<<type<<','<<(r.bus?'B':'A')
    <<','<<rx<<','<<tx<<','<<txStatus<<','<<rxStatus<<','<<r.words.size();
  for(unsigned i=0;i<32;++i){out<<',';out<<(i<r.words.size()?sessionCsvHex(r.words[i]):"-");}
  out<<'\n';return std::move(out.text);
}

}
