/*
 * Copyright (c) 2022-2024 WangBin <wbsecg1 at gmail.com>
 */
#include "Variant.h"
#include "BStr.h"
#include <algorithm>
#include <iostream>
#include <iomanip>
#include <sstream>
using namespace std;

#define MS_ENSURE(f, ...) MS_CHECK(f, return __VA_ARGS__;)
#define MS_WARN(f) MS_CHECK(f)
#define MS_CHECK(f, ...)  do { \
        const HRESULT __ms_hr__ = (f); \
        if (FAILED(__ms_hr__)) { \
            std::clog << #f "  ERROR@" << __LINE__ << __FUNCTION__ << ": (" << std::hex << __ms_hr__ << std::dec << ") " << std::error_code(__ms_hr__, std::system_category()).message() << std::endl << std::flush; \
            __VA_ARGS__ \
        } \
    } while (false)

static_assert(sizeof(ScopedVariant) == sizeof(VARIANT), "ScopedVariant size mismatch");
ScopedVariant::ScopedVariant()
{
    VariantInit(this);
}

ScopedVariant::~ScopedVariant()
{
    if (vt == blackmagicRawVariantTypeString) {
        // will be released and reset in VariantClear
    }

    VariantClear(this);
}

ScopedVariant* ScopedVariant::ReleaseAndGetAddressOf()
{
    VariantClear(this);
    return this;
}

string to_string(const VARIANT& v)
{
    stringstream ss;
    switch (v.vt) {
    case blackmagicRawVariantTypeS16:
        ss << v.iVal;
        break;
    case blackmagicRawVariantTypeU16:
        ss << v.uiVal;
        break;
    case blackmagicRawVariantTypeS32:
        ss << v.intVal;
        break;
    case blackmagicRawVariantTypeU32:
        ss << v.uintVal;
        break;
    case blackmagicRawVariantTypeFloat32:
        ss << v.fltVal;
        break;
    case blackmagicRawVariantTypeString:
        return BStr::to_string(v.bstrVal);
    case blackmagicRawVariantTypeSafeArray: {
        auto sa = v.parray;
        void* sad = nullptr;
        MS_ENSURE(SafeArrayAccessData(sa, &sad), {});
        VARTYPE vt;
        MS_ENSURE(SafeArrayGetVartype(sa, &vt), {});
        long lBound = 0;
        MS_ENSURE(SafeArrayGetLBound(sa, 1, &lBound), {});
        long uBound = 0;
        MS_ENSURE(SafeArrayGetUBound(sa, 1, &uBound), {});
        const auto saLen = (uBound - lBound) + 1;
        const auto aLen = saLen > 32 ? 32 : saLen; // ?
        for (long i = 0; i < aLen; ++i) {
            if (i > 0)
                ss << ' ';
            switch (vt) {
            case blackmagicRawVariantTypeU8:
                ss << std::uppercase << std::setw(2) << std::setfill('0') << std::setbase(16) << (int)static_cast<uint8_t*>(sad)[i];
                break;
            case blackmagicRawVariantTypeS16:
                ss << static_cast<int16_t*>(sad)[i];
                break;
            case blackmagicRawVariantTypeU16:
                ss << static_cast<uint16_t*>(sad)[i];
                break;
            case blackmagicRawVariantTypeS32:
                ss << static_cast<int32_t*>(sad)[i];
                break;
            case blackmagicRawVariantTypeU32:
                ss << static_cast<uint32_t*>(sad)[i];
                break;
            case blackmagicRawVariantTypeFloat32:
                ss << static_cast<float*>(sad)[i];
                break;
            }
        }
    }
        break;
    default:
        return {};
    }
    return ss.str();
}

var_ptr make_v(int16_t x)
{
    auto v = make_shared<VARIANT>();
    VariantInit(v.get());
    v->vt = blackmagicRawVariantTypeS16;
    v->iVal = x;
    return v;
}

var_ptr make_v(uint16_t x)
{
    auto v = make_shared<VARIANT>();
    VariantInit(v.get());
    v->vt = blackmagicRawVariantTypeU16;
    v->uiVal = x;
    return v;
}

var_ptr make_v(int32_t x)
{
    auto v = make_shared<VARIANT>();
    VariantInit(v.get());
    v->vt = blackmagicRawVariantTypeS32;
    v->intVal = x;
    return v;
}

var_ptr make_v(uint32_t x)
{
    auto v = make_shared<VARIANT>();
    VariantInit(v.get());
    v->vt = blackmagicRawVariantTypeU32;
    v->uintVal = x;
    return v;
}

var_ptr make_v(float x)
{
    auto v = make_shared<VARIANT>();
    VariantInit(v.get());
    v->vt = blackmagicRawVariantTypeFloat32;
    v->fltVal = x;
    return v;
}

var_ptr make_v(const string& x)
{
    auto bs = new BStr(x.data());
    auto deleter = [bs](VARIANT* p) {
        default_delete<VARIANT>{}(p);
        delete bs;
    };
    auto v = var_ptr(new VARIANT(), deleter);
    VariantInit(v.get());
    v->vt = blackmagicRawVariantTypeString;
    v->bstrVal = bs->get();
    return v;
}