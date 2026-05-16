#pragma once

#include <DirectXMath.h>

struct ObjectConstants
{
    DirectX::XMFLOAT4X4 mWorld;
    DirectX::XMFLOAT4X4 mWorldViewProj;
    DirectX::XMFLOAT4 mUVTransform;
    DirectX::XMFLOAT4 mCurtainParams;

    ObjectConstants()
        : mUVTransform(1.0f, 1.0f, 0.0f, 0.0f)
        , mCurtainParams(0.0f, 0.08f, 3.5f, 1.8f)
    {
        DirectX::XMStoreFloat4x4(&mWorld, DirectX::XMMatrixIdentity());
        DirectX::XMStoreFloat4x4(&mWorldViewProj, DirectX::XMMatrixIdentity());
    }
};
