/*
 * Copyright (c) 2022 WangBin <wbsecg1 at gmail.com>
 */
#include "BRawGLInterop.h"
#include "ComPtr.h"
using namespace Microsoft::WRL; //ComPtr

using namespace std;

MDK_NS_BEGIN

class BRawGLInterop::Private
{
public:
};

BRawGLInterop::BRawGLInterop()
    : d(make_unique<Private>())
{}

BRawGLInterop::~BRawGLInterop() = default;

bool BRawGLInterop::transfer_begin(BRawVideoBuffers bufs, NativeVideoBuffer::GLTextureArray* ma, NativeVideoBuffer::MapParameter *mp)
{
    // no gl context sharing, so texture from IBlackmagicRawOpenGLInteropHelper won't work
    // TODO: IBlackmagicRawConfiguration->SetPipeline() with cl_context sharing with renderer's gl context, then IBlackmagicRawOpenGLInteropHelper or pure cl/gl interop will work
    return false;
    //auto dev = bufs.device;
    //ComPtr<IBlackmagicRawOpenGLInteropHelper> interop;
    //MS_ENSURE(dev->GetOpenGLInteropHelper(&interop), false);
    //MS_ENSURE(interop->SetImage(bufs.image, &ma->id[0], &ma->target), false);
    //clog << "ma->id[0] " << ma->id[0] << " ma->target" << std::hex << ma->target << std::dec << endl;
    ////mp->format = GL_RGBA;
    //return true;
}
MDK_NS_END