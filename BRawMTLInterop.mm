/*
 * Copyright (c) 2022-2023 WangBin <wbsecg1 at gmail.com>
 * This file is part of MDK
 */
#include "BRawMTLInterop.h"
#include "BlackmagicRawAPI.h"
#include <TargetConditionals.h>
#import <Metal/Metal.h>

MDK_NS_BEGIN
using namespace std;

class BRawMTLInterop::Private
{
public:
};

BRawMTLInterop::BRawMTLInterop()
    : d(make_unique<Private>())
{}

BRawMTLInterop::~BRawMTLInterop() = default;

bool BRawMTLInterop::transfer_begin_mtl(BRawVideoBuffers bufs, NativeVideoBuffer::MetalTextures* ma, NativeVideoBuffer::MapParameter *mp)
{
    if (ma->tex[0])
        return true;
    auto dev = bufs.device;
    BlackmagicRawPipeline pipeline;
    void* context = nullptr;
    void* cmdQueue = nullptr;
    dev->GetPipeline(&pipeline, &context, &cmdQueue);

    auto mtlBuf = (__bridge id<MTLBuffer>)bufs.gpuResource;

    const VideoFormat fmt = bufs.format;
    const int planes = fmt.planeCount();
    size_t offset = 0;
    MTLPixelFormat fmts[3]{};
    switch (bufs.format) {
    case PixelFormat::RGBA:
        fmts[0] = MTLPixelFormatRGBA8Unorm;
        break;
    case PixelFormat::BGRA:
        fmts[0] = MTLPixelFormatBGRA8Unorm;
        break;
    case PixelFormat::RGBA64LE:
    case PixelFormat::BGRA64LE:
        fmts[0] = MTLPixelFormatRGBA16Unorm;
        break;
    case PixelFormat::RGBP16LE:
        fmts[0] = fmts[1] = fmts[2] = MTLPixelFormatR16Unorm;
        break;
    case PixelFormat::RGBPF32LE:
        fmts[0] = fmts[1] = fmts[2] = MTLPixelFormatR32Float;
        break;
    case PixelFormat::BGRAF32LE:
        fmts[0] = MTLPixelFormatRGBA32Float;
        break;
    default:
        break;
    }
    //Metal::from(fmt, fmts);
    for (int i = 0; i < planes; ++i) {
        auto desc = [[MTLTextureDescriptor alloc] init];
        desc.textureType = MTLTextureType2D;
        desc.pixelFormat = fmts[i];
        desc.width = bufs.width;
        desc.height = bufs.height;
        desc.mipmapLevelCount = 1;
        desc.resourceOptions = MTLResourceStorageModePrivate;
        desc.storageMode = mtlBuf.storageMode;
        desc.usage = MTLTextureUsageShaderRead | MTLTextureUsageRenderTarget;
        const id<MTLTexture> tex = [mtlBuf newTextureWithDescriptor:desc offset:offset bytesPerRow:fmt.bytesPerLine(bufs.width, i)];
        mp->width[i] = bufs.width;
        mp->height[i] = bufs.height;
        ma->tex[i] = (__bridge_retained void*)tex;
        offset += bufs.width * bufs.height;
        //tex0 = tex;
    }

    return true;
}

void BRawMTLInterop::transfer_end_mtl()
{
}

MDK_NS_END