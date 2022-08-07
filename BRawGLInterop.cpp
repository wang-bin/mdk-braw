/*
 * Copyright (c) 2022 WangBin <wbsecg1 at gmail.com>
 */
#include "BRawGLInterop.h"

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
    return false;
}
MDK_NS_END