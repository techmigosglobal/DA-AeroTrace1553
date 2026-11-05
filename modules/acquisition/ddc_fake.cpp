// Test-only DLL. Implements a small receive-only SDK contract with deterministic
// Chapter 10 packets. Never packaged or used to qualify a physical DDC card.
#include <windows.h>
#include "stdemace.h"
#include <cstring>
#include <array>
#include <cstdlib>
#include <cstdio>
#include <cstdint>

namespace {
bool configured[32]{},sent[32]{},active[32]{},startedOnce[32]{};
int starts{};
bool flag(const char *name) {char buffer[8]{};return GetEnvironmentVariableA(name,buffer,sizeof(buffer))!=0;}
}
#define EXPORT extern "C" __declspec(dllexport)
EXPORT U16BIT WINAPI aceGetLibVersion(){char value[32]{};return GetEnvironmentVariableA("AEROTRACE_TEST_DDC_API",value,sizeof(value))?U16BIT(std::strtoul(value,nullptr,0)):0x4A2;}
EXPORT S16BIT WINAPI aceErrorStr(S16BIT error,char *buffer,U16BIT size){std::snprintf(buffer,size,"fake SDK status %d",error);return 0;}
EXPORT S16BIT WINAPI aceInitialize(S16BIT device,U16BIT access,U16BIT mode,U32BIT,U32BIT,U32BIT){
  if(flag("AEROTRACE_TEST_DDC_SLOW_OPEN"))Sleep(100);
  if(flag("AEROTRACE_TEST_DDC_DRIVER_OPEN")) return ACE_ERR_DRIVER_OPEN;
  if(device==1 && flag("AEROTRACE_TEST_DDC_FAIL_SECOND")) return -80;
  if(device<0 || device>=32 || access!=ACE_ACCESS_CARD || ((mode&0xff)!=ACE_MODE_MTI&&(mode&0xff)!=ACE_MODE_MT)) return -1;
  configured[device]=true;sent[device]=false;startedOnce[device]=false;return 0;
}
EXPORT S16BIT WINAPI aceGetHwVersionInfo(S16BIT device,PHWVERSIONINFO out){std::memset(out,0,sizeof(*out));std::memcpy(out->szModelName,"SDK fake",8);
  bool legacy=flag("AEROTRACE_TEST_DDC_NO_MTI")||(device==0&&flag("AEROTRACE_TEST_DDC_MIXED"));
#ifdef AEROTRACE_FAKE_LEGACY_ONLY
  legacy=!flag("AEROTRACE_TEST_DDC_FORCE_MTI");
#endif
  out->dwCapabilities=legacy?0:HWVER_CAPABILITY_MTI;out->dwFamilyNumber=legacy?E2MACE:ACEX;return 0;}
