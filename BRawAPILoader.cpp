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
#elif defined(__APPLE__)
# include <CoreFoundation/CoreFoundation.h>
# define dlopen(filename, flags) load_bundle(filename)
# define dlsym(handle, symbol) CFBundleGetFunctionPointerForName(handle, CFSTR(symbol))
# define dlclose(handle) CFRelease(handle)
#else
# include <dlfcn.h>
#endif
#include <iostream>
using namespace std;

#define BRAW_ARG0() (), (), ()
#define BRAW_ARG1(P1) (P1), (P1 p1), (p1)
#define BRAW_ARG2(P1, P2) (P1, P2), (P1 p1, P2 p2), (p1, p2)


#define _BRAW_API(R, NAME, ...) BRAW_API_EXPAND(BRAW_API_EXPAND_T_V(R, NAME, __VA_ARGS__))
#define BRAW_API_EXPAND(EXPR) EXPR
#define BRAW_API_EXPAND_T_V(R, F, ARG_T, ARG_T_V, ARG_V) \
    R F ARG_T_V { \
        static auto fp = (decltype(&F))dlsym(load_once(), #F); \
        assert(fp && "BlackmagicRaw API NOT FOUND: " #F); \
        return fp ARG_V; \
    }

#if (__APPLE__ + 0)
CFBundleRef load_bundle(const char* fwkName)
{
    if (auto m = CFBundleGetMainBundle()) {
        if (auto url = CFBundleCopyPrivateFrameworksURL(m)) {
            auto name = CFStringCreateWithCString(kCFAllocatorDefault, fwkName, kCFStringEncodingUTF8);
            auto fwkUrl = CFURLCreateCopyAppendingPathComponent(kCFAllocatorDefault, url, name, false);
            CFRelease(name);
            CFRelease(url);
            if (fwkUrl) {
                clog << "braw bundle url: " << CFStringGetCStringPtr(CFStringCreateWithFormat(nullptr, nullptr, CFSTR("%@"), (CFTypeRef)fwkUrl), kCFStringEncodingUTF8) << endl;
                auto b = CFBundleCreate(kCFAllocatorDefault, fwkUrl);
                CFRelease(fwkUrl);
                return b;
            }
        }
    }
    return nullptr;
}
#endif

static auto load_once(const char* mod = nullptr)
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
    static auto dso = dlopen(name_default, RTLD_NOW | RTLD_LOCAL);
    if (!dso)
        clog << "Failed to load " << name_default << endl;
    return dso;
}

extern "C" {
_BRAW_API(IBlackmagicRawFactory*, CreateBlackmagicRawFactoryInstance, BRAW_ARG0())
}