#pragma once

#include <DirectXMath.h>

struct ObjectConstants
{
    DirectX::XMFLOAT4X4 WorldViewProj = {};
    DirectX::XMFLOAT4X4 World = {};
    DirectX::XMFLOAT4X4 TextureTransform = {};
    float TotalTime = 0.0f;
    DirectX::XMFLOAT3 Padding = {0.0f, 0.0f, 0.0f};
};
