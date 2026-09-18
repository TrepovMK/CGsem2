Texture2D gSceneTex  : register(t0);  // готовый кадр после lighting
Texture2D gAlbedoTex : register(t1);  //
Texture2D gNormalTex : register(t2);  //
Texture2D gDepthTex  : register(t3);  // g buffer
SamplerState gLinearClamp : register(s0);

cbuffer PostConstants : register(b0)
{
    float2 gInvRenderTargetSize;
    float gTime;
    float gChromaticStrength;
    float gVignetteStrength;
    float3 gPadding;
};

struct VSOut
{
    float4 PosH : SV_POSITION;
    float2 TexC : TEXCOORD0;
};

// Full-screen quad без вершинного буфера: вершины генерируются из SV_VertexID.
// Рисовать вызовом DrawInstanced(4, 1, 0, 0) с топологией TRIANGLESTRIP.
VSOut VS_FullscreenQuad(uint vertexId : SV_VertexID)
{
    float2 pos[4] =
    {
        float2(-1.0f, -1.0f),
        float2(-1.0f,  1.0f),
        float2( 1.0f, -1.0f),
        float2( 1.0f,  1.0f)
    };

    float2 uv[4] =
    {
        float2(0.0f, 1.0f),
        float2(0.0f, 0.0f),
        float2(1.0f, 1.0f),
        float2(1.0f, 0.0f)
    };

    VSOut output;
    output.PosH = float4(pos[vertexId], 0.0f, 1.0f); // куда: угол экрана
    output.TexC = uv[vertexId];  // что читать: место на картинке
    return output;
}

// Заготовка: пиксельный шейдер принимает текстуры G-Buffer
// (gAlbedoTex/gNormalTex/gDepthTex) и сцену после lighting-прохода.
//
// а используются две техники: Chromatic Aberration + Vignette.
float3 ApplyChromaticAberration(float2 uv)
{
    // Радиальное смещение цветовых каналов от центра экрана:
    // красный наружу, синий внутрь, зелёный как есть.
    const float2 dir = uv - 0.5f;
    const float dist2 = dot(dir, dir);
    const float2 offset = dir * dist2 * gChromaticStrength;

    float3 color;
    color.r = gSceneTex.Sample(gLinearClamp, uv + offset).r;
    color.g = gSceneTex.Sample(gLinearClamp, uv).g;
    color.b = gSceneTex.Sample(gLinearClamp, uv - offset).b;
    return color;
}

float3 ApplyVignette(float3 color, float2 uv)
{
    const float2 centeredUv = uv * 2.0f - 1.0f;
    const float vignette = smoothstep(1.15f, 0.18f, dot(centeredUv, centeredUv));
    return color * lerp(1.0f, vignette, gVignetteStrength);
}

float4 PS_PostEffects(VSOut input) : SV_Target
{
    const float2 uv = input.TexC;
    float3 color = ApplyChromaticAberration(uv);

    color = ApplyVignette(color, uv);

    return float4(color, 1.0f);
}
