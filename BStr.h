/*
 * Copyright (c) 2022 WangBin <wbsecg1 at gmail.com>
 */

#pragma once
#include <algorithm>
#include <string.h>
#if (_WIN32 + 0)
# include <comutil.h>
#endif

class BStr
{
public:
#if (_WIN32 + 0)
    using StrTye = BSTR;
#elif (__APPLE__ + 0)
    using StrTye = CFStringRef;
#else
    using StrTye = char*;
#endif

    BStr(const char* s) {
        if (!s)
            return;
#if (_WIN32 + 0)
        s_ = s;
#elif (__APPLE__ + 0)
        s_ = CFStringCreateWithCString(nullptr, s, kCFStringEncodingUTF8);
#else
        s_ = strdup(s);
#endif
    }

    BStr(const BStr&) = delete;
    BStr& operator=(const BStr&) = delete;

    BStr(BStr&& other) {
        std::swap(s_, other.s_);
    }

    BStr& operator=(BStr&& other) {
        std::swap(s_, other.s_);
        return *this;
    }

    ~BStr() {
        release();
    }

    StrTye get() {
#if (_WIN32 + 0)
        return s_.GetBSTR();
#else
        return s_;
#endif
    }

    StrTye *operator&() {
#if (_WIN32 + 0)
        return s_.GetAddress();
#else
        release();
        return &s_;
#endif
    }

private:
    void release() {
        if (!s_)
            return;
#if (_WIN32 + 0)
        s_ = _bstr_t();
#elif (__APPLE__ + 0)
        CFRelease(s_);
#else
        free(s_);
#endif
    }

#if (_WIN32 + 0)
    _bstr_t s_;
#else
    StrTye s_{};
#endif
};