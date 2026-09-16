Texture2D gDiffuseMap : register(t0);
SamplerState gSampler : register(s0);

cbuffer cbPerObject : register(b0)
{
    float4x4 mWorld;
    float4x4 mWorldViewProj;
    float4 mUVTransform;
    float4 mCurtainParams;
};

struct VSInput
{
    float3 Pos : POSITION;
    float3 Normal : NORMAL;
    float2 Tex : TEXCOORD;
};

struct VSOutput
{
    float4 PosH : SV_POSITION;
    float3 WorldPos : POSITION0;
    float3 Normal : NORMAL0;
    float2 TexC : TEXCOORD0;
};

struct PSOutput
{
    float4 Albedo : SV_Target0;
    float4 Normal : SV_Target1;
    float Depth : SV_Target2;
};

VSOutput VS(VSInput vin)
{
    VSOutput vout;

    float pinned = saturate(vin.Tex.y);
    float freeEdge = 1.0f - pinned;
    float wavePhase = vin.Pos.x * mCurtainParams.z + vin.Pos.y * 1.35f + mCurtainParams.x * mCurtainParams.w;
    float secondaryPhase = vin.Pos.z * (mCurtainParams.z * 0.5f) - mCurtainParams.x * (mCurtainParams.w * 0.7f);
    float flutter = sin(wavePhase) + 0.5f * sin(secondaryPhase);

    float3 animatedPos = vin.Pos;
    animatedPos.z += flutter * mCurtainParams.y * freeEdge;
    animatedPos.x += cos(wavePhase * 0.6f) * (mCurtainParams.y * 0.35f) * freeEdge;

    float4 worldPos = mul(float4(animatedPos, 1.0f), mWorld);
    vout.PosH = mul(float4(animatedPos, 1.0f), mWorldViewProj);
    vout.WorldPos = worldPos.xyz;
    vout.Normal = normalize(mul(vin.Normal, (float3x3)mWorld));
    vout.TexC = vin.Tex * mUVTransform.xy + mUVTransform.zw;

    return vout;
}

PSOutput PS(VSOutput pin)
{
    PSOutput pout;
    pout.Albedo = gDiffuseMap.Sample(gSampler, pin.TexC);
    pout.Normal = float4(normalize(pin.Normal), 1.0f);
    pout.Depth = pin.PosH.z;
    return pout;
}

// Проход глубины для каскадных теневых карт: только позиция в light space.
struct ShadowVSInput
{
    float3 Pos : POSITION;
    float3 Normal : NORMAL;
    float2 Tex : TEXCOORD;
};

struct ShadowVSOut
{
    float4 PosH : SV_POSITION;
};

ShadowVSOut VS_Shadow(ShadowVSInput vin)
{
    ShadowVSOut vout;
    vout.PosH = mul(float4(vin.Pos, 1.0f), mWorldViewProj);
    return vout;
}
