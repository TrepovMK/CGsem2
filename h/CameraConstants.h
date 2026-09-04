#pragma once

#include <DirectXMath.h>

struct PassConstants
{
    DirectX::XMFLOAT4X4 InvViewProj = {};
    DirectX::XMFLOAT3 EyePosW = {0.0f, 0.0f, 0.0f};
    float Padding = 0.0f;
    DirectX::XMFLOAT4 AmbientColor = {0.08f, 0.08f, 0.1f, 1.0f};
};
