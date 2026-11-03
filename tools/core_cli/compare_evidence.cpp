#include "core.hpp"
#include "qualification_source.hpp"
#include "signal_checks.hpp"
#include "capture_time.hpp"
#include <array>
#include <iostream>
#include <sstream>
#include <windows.h>
#include <bcrypt.h>
namespace {
struct Hash {
  BCRYPT_ALG_HANDLE algorithm{};BCRYPT_HASH_HANDLE hash{};
  Hash(){if(BCryptOpenAlgorithmProvider(&algorithm,BCRYPT_SHA256_ALGORITHM,nullptr,0)<0||BCryptCreateHash(algorithm,&hash,nullptr,0,nullptr,0,0)<0)throw std::runtime_error("SHA-256 setup failed");}
  ~Hash(){if(hash)BCryptDestroyHash(hash);if(algorithm)BCryptCloseAlgorithmProvider(algorithm,0);}
  void add(const std::vector<unsigned char> &b){if(BCryptHashData(hash,const_cast<PUCHAR>(b.data()),ULONG(b.size()),0)<0)throw std::runtime_error("SHA-256 update failed");}
  std::string finish(){std::array<unsigned char,32> b{};if(BCryptFinishHash(hash,b.data(),b.size(),0)<0)throw std::runtime_error("SHA-256 finish failed");std::string s;constexpr char hex[]="0123456789abcdef";for(auto v:b){s+=hex[v>>4];s+=hex[v&15];}return s;}
};
struct Summary {std::array<std::uint64_t,4> count{};std::array<std::string,4> digest{};aero::ScanResult scan;};
Summary summarize(const std::filesystem::path &path){Summary out;std::array<Hash,4> hashes;out.scan=aero::scanRecording(path,[&](const aero::Record &r,auto){
  const auto k=(r.channel-1)*2+r.bus;std::vector<unsigned char> b;b.reserve(80);auto word=[&](std::uint16_t v){b.push_back(v&255);b.push_back(v>>8);};
  word(r.commandWord);word(r.commandWord2);word(r.statusWord);word(r.statusWord2);word(r.flags);word(r.validity&15);word(std::uint16_t(r.words.size()));for(auto w:r.words)word(w);
  hashes[k].add(b);++out.count[k];return true;
});for(unsigned k=0;k<4;++k)out.digest[k]=hashes[k].finish();return out;}
bool compare(const std::filesystem::path &reference,const std::filesystem::path &observed,std::ostream &out){const auto a=summarize(reference),b=summarize(observed);bool passed=a.scan.error.empty()&&b.scan.error.empty()&&!a.scan.recovered&&!b.scan.recovered&&a.scan.count>0;
  for(unsigned k=0;k<4;++k)passed&=a.count[k]==b.count[k]&&a.digest[k]==b.digest[k];
  out<<"{\"schema\":\"aerotrace.reference-comparison.v1\",\"status\":\""<<(passed?"PASS":"FAIL")<<"\",\"physical_qualification\":\"NOT RUN: independent bench sign-off required\",\"timing_accuracy\":\"NOT MEASURED\",\"comparison\":\"Ordered per-stream SHA-256 of commands, statuses, flags, presence and payload; independent inter-stream ordering, identifiers and timestamps excluded\",\"error\":"<<aero::jsonString(a.scan.error+b.scan.error)<<",\"recovered\":"<<(a.scan.recovered||b.scan.recovered?"true":"false")<<",\"streams\":[";
  for(unsigned k=0;k<4;++k){if(k)out<<',';out<<"{\"channel\":"<<k/2+1<<",\"bus\":\""<<(k%2?'B':'A')<<"\",\"expected\":"<<a.count[k]<<",\"observed\":"<<b.count[k]<<",\"expected_sha256\":\""<<a.digest[k]<<"\",\"observed_sha256\":\""<<b.digest[k]<<"\"}";}out<<"]}\n";return passed;
}
int selfTest(){const auto root=std::filesystem::temp_directory_path()/("aerotrace-reference-"+std::to_string(GetCurrentProcessId())+"-"+std::to_string(GetTickCount64()));if(!std::filesystem::create_directory(root))return 1;
  struct Cleanup{std::filesystem::path p;~Cleanup(){std::error_code ec;const auto full=std::filesystem::weakly_canonical(p,ec);if(!ec&&full.parent_path()==std::filesystem::weakly_canonical(std::filesystem::temp_directory_path())&&full.filename().string().starts_with("aerotrace-reference-"))std::filesystem::remove_all(full,ec);}} cleanup{root};
  aero::Recorder writer;writer.open(root/"expected.atd");for(unsigned n=0;n<40;++n)writer.append(aero::qualificationRecord(n));writer.close();
  writer.open(root/"observed.atd");for(unsigned k=0;k<4;++k)for(unsigned n=k;n<40;n+=4){auto r=aero::qualificationRecord(n);r.timestampNs+=5000;r.id+=100;writer.append(r);}writer.close();
  std::ostringstream report;if(!compare(root/"expected.atd",root/"observed.atd",report))return 1;
  writer.open(root/"changed.atd");for(unsigned n=0;n<40;++n){auto r=aero::qualificationRecord(n);if(n==17)r.words[0]^=1;writer.append(r);}writer.close();if(compare(root/"expected.atd",root/"changed.atd",report))return 1;
  std::filesystem::resize_file(root/"observed.atd",20);if(compare(root/"expected.atd",root/"observed.atd",report))return 1;std::cout<<"PASS: independent inter-stream order, changed payload and truncated evidence\n";return 0;
}
}
int main(int argc,char **argv){try{if(argc==2&&std::string(argv[1])=="--self-test")return selfTest();if(argc!=4){std::cerr<<"Usage: aerotrace_compare <reference.atd> <observed.atd> <report.json>\n";return 2;}std::ofstream report(std::filesystem::path(std::u8string(reinterpret_cast<const char8_t *>(argv[3]))));if(!report)throw std::runtime_error("Cannot create comparison report");const auto passed=compare(std::filesystem::path(std::u8string(reinterpret_cast<const char8_t *>(argv[1]))),std::filesystem::path(std::u8string(reinterpret_cast<const char8_t *>(argv[2]))),report);report.flush();if(!report)throw std::runtime_error("Cannot write comparison report");return passed?0:4;}catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 2;}}
