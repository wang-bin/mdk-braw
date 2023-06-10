/*
 * Copyright (c) 2022 WangBin <wbsecg1 at gmail.com>
 * This file is part of MDK
 */
#pragma once
#include "mdk/Buffer.h"
#include "mdk/VideoBuffer.h"
#include "BlackmagicRawAPI.h"

MDK_NS_BEGIN

enum class PixelFormat;
struct BRawVideoBuffers
{
    int width;
    int height;
    PixelFormat format;
    IBlackmagicRawProcessedImage *image;
    uint32_t bytes;
    void* gpuResource;
    BlackmagicRawResourceType type;
    IBlackmagicRawPipelineDevice* device; // owned. must Release() in cleanup
    IBlackmagicRawResourceManager* resMgr; // for host map
};

MDK_NS_END