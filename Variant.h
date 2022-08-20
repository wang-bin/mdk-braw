#pragma once
#include "BlackmagicRawAPI.h"
#include <memory>
#include <string>

std::string to_string(const VARIANT& v);

using var_ptr = std::shared_ptr<VARIANT>;
var_ptr make_v(int16_t x);
var_ptr make_v(uint16_t x);
var_ptr make_v(int32_t x);
var_ptr make_v(uint32_t x);
var_ptr make_v(float x);
var_ptr make_v(const std::string& x);
