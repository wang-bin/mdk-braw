/*
 * Copyright (c) 2022-2024 WangBin <wbsecg1 at gmail.com>
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
# define dlsym(handle, symbol) ((handle) ? CFBundleGetFunctionPointerForName(handle, CFSTR(symbol)) : nullptr)
# define dlclose(handle) CFRelease(handle)
#else
# include <dlfcn.h>
#endif
#include <iostream>
using namespace std;

#define BRAW_ARG0() (), (), ()
#define BRAW_ARG1(P1) (P1), (P1 p1), (p1)
#define BRAW_ARG2(P1, P2) (P1, P2), (P1 p1, P2 p2), (p1, p2)
#define BRAW_ARG3(P1, P2, P3) (P1, P2, P3), (P1 p1, P2 p2, P3 p3), (p1, p2, p3)


#define _BRAW_API(R, NAME, ...) BRAW_API_EXPAND(BRAW_API_EXPAND_T_V(R, NAME, __VA_ARGS__))
#define BRAW_API_EXPAND(EXPR) EXPR
#define BRAW_API_EXPAND_T_V(R, F, ARG_T, ARG_T_V, ARG_V) \
    R F ARG_T_V { \
        static auto fp = (decltype(&F))dlsym(load_once(), #F); \
        if (!fp) \
            return default_rv<R>(); \
        return fp ARG_V; \
    }

template<typename T> static T default_rv() {return {};}
template<> void default_rv<void>() {}

#if (__APPLE__ + 0)
static CFBundleRef load_bundle(const char* fwkName)
{
    if (auto m = CFBundleGetMainBundle()) {
        if (auto url = CFBundleCopyBundleURL(m)) {
            if (auto ext = CFURLCopyPathExtension(url); ext) {
                if (CFStringCompare(ext, CFSTR("appex"), 0) == kCFCompareEqualTo) {
// MY_APP.app/PlugIns/MY_APP_EXTENSION.appex
// appex is sandboxed, so main app/framework MUST link to fwkName if current module is dynamic loaded
                    if (auto appUrl = CFURLCreateCopyDeletingLastPathComponent(kCFAllocatorDefault, url); appUrl) {
                        CFRelease(url);
                        url = appUrl;
                        appUrl = CFURLCreateCopyDeletingLastPathComponent(kCFAllocatorDefault, url);
                        CFRelease(url);
                        url = appUrl;
                        if (auto appM = CFBundleCreate(kCFAllocatorDefault, url); appM) {
                            CFRelease(m);
                            m = appM;
                        }
                    }
                }
                CFRelease(ext);
            }
            CFRelease(url);
        }
        if (auto url = CFBundleCopyPrivateFrameworksURL(m)) {
            auto name = CFStringCreateWithCString(kCFAllocatorDefault, fwkName, kCFStringEncodingUTF8);
            auto fwkUrl = CFURLCreateCopyAppendingPathComponent(kCFAllocatorDefault, url, name, false);
            CFRelease(name);
            CFRelease(url);
            if (fwkUrl) {
                auto s = CFStringCreateWithFormat(nullptr, nullptr, CFSTR("%@"), (CFTypeRef)fwkUrl);
                clog << "braw bundle url: " << CFStringGetCStringPtr(s, kCFStringEncodingUTF8) << endl;
                CFRelease(s);
                auto b = CFBundleCreate(kCFAllocatorDefault, fwkUrl);
                CFRelease(fwkUrl);
                return b;
            }
        }
    }
    return nullptr;
}
#endif

inline string to_string(const wchar_t* ws)
{
    string s(snprintf(nullptr, 0, "%ls", ws), 0);
    snprintf(&s[0], s.size() + 1, "%ls", ws);
    return s;
}

inline string to_string(const char* s) { return s;}

static auto load_once()
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
        clog << "Failed to load BRAW runtime: " << to_string(name_default) << endl;
    return dso;
}

extern "C" {
_BRAW_API(IBlackmagicRawFactory*, CreateBlackmagicRawFactoryInstance, BRAW_ARG0())
#ifndef _WIN32
_BRAW_API(HRESULT, VariantInit, BRAW_ARG1(Variant*))
_BRAW_API(HRESULT, VariantClear, BRAW_ARG1(Variant*))
_BRAW_API(SafeArray*, SafeArrayCreate, BRAW_ARG3(BlackmagicRawVariantType, uint32_t, SafeArrayBound*))
_BRAW_API(HRESULT, SafeArrayGetVartype, BRAW_ARG2(SafeArray*, BlackmagicRawVariantType*))
_BRAW_API(HRESULT, SafeArrayGetLBound, BRAW_ARG3(SafeArray*, uint32_t, long*))
_BRAW_API(HRESULT, SafeArrayGetUBound, BRAW_ARG3(SafeArray*, uint32_t, long*))
_BRAW_API(HRESULT, SafeArrayAccessData, BRAW_ARG2(SafeArray*, void**))
_BRAW_API(HRESULT, SafeArrayUnaccessData, BRAW_ARG1(SafeArray*))
_BRAW_API(HRESULT, SafeArrayDestroy, BRAW_ARG1(SafeArray*))
#endif
}