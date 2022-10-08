/*
 * Copyright (c) 2022 WangBin <wbsecg1 at gmail.com>
 */

#pragma once
#include <algorithm>
#include <string>
#include <string.h>
#if (_WIN32 + 0)
# include <oleauto.h>
#endif

class BStr
{
public:
#if (_WIN32 + 0)
    using StrTye = BSTR; // WCHAR*: length prefix + data string + 0x0000
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
        std::snprintf(&cs[0], cs.size() + 1, "%ls", s); // TODO: code page?
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

    BStr(const char* s/*utf8*/) {
        if (!s)
            return;
#if (_WIN32 + 0)
        const auto wlen = MultiByteToWideChar(CP_UTF8, 0, s, strlen(s), nullptr, 0); // including null terminator
        std::wstring ws(wlen, 0);
        MultiByteToWideChar(CP_UTF8, 0, s, strlen(s), &ws[0], ws.size());
        s_ = SysAllocString(ws.data());
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
        return s_;
    }

private:
    void release() {
        if (!s_)
            return;
#if (_WIN32 + 0)
        SysFreeString(s_);
#elif (__APPLE__ + 0)
        CFRelease(s_);
#else
        free(s_);
#endif
        s_ = {};
    }

    StrTye s_{};
};