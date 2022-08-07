/*
 * Copyright (c) 2022 WangBin <wbsecg1 at gmail.com>
 */
#pragma once

#include "BRawVideoBufferPool.h"
#include <memory>

MDK_NS_BEGIN

class BRawGLInterop
{
public:
    BRawGLInterop();
    virtual ~BRawGLInterop();

    bool transfer_begin(BRawVideoBuffers bufs, NativeVideoBuffer::GLTextureArray* ma, NativeVideoBuffer::MapParameter *mp);
    void transfer_end() {}

private:
    class Private;
    std::unique_ptr<Private> d;
};

MDK_NS_END