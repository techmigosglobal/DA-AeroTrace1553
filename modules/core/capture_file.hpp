#pragma once

#include <cstdio>
#include <filesystem>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <fcntl.h>
#include <io.h>
#else
#include <unistd.h>
#endif

namespace aero::capture_file {
inline std::FILE *open(const std::filesystem::path &path, bool exclusive) {
#ifdef _WIN32
  const auto handle=CreateFileW(path.c_str(),GENERIC_WRITE,FILE_SHARE_READ,nullptr,
                                exclusive ? CREATE_NEW : CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
  if(handle==INVALID_HANDLE_VALUE)return nullptr;
  const int descriptor=_open_osfhandle(reinterpret_cast<intptr_t>(handle),_O_WRONLY|_O_BINARY);
  if(descriptor<0){CloseHandle(handle);return nullptr;}
  auto *file=_fdopen(descriptor,"wb");
  if(!file)_close(descriptor);
  return file;
#else
  return std::fopen(path.c_str(),exclusive ? "wbx" : "wb");
#endif
}

inline bool write(std::FILE *file,const void *data,std::size_t size) {
  return file && std::fwrite(data,1,size,file)==size;
}

inline bool checkpoint(std::FILE *file) {
  if(!file || std::fflush(file)!=0)return false;
#ifdef _WIN32
  return FlushFileBuffers(reinterpret_cast<HANDLE>(_get_osfhandle(_fileno(file))))!=0;
#else
  return fsync(fileno(file))==0;
#endif
}
}