EXPORT S16BIT WINAPI aceFree(S16BIT device){configured[device]=false;active[device]=false;return flag("AEROTRACE_TEST_DDC_FREE_OPEN_FAIL")||(startedOnce[device]&&flag("AEROTRACE_TEST_DDC_FREE_FAIL"))?-81:0;}
EXPORT S16BIT WINAPI aceSetTimeTagRes(S16BIT,U16BIT resolution){return resolution==ACE_TT_2US ? 0 : -1;}
EXPORT int WINAPI aeroTestStartCount(){return starts;}
EXPORT void WINAPI aeroTestResetStarts(){starts=0;}
EXPORT int WINAPI aeroTestOpenCount(){int n=0;for(bool value:configured)n+=value;return n;}
#ifndef AEROTRACE_FAKE_LEGACY_ONLY
EXPORT S16BIT WINAPI aceMTIConfigure(S16BIT device,U32BIT,U32BIT count,U32BIT bytes,BOOLEAN zero,U32BIT,U32BIT messages,U16BIT interval,U32BIT,U16BIT,U8BIT,U8BIT,U8BIT,U32BIT){
  return configured[device] && count==32 && bytes==65536 && !zero && messages<=1000 && interval>=10 ? 0 : -1;
}
EXPORT S16BIT WINAPI aceMTIStart(S16BIT device){
  if(flag("AEROTRACE_TEST_DDC_REQUIRE_DUAL") && !(configured[0] && configured[1])) return -98;
  if(device==1&&flag("AEROTRACE_TEST_DDC_START_SECOND"))return -98;
  sent[device]=false;++starts;active[device]=startedOnce[device]=true;return 0;
}
EXPORT S16BIT WINAPI aceMTIStop(S16BIT device){if(flag("AEROTRACE_TEST_DDC_STOP_FAIL"))return -80;active[device]=false;return 0;}
EXPORT S16BIT WINAPI aceMTIGetCh10DataPkt(S16BIT device,PMTI_CH10_DATA_PKT *output,S16BIT timeout){
  if(timeout!=0) return -2;
  if(flag("AEROTRACE_TEST_DDC_TAIL_ONLY") && active[device])return ACE_ERR_BUFFER_UNAVAILABLE;
  if(sent[device] && flag("AEROTRACE_TEST_DDC_DISCONNECT"))return ACE_ERR_DRIVER_OPEN;
  if(sent[device] && flag("AEROTRACE_TEST_DDC_OVERFLOW"))return ACE_ERR_BUFFER_OVERFLOW;
  if(sent[device]) return ACE_ERR_BUFFER_UNAVAILABLE;
  static std::array<std::uint64_t,8192> owned;
  if(flag("AEROTRACE_TEST_DDC_SDK_OWNED"))*output=reinterpret_cast<PMTI_CH10_DATA_PKT>(owned.data());
  auto packet=*output;if(!packet) return -3;
  std::memset(packet,0,72);packet->u16PktSyncPattern=IRIG_CH10_PKT_SYNC_PATT;packet->u16ChannelId=device+1;
  packet->u32PktLength=72;packet->u32DataLength=48;packet->u32ChnlSpecificData=2;packet->u16DatTypePktFlags=0x1900;
  if(flag("AEROTRACE_TEST_DDC_ABSOLUTE")) packet->u16DatTypePktFlags|=0x40;
  auto cursor=reinterpret_cast<U16BIT *>(reinterpret_cast<std::uint8_t *>(packet)+28);
  for(unsigned index=0;index<2;++index) {
    std::uint64_t ticks=100000+device*100+index*(flag("AEROTRACE_TEST_DDC_2US_GAP")?50000:1);
    if(flag("AEROTRACE_TEST_DDC_UNUSED_TIME")) ticks|=0xffff000000000000ull;
    if(flag("AEROTRACE_TEST_DDC_RTC_WRAP")) ticks=index ? 0 : (1ull<<48)-1;
    std::memcpy(cursor,&ticks,8);cursor+=4;
    const bool busB=flag("AEROTRACE_TEST_DDC_BOTH_BUSES")?index==1:device%2!=0;
    *cursor++=ACE_MT_BSW_EOM|(busB ? ACE_MT_BSW_CHNL : 0);*cursor++=0;*cursor++=8;
    *cursor++=0x1842;*cursor++=1200+index;*cursor++=2000+index;*cursor++=0x1800;
  }
  sent[device]=true;return 0;
}
EXPORT S16BIT WINAPI aceMTIFreeCh10DataPkt(S16BIT,PMTI_CH10_DATA_PKT*){return flag("AEROTRACE_TEST_DDC_PACKET_FREE_FAIL")?-81:flag("AEROTRACE_TEST_DDC_FREE_UNSUPPORTED")?ACE_ERR_NOT_SUPPORTED:0;}
EXPORT S16BIT WINAPI aceMTIDecodeRawMsg(PMTI_CH10_DATA_PKT,U16BIT **cursor,MSGSTRUCT *output){
  auto raw=*cursor;std::memset(output,0,sizeof(*output));output->wBlkSts=raw[4];
  output->wCmdWrd1=raw[7];output->wStsWrd1=raw[10];output->wCmdWrd1Flg=output->wStsWrd1Flg=1;
  output->wWordCount=2;output->aDataWrds[0]=raw[8];output->aDataWrds[1]=raw[9];
  *cursor+=11;return 0;
}
#ifndef AEROTRACE_FAKE_NO_METRICS
EXPORT S16BIT WINAPI aceMtiGetMetrics(S16BIT device,ACEX_MTI_METRICS *output){
  std::memset(output,0,sizeof(*output));if(flag("AEROTRACE_TEST_DDC_LOSS")||(!active[device]&&flag("AEROTRACE_TEST_DDC_STOP_LOSS")))output->u32MtiDroppedMsgs=device+1;
  if(flag("AEROTRACE_TEST_DDC_COUNTER_OVERFLOW"))output->u32MtiStkOverflowCount=device+1;return 0;
}
#endif
#endif

EXPORT S16BIT WINAPI aceMTConfigure(S16BIT,U16BIT,U16BIT,U16BIT,U32BIT){return flag("AEROTRACE_TEST_DDC_UNSUPPORTED")?ACE_ERR_NOT_SUPPORTED:0;}
EXPORT S16BIT WINAPI aceMTInstallHBuf(S16BIT,U32BIT){return 0;}
EXPORT S16BIT WINAPI aceMTStart(S16BIT d){if(d==1&&flag("AEROTRACE_TEST_DDC_START_SECOND"))return -98;++starts;active[d]=startedOnce[d]=true;return 0;}
EXPORT S16BIT WINAPI aceMTStop(S16BIT d){if(flag("AEROTRACE_TEST_DDC_STOP_FAIL"))return -80;active[d]=false;return 0;}
EXPORT S16BIT WINAPI aceMTGetHBufMsgsRaw(S16BIT d,U16BIT *out,U16BIT,U32BIT *count,U32BIT *lost,U32BIT *host){
  *lost=*host=0;if((active[d]&&flag("AEROTRACE_TEST_DDC_TAIL_ONLY"))||sent[d]){*count=0;return 0;}*count=1;std::memset(out,0,80);out[0]=1200;sent[d]=true;return 0;
}
EXPORT S16BIT WINAPI aceMTDecodeRawMsg(S16BIT d,U16BIT *raw,MSGSTRUCT *out){std::memset(out,0,sizeof(*out));out->wCmdWrd1=0x1841;out->wCmdWrd1Flg=1;out->wWordCount=1;out->aDataWrds[0]=raw[0];out->wTimeTag=1234;out->wBlkSts=d%2?ACE_MT_BSW_CHNL:0;return 0;}
