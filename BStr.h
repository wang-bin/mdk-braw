/*
 * Copyright (c) 2022 WangBin <wbsecg1 at gmail.com>
 */

#pragma once
#include <algorithm>
#include <string>
#include <string.h>
#if (_WIN32 + 0)
# include <comutil.h>
#endif

class BStr
{
public:
#if (_WIN32 + 0)
    using StrTye = BSTR; // WCHAR
#elif (__APPLE__ + 0)
    using StrTye = CFStringRef;
#else
    using StrTye = char*;
#endif

    static std::string to_string(StrTye s) {
        if (!s)
            return {};
#if (_WIN32 + 0)
        std::string cs(std::snprintf(nullptr, 0, "%ls", s), 0);
        std::snprintf(&cs[0], cs.size() + 1, "%ls", s);
        return cs;
#elif (__APPLE__ + 0)
        if (auto cs = CFStringGetCStringPtr(s, kCFStringEncodingUTF8))
            return cs;
        const auto utf16length = CFStringGetLength(s);
        const auto maxUtf8len = CFStringGetMaximumSizeForEncoding(utf16length, kCFStringEncodingUTF8);
        std::string u8s(maxUtf8len, 0);
        CFStringGetCString(s, u8s.data(), maxUtf8len, kCFStringEncodingUTF8);
        u8s.resize(strlen(u8s.data()));
        return u8s;
#else
        return s;
#endif
    }

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