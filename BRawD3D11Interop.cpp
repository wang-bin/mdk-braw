/*
 * Copyright (c) 2022 WangBin <wbsecg1 at gmail.com>
 * This file is part of MDK
 */
#include "BRawD3D11Interop.h"
#include "ComPtr.h"
#include <d3d11.h>

MDK_NS_BEGIN
using namespace std;
using namespace Microsoft::WRL; //ComPtr

class BRawD3D11Interop::Private
{
public:
};

BRawD3D11Interop::BRawD3D11Interop()
    : d(make_unique<Private>())
{}

BRawD3D11Interop::~BRawD3D11Interop() = default;


struct D3D11Textures : NativeVideoBuffer::D3D11Textures {
    ComPtr<ID3D11Texture2D> sp[3];
};

void* BRawD3D11Interop::transfer_begin_d3d11(BRawVideoBuffers* bufs, NativeVideoBuffer::MapParameter *mp)
{
    return nullptr;
}

void BRawD3D11Interop::transfer_end_d3d11(void* sw11)
{
    if (!sw11)
        return;
    delete static_cast<D3D11Textures*>(sw11);
}

MDK_NS_END