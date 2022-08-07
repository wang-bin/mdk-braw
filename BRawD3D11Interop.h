/*
 * Copyright (c) 2022 WangBin <wbsecg1 at gmail.com>
 */
#pragma once

#include "BRawVideoBufferPool.h"
#include <memory>

MDK_NS_BEGIN

class BRawD3D11Interop
{
public:
    BRawD3D11Interop();
    virtual ~BRawD3D11Interop();

    void* transfer_begin_d3d11(BRawVideoBuffers* sw, NativeVideoBuffer::MapParameter *mp);
    void transfer_end_d3d11(void*);

private:
    class Private;
    std::unique_ptr<Private> d;
};

MDK_NS_END