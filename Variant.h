/*
 * Copyright (c) 2022-2024 WangBin <wbsecg1 at gmail.com>
 */
#pragma once
#include "BlackmagicRawAPI.h"
#include <memory>
#include <string>

class ScopedVariant : public VARIANT
{
public:
    ScopedVariant();
    ~ScopedVariant();

    ScopedVariant* ReleaseAndGetAddressOf();
    ScopedVariant* operator&() {
        return ReleaseAndGetAddressOf();
    }
};

std::string to_string(const VARIANT& v);

using var_ptr = std::shared_ptr<VARIANT>;
var_ptr make_v(int16_t x);
var_ptr make_v(uint16_t x);
var_ptr make_v(int32_t x);
var_ptr make_v(uint32_t x);
var_ptr make_v(float x);
var_ptr make_v(const std::string& x);
