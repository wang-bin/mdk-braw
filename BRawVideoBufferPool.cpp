/*
 * Copyright (c) 2022 WangBin <wbsecg1 at gmail.com>
 */
#include "mdk/VideoBuffer.h"
#include "NativeVideoBufferTemplate.h"
#include "BRawGLInterop.h"
#include "BRawD3D11Interop.h"
#include "BRawMTLInterop.h"

using namespace std;

MDK_NS_BEGIN

class BRawVideoBufferPool final : public NativeVideoBufferPool, public BRawGLInterop
#if (__APPLE__ + 0)
    , public BRawMTLInterop
#endif
#if (_WIN32 + 0)
    , public BRawD3D11Interop
#endif
{
public:
    // opaque: BRawVideoBuffers*
    NativeVideoBufferRef getBuffer(void* opaque, std::function<void()> cleanup) override;
    bool transfer_to_host(BRawVideoBuffers bufs, NativeVideoBuffer::MemoryArray* ma, NativeVideoBuffer::MapParameter *mp) {
        if (ma->data[0]) // can be reused
            return true;
        if (!bufs.resMgr)
            return false;
        BlackmagicRawPipeline pipeline;
        void* context = nullptr;
        void* cmdQueue = nullptr;
        bufs.device->GetPipeline(&pipeline, &context, &cmdQueue);
        void* host = nullptr;
        bufs.resMgr->GetResourceHostPointer(context, cmdQueue, bufs.gpuResource, bufs.type, &host);
        if (!host)
            return false;
        VideoFormat fmt = bufs.format;
        mp->format = bufs.format;
        size_t offset = 0;
        for (int i = 0; i < fmt.planeCount(); ++i) {
            ma->data[i] = (uint8_t*)host + offset;
            mp->width[i] = bufs.width;
            mp->height[i] = bufs.height;
            offset += bufs.width * bufs.height;
        }
        return true;
    }

private:
    //friend class BRawVideoVideoBuffer; // access protected interop apis
};

typedef shared_ptr<BRawVideoBufferPool> PoolRef;
using BRawVideoVideoBuffer = NativeVideoBufferImpl<BRawVideoBuffers, BRawVideoBufferPool>;

NativeVideoBufferRef BRawVideoBufferPool::getBuffer(void* opaque, std::function<void()> cleanup)
{
    auto s = (BRawVideoBuffers*)opaque;
    return std::make_shared<BRawVideoVideoBuffer>(static_pointer_cast<BRawVideoBufferPool>(shared_from_this()), *s, cleanup);
}

void register_native_buffer_pool_braw()
{
    NativeVideoBufferPool::registerOnce("BRAW", []()->PoolRef {
        return std::make_shared<BRawVideoBufferPool>();
    });
}
MDK_NS_END