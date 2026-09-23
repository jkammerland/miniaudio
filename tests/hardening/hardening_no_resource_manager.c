#if defined(MINIAUDIO_HARDENING_REQUIRE_CXX23)
    #if defined(_MSVC_LANG)
        #define MA_HARDENING_CXX_VERSION _MSVC_LANG
    #elif defined(__cplusplus)
        #define MA_HARDENING_CXX_VERSION __cplusplus
    #else
        #error "The forced C++23 regression must compile as C++."
    #endif

    #if MA_HARDENING_CXX_VERSION < 202302L
        #error "The forced C++23 regression requires a C++23 language mode."
    #endif

    #undef MA_HARDENING_CXX_VERSION
#endif

#define MA_NO_RESOURCE_MANAGER
#define MINIAUDIO_IMPLEMENTATION
#include "../../miniaudio.h"

int main(void)
{
    return ma_version_string() == NULL;
}
