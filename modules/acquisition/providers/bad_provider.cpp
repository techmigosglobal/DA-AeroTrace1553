#include "provider_abi.h"
extern "C" AT_EXPORT const AtProviderApi *AT_CALL aerotrace_provider_get_api(uint32_t,uint32_t){static const AtProviderApi bad{sizeof(AtProviderApi),999};return &bad;}
