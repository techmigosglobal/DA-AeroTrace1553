#include "core.hpp"
#include "field_encoding.hpp"
#include "protocol.hpp"
#include "capture_file.hpp"
#include <cmath>
#include <cstring>
#include <limits>
#include <bit>
#include <sstream>
#include <iomanip>
#include <locale>
#include <algorithm>
#include <stdexcept>
namespace aero {
Command command(std::uint16_t w) {
  Command c;
  c.rt = (w >> 11) & 31;
  c.transmit = (w >> 10) & 1;
  c.sa = (w >> 5) & 31;
  c.mode = c.sa == 0 || c.sa == 31;
  c.broadcast = c.rt == 31;
  c.count = w & 31;
  if (!c.mode && !c.count)
    c.count = 32;
  return c;
}
namespace {
constexpr std::string_view sourceWordNames[]{"payload","command1","command2","status1","status2"};
}
std::string_view signalWordName(SignalWord word) {
  const auto i=static_cast<unsigned>(word);
  return i<std::size(sourceWordNames)?sourceWordNames[i]:std::string_view{};
}
std::optional<SignalWord> parseSignalWord(std::string_view name) {
  for(unsigned i=0;i<std::size(sourceWordNames);++i)
    if(sourceWordNames[i]==name)return static_cast<SignalWord>(i);
  return {};
}
bool prepareConversion(Signal &signal) {
  if(signal.conversionFormula.empty()) {
    signal.preparedFormula.reset();
    return true;
  }
  if(signal.preparedFormula && signal.preparedFormula->matches(signal.conversionFormula))return true;
  auto compiled=ConversionFormula::compile(signal.conversionFormula);
  signal.preparedFormula=compiled?std::make_shared<const ConversionFormula>(std::move(*compiled)):nullptr;
  return bool(signal.preparedFormula);
}
std::string validate(const Signal &s) {
  if (s.id.empty())
    return "Signal ID is required";
  if (s.rt > 31 || s.sa > 31 || s.word > 31)
    return "RT, SA and word index must be 0–31";
  if (s.expectedWordCount>32) return "Expected word count must be 1–32 or unspecified";
  if (!s.width || s.width > 64 || s.lsb > 15 || s.lsb+s.width>64 || s.word + (s.lsb + s.width + 15) / 16 > 32)
    return "Bit field must fit the payload (1–64 bits)";
  if(signalWordName(s.sourceWord).empty())return "Unknown source word";
  if(s.sourceWord!=SignalWord::Payload && (s.word || s.lsb+s.width>16))
    return "Command and status fields must fit one 16-bit word";
  if((s.sourceWord==SignalWord::Command2 || s.sourceWord==SignalWord::Status2) && s.peerRt<0)
    return "Select the opposite RT and subaddress for a second command or status";
  if(s.sourceWord==SignalWord::Payload && s.expectedWordCount &&
     s.word+(s.lsb+s.width+15)/16>s.expectedWordCount) return "Bit field exceeds the expected word count";
  if(s.matchWord<-1 || s.matchWord>31)return "Frame tag word index must be 0–31 or unspecified";
  if(s.matchWord>=0 && (s.sourceWord!=SignalWord::Payload || (s.expectedWordCount && unsigned(s.matchWord)>=s.expectedWordCount)))
    return "Frame tag word must point to a valid payload word inside the expected word count";
  if(s.peerRt < -1 || s.peerRt>31 || s.peerSa < -1 || s.peerSa>31 ||
     ((s.peerRt<0)!=(s.peerSa<0)))return "Specify both RT-to-RT peer addresses";
  if(s.peerRt>=0) {
    const bool receiving=s.sourceWord==SignalWord::Command1 || s.sourceWord==SignalWord::Status2;
    if(s.transmit==receiving || int(s.rt)==s.peerRt || s.sa==0 || s.sa==31 || s.peerSa==0 || s.peerSa==31 ||
       (receiving && s.peerRt==31) || (s.rt==31 && (!receiving || s.sourceWord==SignalWord::Status2)))
      return "RT-to-RT route needs distinct terminals, data subaddresses and the direction for the selected word";
  }
  if(s.channel>2 || s.bus < -1 || s.bus > 1) return "Invalid channel or bus";
  if(s.numericType!="integer" && s.numericType!="float32" && s.numericType!="float64" && s.numericType!="bcd")
    return "Unsupported numeric type";
  if((s.numericType=="float32" && (s.width!=32 || s.lsb)) ||
     (s.numericType=="float64" && (s.width!=64 || s.lsb)) ||
     (s.numericType=="bcd" && s.width%4)) return "Numeric type and bit width disagree";
  if (!std::isfinite(s.scale) || !std::isfinite(s.offset))
    return "Scale and offset must be finite";
  if(!s.conversionFormula.empty() && (!s.preparedFormula || !s.preparedFormula->matches(s.conversionFormula)) &&
     !ConversionFormula::compile(s.conversionFormula))
    return "Invalid custom conversion. Use x, numbers, arithmetic or the supported functions.";
  return {};
}
std::optional<double> decode(const Record &r, const Signal &s, bool exampleContext) {
  return decodePayload(r,s,interpret(r),exampleContext);
}
std::optional<double> decodePayload(const Record &r,const Signal &s,const ProtocolInfo &protocol, bool exampleContext) {
  if (!validate(s).empty() || (s.simulationOnly && (!exampleContext || r.source!=0)))return {};
  if(s.icdSetId=="manual" && (protocol.rtToRt || protocol.mode))return {};
  const bool first=s.sourceWord==SignalWord::Command1 || s.sourceWord==SignalWord::Status2;
  const auto &c=signalCommand(protocol,s);
  if(!matchesSignalRoute(r,s,c))return {};
  if(s.peerRt>=0) {
    const auto peer=first ? protocol.second : protocol.first;
    if(!protocol.rtToRt || !protocol.command2Present || int(peer.rt)!=s.peerRt || int(peer.sa)!=s.peerSa)return {};
  }
  if(s.expectedWordCount && (protocol.mode || c.count!=s.expectedWordCount))return {};
  const FieldEncoding encoding(s);
  switch(s.sourceWord) {
    case SignalWord::Payload:
      if(!protocol.payloadEligible || s.word+encoding.wordCount>r.words.size() ||
         (s.expectedWordCount && r.words.size()!=s.expectedWordCount))return {};
      return encoding.convert(std::span(r.words).subspan(s.word));
    default: {
      const auto word=capturedHeaderWord(r,protocol,s.sourceWord);
      if(!protocol.valid || !word)return {};
      return encoding.convert(std::span(&*word,1));
    }
  }
}
Record synthetic(std::uint64_t n) {
  Record r;
  r.id = n + 1;
  r.timestampNs = n * 83000000ull;
  r.channel = 1 + (n / 2) % 2;
  r.bus = n % 2;
  unsigned rt = n % 4 == 0 ? 3 : 1 + (n * 7) % 28,
           sa = n % 4 == 0 ? 2 : 1 + (n * 3) % 30;
  bool tx = n % 3 != 0 && n % 4 != 0;
  r.commandWord = (rt << 11) | (tx << 10) | (sa << 5) | 8;
  r.words = {static_cast<std::uint16_t>(1000 + 180 * std::sin(n * .07)),
             static_cast<std::uint16_t>(6699 + 600 * std::sin(n * .05)),
             static_cast<std::uint16_t>(2850 + 80 * std::sin(n * .09)),
             static_cast<std::uint16_t>(242 + 40 * std::sin(n * .13)),
             static_cast<std::uint16_t>(33555 + 1000 * std::sin(n * .03)),
             static_cast<std::uint16_t>(418 + 60 * std::sin(n * .08)),
             static_cast<std::uint16_t>(1245 + 100 * std::sin(n * .04)),
             8};
  r.statusWord = rt << 11;
  r.flags = n && n % 97 == 0 ? 1 : 0;
  return r;
}
std::string serializeProfile(const std::vector<Signal> &mappings) {
  auto quote=[](const std::string &value) {
    std::ostringstream output;output<<'"';
    for(unsigned char c:value) {
      if(c=='"' || c=='\\') output<<'\\'<<char(c);
      else if(c<32) output<<"\\u"<<std::hex<<std::setw(4)<<std::setfill('0')<<unsigned(c)<<std::dec;
      else output<<char(c);
    }
    output<<'"';return output.str();
  };
  std::ostringstream out;out.imbue(std::locale::classic());out<<std::setprecision(17);
  const bool custom=std::any_of(mappings.begin(),mappings.end(),[](const Signal &s){return !s.conversionFormula.empty();});
  out<<"{\"schema\":\"aerotrace.icd.profile.v"<<(custom?3:2)<<"\",\"signals\":[";
  for(std::size_t index=0;index<mappings.size();++index) {
    const auto &s=mappings[index];if(index) out<<',';
    out<<"{\"id\":"<<quote(s.id)<<",\"unit\":"<<quote(s.unit)<<",\"rt\":"<<s.rt<<",\"sa\":"<<s.sa
      <<",\"word\":"<<s.word<<",\"lsb\":"<<s.lsb<<",\"width\":"<<s.width<<",\"scale\":"<<s.scale<<",\"offset\":"<<s.offset
      <<",\"signed\":"<<(s.signedValue?"true":"false")<<",\"transmit\":"<<(s.transmit?"true":"false")
      <<",\"channel\":"<<s.channel<<",\"bus\":"<<s.bus<<",\"numeric_type\":"<<quote(s.numericType)
      <<",\"lsw_first\":"<<(s.leastSignificantWordFirst?"true":"false")<<",\"expected_word_count\":"<<s.expectedWordCount<<",\"simulation_only\":"<<(s.simulationOnly?"true":"false")<<",\"source_document\":"<<quote(s.sourceDocument)
      <<",\"source_hash\":"<<quote(s.sourceHash)<<",\"source_page\":"<<s.sourcePage<<",\"operator_note\":"<<quote(s.operatorNote)
      <<",\"subsystem\":"<<quote(s.subsystem)<<",\"message_id\":"<<quote(s.messageId)<<",\"source_signal_id\":"<<quote(s.sourceSignalId)<<",\"icd_set_id\":"<<quote(s.icdSetId)<<",\"icd_revision\":"<<quote(s.icdRevision)
      <<",\"source_word\":"<<quote(std::string(signalWordName(s.sourceWord)))<<",\"peer_rt\":"<<s.peerRt<<",\"peer_sa\":"<<s.peerSa;
    if(!s.conversionFormula.empty())out<<",\"conversion_formula\":"<<quote(s.conversionFormula);
    if(s.matchWord>=0)out<<",\"match_word\":"<<s.matchWord<<",\"match_value\":"<<s.matchValue;
    out<<'}';
  }
  out<<"]}";return out.str();
}
std::uint32_t crc32(const std::vector<std::uint8_t> &v) {
  static constexpr auto table=[] {
    std::array<std::uint32_t,256> values{};
    for(unsigned i=0;i<256;++i){auto c=std::uint32_t(i);for(unsigned bit=0;bit<8;++bit)c=(c>>1)^(0xedb88320u&-(c&1));values[i]=c;}
    return values;
  }();
  std::uint32_t c = ~0u;
  for (auto b : v)c=(c>>8)^table[(c^b)&255];
  return ~c;
}
template <class T> void put(std::vector<std::uint8_t> &v, T n) {
  for (unsigned i = 0; i < sizeof(T); ++i)
    v.push_back(static_cast<std::uint8_t>(n >> (8 * i)));
}
template <class T> T get(const std::vector<std::uint8_t> &v, std::size_t &p) {
  std::uint64_t n = 0;
  for (unsigned i = 0; i < sizeof(T); ++i)
    n |= std::uint64_t(v.at(p++)) << (8 * i);
  return static_cast<T>(n);
}
Recorder::~Recorder(){close();}
bool Recorder::open(const std::filesystem::path &p,bool exclusive) {
  close();
  out=capture_file::open(p,exclusive);
  return capture_file::write(out,"ATD2\x02\x00\x00\x00",8);
}
bool Recorder::append(const Record &r) {return appendBatch(std::span<const Record>(&r,1));}
bool Recorder::appendBatch(std::span<const Record> records) {
  if(!out)return false;
  constexpr std::size_t blockLimit=65536;
  if(payloadBuffer.capacity()<121)payloadBuffer.reserve(121);
  if(batchBuffer.capacity()<blockLimit)batchBuffer.reserve(blockLimit);
  batchBuffer.clear();
  for(const auto &r:records){
    if(r.words.size()>32)return false;
    if(batchBuffer.size()+129>blockLimit){if(!capture_file::write(out,batchBuffer.data(),batchBuffer.size()))return false;batchBuffer.clear();}
    auto &v=payloadBuffer;v.clear();
  put(v, r.id);
  put(v, r.timestampNs);
  put(v, r.channel);
  put(v, r.bus);
  put(v, r.commandWord);
  put(v, r.statusWord);
  put(v, r.flags);
  put(v, static_cast<std::uint16_t>(r.words.size()));
  for (auto w : r.words)
    put(v, w);
  put(v,r.source); put(v,r.vendorStatus); put(v,r.commandWord2); put(v,r.statusWord2);
  put(v,r.validity); put(v,r.messageType); put(v,r.deviceTicks); put(v,r.hostMonotonicNs); put(v,r.tickResolutionNs);

    put(batchBuffer,static_cast<std::uint32_t>(v.size()));put(batchBuffer,crc32(v));
    batchBuffer.insert(batchBuffer.end(),v.begin(),v.end());
  }
  return batchBuffer.empty() || capture_file::write(out,batchBuffer.data(),batchBuffer.size());
}
bool Recorder::flush() { return out && std::fflush(out)==0; }
bool Recorder::checkpoint(){return capture_file::checkpoint(out);}
bool Recorder::close() {
  if(!out)return true;
  const bool ok=std::fclose(out)==0;
  out=nullptr;
  return ok;
}

namespace {
bool readRecord(std::istream &f,bool version2,Record &r,bool &recovered) {
  std::vector<std::uint8_t> h(8);
  f.read(reinterpret_cast<char *>(h.data()),8);
  if(!f.gcount()) { if(f.bad()) throw std::runtime_error("Recording read failed"); return false; }
  if(f.gcount()!=8) {recovered=true;return false;}
  std::size_t p=0;const auto length=get<std::uint32_t>(h,p),crc=get<std::uint32_t>(h,p);
  const unsigned extra=version2?31:0;
  if(length<26+extra || length>90+extra) {recovered=true;return false;}
  std::vector<std::uint8_t> v(length);f.read(reinterpret_cast<char *>(v.data()),length);
  if(f.gcount()!=length || crc32(v)!=crc) {recovered=true;return false;}
  p=0;r={};r.id=get<std::uint64_t>(v,p);r.timestampNs=get<std::uint64_t>(v,p);
  r.channel=get<std::uint8_t>(v,p);r.bus=get<std::uint8_t>(v,p);
  r.commandWord=get<std::uint16_t>(v,p);r.statusWord=get<std::uint16_t>(v,p);r.flags=get<std::uint16_t>(v,p);
  auto count=get<std::uint16_t>(v,p);
  if(count>32 || 26+count*2+extra!=length || r.channel<1 || r.channel>2 || r.bus>1) {recovered=true;return false;}
  while(count--) r.words.push_back(get<std::uint16_t>(v,p));
  if(version2) {
    r.source=get<std::uint8_t>(v,p);r.vendorStatus=get<std::uint16_t>(v,p);
    r.commandWord2=get<std::uint16_t>(v,p);r.statusWord2=get<std::uint16_t>(v,p);
    r.validity=get<std::uint16_t>(v,p);r.messageType=get<std::uint16_t>(v,p);
    r.deviceTicks=get<std::uint64_t>(v,p);r.hostMonotonicNs=get<std::uint64_t>(v,p);r.tickResolutionNs=get<std::uint32_t>(v,p);
    if(r.source>2) {recovered=true;return false;}
  }
  return true;
}
bool recordingHeader(std::istream &f,bool &version2) {
  char magic[8]{};f.read(magic,8);
  version2=std::memcmp(magic,"ATD2\x02\x00\x00\x00",8)==0;
  return f.gcount()==8 && (version2 || std::memcmp(magic,"ATD1\x01\x00\x00\x00",8)==0);
}
}
ScanResult scanRecordingStream(std::istream &f,const std::function<bool(const Record &,std::uint64_t)> &visit) {
  ScanResult result;
  try {
    bool version2=false;if(!recordingHeader(f,version2)) {result.error="Unsupported recording header/version";return result;}
    // readRecord validates the exact format/word-count length. Track consumed
    // bytes instead of querying the filesystem position for every message.
    std::uint64_t offset=8;
    for(;;) {Record record;if(!readRecord(f,version2,record,result.recovered)) break;
      ++result.count;if(!visit(record,offset)) break;
      offset+=8+26+(version2?31:0)+record.words.size()*2;}
  } catch(const std::exception &e) {result.error=e.what();}
  return result;
}
ScanResult scanRecording(const std::filesystem::path &path,const std::function<bool(const Record &,std::uint64_t)> &visit) {
  std::ifstream f(path,std::ios::binary);if(!f)return {0,false,"Cannot open recording"};return scanRecordingStream(f,visit);
}
ReadResult read(const std::filesystem::path &path) {
  ReadResult result;const auto status=scanRecording(path,[&](const Record &r,std::uint64_t){result.records.push_back(r);return true;});
  result.error=status.error;result.recovered=status.recovered;return result;
}
ScanResult RecordingIndex::open(const std::filesystem::path &value,const std::function<void(const Record &)> &visitor) {
  std::lock_guard lock(inputMutex);
  entries.clear();input.close();path=value;
  std::ifstream header(path,std::ios::binary);recordingHeader(header,version2);
  auto result=scanRecording(path,[&](const Record &r,std::uint64_t offset){entries.push_back({offset,r.timestampNs,entries.empty()?r.timestampNs:std::max(r.timestampNs,entries.back().playback)});if(visitor)visitor(r);return true;});
  // Preserve acquisition order, including clock reversals; playback scheduling alone is nondecreasing.
  if(!result.error.empty()) entries.clear();else input.open(path,std::ios::binary);return result;
}
Record RecordingIndex::at(std::size_t i) const {
  std::lock_guard lock(inputMutex);
  if(!input.is_open())throw std::runtime_error("Recording is no longer available");
  input.clear();input.seekg(entries.at(i).offset);Record r;bool recovered=false;
  if(!readRecord(input,version2,r,recovered))throw std::runtime_error("Recording changed or failed CRC validation during replay");
  return r;
}
} // namespace aero
