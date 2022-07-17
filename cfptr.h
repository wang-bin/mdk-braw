/*
 * Copyright (c) 2018-2022 WangBin <wbsecg1 at gmail.com>
 * This file is part of MDK
 * MDK SDK: https://github.com/wang-bin/mdk-sdk
 *
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 */
#pragma once
#include <CoreFoundation/CoreFoundation.h>
#include <utility>
namespace apple {
template<typename T = CFTypeRef>
struct cfptr {
    cfptr(const T& t = T()) : val_(t) {}

    cfptr(cfptr&& that) : val_(that.val_) { that.val_ = T();}

    cfptr(const cfptr& that) : val_(that.val_) {
        if (val_)
            CFRetain(val_);
    }

    ~cfptr() {
        if (val_)
            CFRelease(val_);
    }

    operator T() const { return val_; }

    template<typename U>
    operator U() const {return U(val_);}

    explicit operator bool() const {return !!val_;}

    void swap(cfptr& that) noexcept(noexcept(std::swap(val_, that.val_))) { std::swap(val_, that.val_);}

    cfptr& operator=(const cfptr& that) {
        auto tmp(that);
        swap(tmp);
        return *this;
    }

    cfptr& operator=(cfptr&& that) {
        auto tmp(std::move(that));
        swap(tmp);
        return *this;
    }

    template<typename U = T>
    U *operator&() {
        if (val_)
            CFRelease(val_);
        return &val_;
    }

    template<typename U = T>
    const U* const operator&() const { return address(); }

    template<typename U = T>
    const U* address() const { return &val_; }

private:
    T val_;
};
} // namespace apple
