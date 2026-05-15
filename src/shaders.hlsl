Texture2D gDiffuseMap : register(t0);
SamplerState gSampler : register(s0);

cbuffer cbPerObject : register(b0)
{
    float4x4 mWorldViewProj;
    float4 mUVTransform;  // x = scaleU, y = scaleV, z = offsetU, w = offsetV
    float4 mCurtainParams; // x = time, y = amplitude, z = spatial frequency, w = speed
};

struct VSInput
{
    float3 Pos : POSITION;
    float3 Normal : NORMAL;
    float2 Tex : TEXCOORD;
};

struct PSInput
{
    float4 PosH : SV_POSITION;
    float2 TexC : TEXCOORD;
};

PSInput VS(VSInput vin)
{
    PSInput vout;

    float pinned = saturate(vin.Tex.y);
    float freeEdge = 1.0f - pinned;
    float wavePhase = vin.Pos.x * mCurtainParams.z + vin.Pos.y * 1.35f + mCurtainParams.x * mCurtainParams.w;
    float secondaryPhase = vin.Pos.z * (mCurtainParams.z * 0.5f) - mCurtainParams.x * (mCurtainParams.w * 0.7f);
    float flutter = sin(wavePhase) + 0.5f * sin(secondaryPhase);

    float3 animatedPos = vin.Pos;
    animatedPos.z += flutter * mCurtainParams.y * freeEdge;
    animatedPos.x += cos(wavePhase * 0.6f) * (mCurtainParams.y * 0.35f) * freeEdge;

    vout.PosH = mul(float4(animatedPos, 1.0f), mWorldViewProj);

    vout.TexC = vin.Tex * mUVTransform.xy + mUVTransform.zw;

    return vout;
}

float4 PS(PSInput pin) : SV_Target
{
    float4 tex = gDiffuseMap.Sample(gSampler, pin.TexC);


    return tex;
}
