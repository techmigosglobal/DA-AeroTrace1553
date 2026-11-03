#pragma once
/* AeroTrace receive provider ABI v1. x64, natural alignment, C calling convention.
 * No C++/Qt/vendor types or allocator ownership cross this boundary. */
#include <stdint.h>
#include <stddef.h>
#ifdef _WIN32
#define AT_CALL __cdecl
#define AT_EXPORT __declspec(dllexport)
#else
#define AT_CALL
#define AT_EXPORT
#endif
#define AT_PROVIDER_ABI 1u
#define AT_PROVIDER_FIXTURE 1u
#define AT_PROVIDER_MAX_RECORDS 262144u
#define AT_PROVIDER_MAX_PACKETS 512u
#ifdef __cplusplus
extern "C" {
#endif
typedef struct AtError { uint32_t size,category; int32_t code; char operation[64],detail[4096]; } AtError;
typedef struct AtCallbacks {
  uint32_t size; void *context;
  void (AT_CALL *diagnostic)(void *,const char *,int32_t,const char *);
  void (AT_CALL *device_state)(void *,uint32_t,uint32_t,const char *);
  int32_t (AT_CALL *cancelled)(void *);
} AtCallbacks;
typedef struct AtConfig {
  uint32_t size,enabled_mask; int32_t logical_devices[2]; const char *runtime_utf8; AtCallbacks callbacks;
} AtConfig;
typedef struct AtRecord {
  uint64_t id,timestamp_ns,device_ticks,host_monotonic_ns;
  uint32_t tick_resolution_ns;
  uint16_t command,status,flags,vendor_status,command2,status2,validity,message_type,word_count,words[32];
  uint8_t channel,bus,source,reserved;
} AtRecord;
typedef struct AtPacket { uint32_t channel,chapter10,size; const uint8_t *data; } AtPacket;
typedef struct AtMetrics {
  uint64_t lost,overflows,channel_lost[2],channel_overflows[2];
  uint64_t poll_ns,packet_processing_ns;
} AtMetrics;
typedef struct AtBatch {
  uint32_t size,record_count,packet_count; const AtRecord *records; const AtPacket *packets;
  AtMetrics metrics; void *release_token;
} AtBatch;
typedef struct AtProviderApi {
  uint32_t size,abi,flags,capabilities;
  const char *provider_id,*sdk_identity,*header_sha256,*runtime_sha256,*qualification;
  int32_t (AT_CALL *open)(const AtConfig *,void **,AtError *);
  int32_t (AT_CALL *update_callbacks)(void *,const AtCallbacks *,AtError *);
  int32_t (AT_CALL *start)(void *,AtError *);
  int32_t (AT_CALL *poll)(void *,AtBatch *,AtError *);
  int32_t (AT_CALL *drain)(void *,AtBatch *,AtError *);
  int32_t (AT_CALL *metrics)(void *,AtMetrics *,AtError *);
  void (AT_CALL *release_batch)(void *,void *);
  int32_t (AT_CALL *close)(void *,AtError *);
} AtProviderApi;
typedef const AtProviderApi *(AT_CALL *AtGetProviderApi)(uint32_t,uint32_t);
AT_EXPORT const AtProviderApi *AT_CALL aerotrace_provider_get_api(uint32_t abi,uint32_t host_size);
#ifdef __cplusplus
}
static_assert(sizeof(void *)==8,"AeroTrace provider ABI is x64 only");
static_assert(sizeof(AtRecord)==128 && offsetof(AtRecord,words)==54,"Provider record layout changed");
#endif
