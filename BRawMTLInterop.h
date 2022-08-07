/*
 * Copyright (c) 2022 WangBin <wbsecg1 at gmail.com>
 * This file is part of MDK
 */
#pragma once

#include "BRawVideoBufferPool.h"
#include <memory>

MDK_NS_BEGIN

class BRawMTLInterop
{
public:
    BRawMTLInterop();
    virtual ~BRawMTLInterop();

    bool transfer_begin_mtl(BRawVideoBuffers bufs, NativeVideoBuffer::MetalTextures* ma, NativeVideoBuffer::MapParameter *mp);
    void transfer_end_mtl();

private:
    class Private;
    std::unique_ptr<Private> d;
};

MDK_NS_END