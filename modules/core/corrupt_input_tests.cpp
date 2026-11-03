#include "core.hpp"
#include "packet_bounds.hpp"
#include <sstream>
#include <chrono>
#include <array>
#include <iostream>
#define CHECK(x) do{if(!(x))throw std::runtime_error(#x);}while(false)
int main(){const auto root=std::filesystem::temp_directory_path()/("aerotrace-corrupt-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));try{
  CHECK(std::filesystem::create_directory(root));const auto file=root/"test.atd";aero::Recorder writer;CHECK(writer.open(file));
  for(auto t:std::array<std::uint64_t,3>{0,UINT64_MAX,1}){auto r=aero::synthetic(0);r.timestampNs=t;CHECK(writer.append(r));}CHECK(writer.close());
  aero::RecordingIndex index;CHECK(index.open(file).count==3);CHECK(index.playbackTimestamp(2)==UINT64_MAX);CHECK(index.at(2).timestampNs==1);
  std::ifstream input(file,std::ios::binary);const std::string bytes{std::istreambuf_iterator<char>(input),{}};input.close();
  for(std::size_t n=0;n<bytes.size();++n){std::istringstream truncated(bytes.substr(0,n));const auto scanned=aero::scanRecordingStream(truncated,[](const auto &,auto){return true;});CHECK(scanned.count<=3);CHECK(n<8?!scanned.error.empty():scanned.error.empty());}
  auto bad=bytes;bad[8]=char(255);bad[9]=char(255);bad[10]=char(255);bad[11]=char(255);std::istringstream enormous(bad);CHECK(aero::scanRecordingStream(enormous,[](const auto &,auto){return true;}).recovered);
  bad=bytes;bad[20]^=1;std::istringstream crc(bad);CHECK(aero::scanRecordingStream(crc,[](const auto &,auto){return true;}).recovered);
  {std::fstream changed(file,std::ios::binary|std::ios::in|std::ios::out);changed.seekp(20);changed.put(bad[20]);}bool rejected=false;try{index.at(0);}catch(...){rejected=true;}CHECK(rejected);
  std::array<std::uint8_t,28> packet{};packet[0]=0x25;packet[1]=0xeb;packet[4]=28;packet[8]=4;CHECK(aero::validChapter10Envelope(packet));for(unsigned n=0;n<28;++n)CHECK(!aero::validChapter10Envelope(std::span(packet).first(n)));packet[4]=255;CHECK(!aero::validChapter10Envelope(packet));
  // Index owns an input stream until destruction; leave this tiny fixture for the OS
  // temporary-directory cleanup rather than deleting an open Windows file.
  std::cout<<"PASS: truncated headers/records, length bounds, CRC, changed replay data, time limits and packet envelopes\n";return 0;
}catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}}
