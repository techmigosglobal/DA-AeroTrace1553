#pragma once
#include "helper_protocol.hpp"
#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <mutex>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <sddl.h>
#include <bcrypt.h>

namespace aero::helper {
struct Handle {
  HANDLE value{INVALID_HANDLE_VALUE};
  Handle()=default;explicit Handle(HANDLE v):value(v){}
  ~Handle(){reset();}Handle(const Handle &)=delete;Handle &operator=(const Handle &)=delete;
  Handle(Handle &&other)noexcept:value(other.value){other.value=INVALID_HANDLE_VALUE;}
  Handle &operator=(Handle &&other)noexcept{if(this!=&other){reset();value=other.value;other.value=INVALID_HANDLE_VALUE;}return *this;}
  void reset(HANDLE v=INVALID_HANDLE_VALUE){if(value&&value!=INVALID_HANDLE_VALUE)CloseHandle(value);value=v;}
  explicit operator bool()const{return value&&value!=INVALID_HANDLE_VALUE;}
};
inline std::runtime_error winError(const char *operation){return std::runtime_error(std::string(operation)+" (Windows "+std::to_string(GetLastError())+")");}
inline std::string utf8(const std::wstring &v){if(v.empty())return {};int n=WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,v.data(),int(v.size()),nullptr,0,nullptr,nullptr);if(!n)throw winError("UTF-16 conversion");std::string out(n,0);WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,v.data(),int(v.size()),out.data(),n,nullptr,nullptr);return out;}
inline std::wstring wide(const std::string &v){if(v.empty())return {};int n=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,v.data(),int(v.size()),nullptr,0);if(!n)throw winError("UTF-8 conversion");std::wstring out(n,0);MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,v.data(),int(v.size()),out.data(),n);return out;}
inline std::filesystem::path executablePath(){std::wstring p(32768,0);auto n=GetModuleFileNameW(nullptr,p.data(),DWORD(p.size()));if(!n||n>=p.size())throw winError("Get executable path");p.resize(n);return p;}
inline std::string hex(std::span<const std::uint8_t> bytes){static const char digits[]="0123456789abcdef";std::string s;for(auto b:bytes){s+=digits[b>>4];s+=digits[b&15];}return s;}
inline std::string nonce(){std::array<std::uint8_t,16>b{};if(BCryptGenRandom(nullptr,b.data(),ULONG(b.size()),BCRYPT_USE_SYSTEM_PREFERRED_RNG)<0)throw std::runtime_error("Cannot generate helper nonce");return hex(b);}
inline std::string hashFile(const std::filesystem::path &path){
  std::error_code ec;auto bytes=std::filesystem::file_size(path,ec);if(ec||bytes>256ull*1024*1024)throw std::runtime_error("Helper pack file missing or exceeds 256 MiB");
  BCRYPT_ALG_HANDLE algorithm{};BCRYPT_HASH_HANDLE hash{};if(BCryptOpenAlgorithmProvider(&algorithm,BCRYPT_SHA256_ALGORITHM,nullptr,0)<0)throw std::runtime_error("SHA-256 unavailable");
  struct Cleanup{BCRYPT_ALG_HANDLE a;BCRYPT_HASH_HANDLE &h;~Cleanup(){if(h)BCryptDestroyHash(h);BCryptCloseAlgorithmProvider(a,0);}}cleanup{algorithm,hash};
  if(BCryptCreateHash(algorithm,&hash,nullptr,0,nullptr,0,0)<0)throw std::runtime_error("SHA-256 initialization failed");std::ifstream in(path,std::ios::binary);std::array<char,65536> buffer{};std::uint64_t read{};
  while(in){in.read(buffer.data(),buffer.size());auto n=in.gcount();if(n){read+=n;if(BCryptHashData(hash,reinterpret_cast<PUCHAR>(buffer.data()),ULONG(n),0)<0)throw std::runtime_error("SHA-256 update failed");}}
  if(!in.eof()||read!=bytes)throw std::runtime_error("Helper pack changed or failed during hashing");std::array<std::uint8_t,32> digest{};if(BCryptFinishHash(hash,digest.data(),ULONG(digest.size()),0)<0)throw std::runtime_error("SHA-256 finalization failed");return hex(digest);
}
inline bool x86Image(const std::filesystem::path &p){std::ifstream f(p,std::ios::binary);std::uint16_t mz{},machine{};std::uint32_t offset{},signature{};f.read(reinterpret_cast<char*>(&mz),2);f.seekg(0x3c);f.read(reinterpret_cast<char*>(&offset),4);if(offset>16*1024*1024)return false;f.seekg(offset);f.read(reinterpret_cast<char*>(&signature),4);f.read(reinterpret_cast<char*>(&machine),2);return f&&mz==0x5a4d&&signature==0x4550&&machine==0x14c;}
struct PipeSecurity {
  PSECURITY_DESCRIPTOR descriptor{};SECURITY_ATTRIBUTES attributes{sizeof(SECURITY_ATTRIBUTES),nullptr,FALSE};
  PipeSecurity(){HANDLE raw{};if(!OpenProcessToken(GetCurrentProcess(),TOKEN_QUERY,&raw))throw winError("Read pipe owner token");Handle token(raw);DWORD n{};GetTokenInformation(token.value,TokenUser,nullptr,0,&n);std::vector<std::uint8_t> data(n);if(!GetTokenInformation(token.value,TokenUser,data.data(),n,&n))throw winError("Read pipe owner SID");LPWSTR sid{};if(!ConvertSidToStringSidW(reinterpret_cast<TOKEN_USER*>(data.data())->User.Sid,&sid))throw winError("Convert pipe owner SID");std::wstring sddl=L"D:P(A;;GA;;;"+std::wstring(sid)+L")";LocalFree(sid);if(!ConvertStringSecurityDescriptorToSecurityDescriptorW(sddl.c_str(),SDDL_REVISION_1,&descriptor,nullptr))throw winError("Pipe ACL");attributes.lpSecurityDescriptor=descriptor;}
  ~PipeSecurity(){if(descriptor)LocalFree(descriptor);}
};
inline Handle serverPipe(const std::wstring &name){PipeSecurity security;Handle p(CreateNamedPipeW(name.c_str(),PIPE_ACCESS_DUPLEX|FILE_FLAG_OVERLAPPED|FILE_FLAG_FIRST_PIPE_INSTANCE,PIPE_TYPE_BYTE|PIPE_READMODE_BYTE|PIPE_WAIT|PIPE_REJECT_REMOTE_CLIENTS,1,65536,65536,0,&security.attributes));if(!p)throw winError("Create helper pipe");return p;}
inline void connectPipe(HANDLE pipe,unsigned timeoutMs){Handle event(CreateEventW(nullptr,TRUE,FALSE,nullptr));OVERLAPPED operation{};operation.hEvent=event.value;if(ConnectNamedPipe(pipe,&operation))return;auto code=GetLastError();if(code==ERROR_PIPE_CONNECTED)return;if(code!=ERROR_IO_PENDING)throw winError("Connect helper pipe");if(WaitForSingleObject(event.value,timeoutMs)!=WAIT_OBJECT_0){CancelIoEx(pipe,&operation);WaitForSingleObject(event.value,INFINITE);throw std::runtime_error("Helper pipe handshake timed out");}DWORD n{};if(!GetOverlappedResult(pipe,&operation,&n,FALSE))throw winError("Complete helper pipe connection");}
inline Handle clientPipe(const std::wstring &name,unsigned timeoutMs){auto deadline=std::chrono::steady_clock::now()+std::chrono::milliseconds(timeoutMs);for(;;){Handle p(CreateFileW(name.c_str(),GENERIC_READ|GENERIC_WRITE,0,nullptr,OPEN_EXISTING,FILE_FLAG_OVERLAPPED,nullptr));if(p)return p;if(GetLastError()!=ERROR_PIPE_BUSY&&GetLastError()!=ERROR_FILE_NOT_FOUND)throw winError("Open helper pipe");if(std::chrono::steady_clock::now()>=deadline)throw std::runtime_error("Helper pipe unavailable before deadline");WaitNamedPipeW(name.c_str(),50);Sleep(1);}}
inline void transfer(HANDLE pipe,std::span<std::uint8_t> bytes,bool write,unsigned timeoutMs,const std::atomic_bool *closing=nullptr){
  const auto deadline=std::chrono::steady_clock::now()+std::chrono::milliseconds(timeoutMs);std::size_t at=0;
  while(at<bytes.size()){
    Handle event(CreateEventW(nullptr,TRUE,FALSE,nullptr));OVERLAPPED operation{};operation.hEvent=event.value;DWORD done{};BOOL ok=write?WriteFile(pipe,bytes.data()+at,DWORD(bytes.size()-at),&done,&operation):ReadFile(pipe,bytes.data()+at,DWORD(bytes.size()-at),&done,&operation);
    if(!ok){auto code=GetLastError();if(code!=ERROR_IO_PENDING)throw winError(write?"Helper pipe write":"Helper pipe read");for(;;){auto result=WaitForSingleObject(event.value,50);if(result==WAIT_OBJECT_0)break;if(result==WAIT_FAILED)throw winError("Helper pipe wait");if((closing&&closing->load())||std::chrono::steady_clock::now()>=deadline){CancelIoEx(pipe,&operation);WaitForSingleObject(event.value,INFINITE);throw std::runtime_error(closing&&closing->load()?"Helper pipe cancelled":"Helper pipe I/O deadline exceeded");}}if(!GetOverlappedResult(pipe,&operation,&done,FALSE))throw winError("Helper pipe completion");}
    if(!done)throw std::runtime_error("Helper pipe closed during frame");at+=done;
  }
}
inline Frame readFrame(HANDLE p,std::uint64_t &sequence,unsigned timeoutMs,const std::atomic_bool *closing=nullptr){std::vector<std::uint8_t> b(HeaderBytes);transfer(p,b,false,timeoutMs,closing);auto n=payloadLength(b);b.resize(HeaderBytes+n);if(n)transfer(p,std::span<std::uint8_t>(b).subspan(HeaderBytes),false,timeoutMs,closing);return decode(b,++sequence);}
inline void writeFrame(HANDLE p,const Frame &frame,unsigned timeoutMs,const std::atomic_bool *closing=nullptr){auto b=encode(frame);transfer(p,b,true,timeoutMs,closing);}
inline void clientIs(HANDLE pipe,DWORD process){ULONG actual{};if(!GetNamedPipeClientProcessId(pipe,&actual)||actual!=process)throw std::runtime_error("Unexpected helper pipe client process");}
inline void serverIs(HANDLE pipe,DWORD process){ULONG actual{};if(!GetNamedPipeServerProcessId(pipe,&actual)||actual!=process)throw std::runtime_error("Unexpected helper pipe server process");}
}
#endif
