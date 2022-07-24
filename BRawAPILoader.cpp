/*
 * Copyright (c) 2022 WangBin <wbsecg1 at gmail.com>
 */

#include "BlackmagicRawAPI.h"
#include <cassert>
# if __has_include("cppcompat/cstdlib.hpp")
#   include "cppcompat/cstdlib.hpp"
# else
#   include <cstdlib>
# endif
#if defined(_WIN32)
# ifndef UNICODE
#   define UNICODE 1
# endif
# include <windows.h>
# ifdef WINAPI_FAMILY
#  include <winapifamily.h>
#  if !WINAPI_FAMILY_PARTITION(WINAPI_PARTITION_DESKTOP)
#    define BRAW_WINRT 1
#  endif
# endif
# if (BRAW_WINRT+0)
#   define dlopen(filename, flags) LoadPackagedLibrary(filename, 0)
# else
#   define dlopen(filename, flags) LoadLibrary(filename)
# endif
# define dlsym(handle, symbol) GetProcAddress((HMODULE)handle, symbol)
# define dlclose(handle) FreeLibrary((HMODULE)handle)
#else
# include <dlfcn.h>
#endif

#define BRAW_ARG0() (), (), ()
#define BRAW_ARG1(P1) (P1), (P1 p1), (p1)
#define BRAW_ARG2(P1, P2) (P1, P2), (P1 p1, P2 p2), (p1, p2)


#define _BRAW_API(R, NAME, ...) BRAW_API_EXPAND(BRAW_API_EXPAND_T_V(R, NAME, __VA_ARGS__))
#define BRAW_API_EXPAND(EXPR) EXPR
#define BRAW_API_EXPAND_T_V(R, F, ARG_T, ARG_T_V, ARG_V) \
    R F ARG_T_V { \
        static auto fp = (decltype(&F))dlsym(load_BlackmagicRawAPI(), #F); \
        assert(fp && "BlackmagicRaw API NOT FOUND: " #F); \
        return fp ARG_V; \
    }

#if !(__APPLE__ + 0)
static auto load_BlackmagicRawAPI(const char* mod = nullptr)->decltype(dlopen(nullptr, RTLD_LAZY))
{
    const auto name_default =
#if (_WIN32+0)
        TEXT("BlackmagicRawAPI.dll")
#elif (__APPLE__+0)
        "BlackmagicRawAPI.framework"
#else
        "libBlackmagicRawAPI.so"
#endif
        ;
    return dlopen(name_default, RTLD_NOW | RTLD_LOCAL);
}

extern "C" {
_BRAW_API(IBlackmagicRawFactory*, CreateBlackmagicRawFactoryInstance, BRAW_ARG0())
}
#endif