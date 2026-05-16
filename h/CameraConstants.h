#pragma once

#include <DirectXMath.h>

struct CameraConstants
{
    DirectX::XMFLOAT4X4 mInvViewProj;
    DirectX::XMFLOAT3 mCameraPos;
    float mPadding1;
    DirectX::XMFLOAT2 mScreenSize;
    DirectX::XMFLOAT2 mPadding;

    CameraConstants()
        : mCameraPos(0.0f, 0.0f, 0.0f)
        , mPadding1(0.0f)
        , mScreenSize(800.0f, 600.0f)
        , mPadding(0.0f, 0.0f)
    {
        DirectX::XMStoreFloat4x4(&mInvViewProj, DirectX::XMMatrixIdentity());
    }
};
