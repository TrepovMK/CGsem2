#pragma once

#include <DirectXMath.h>

// Константы пост-прохода. Раскладка обязана совпадать
// с cbuffer PostConstants в src/post_effects.hlsl.
struct PostConstants
{
    DirectX::XMFLOAT2 InvRenderTargetSize = { 1.0f / 800.0f, 1.0f / 600.0f };
    float Time = 0.0f;
    float ChromaticStrength = 0.25f;
    float VignetteStrength = 0.78f;
    float OutlineStrength = 1.0f;
    float OutlineThreshold = 0.05f;
    DirectX::XMFLOAT2 Padding = { 0.0f, 0.0f };
};
