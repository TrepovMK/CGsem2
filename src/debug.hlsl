Texture2D gTex : register(t0);
SamplerState gSampler : register(s0);

cbuffer cbDebug : register(b0)
{
    uint gMode;
    float gNearZ;
    float gFarZ;
};

struct VSOutput
{
    float4 PosH : SV_POSITION;
    float2 TexC : TEXCOORD;
};

VSOutput VS(uint vertexId : SV_VertexID)
{
    VSOutput vout;
    float2 texCoord = float2((vertexId << 1) & 2, vertexId & 2);
    vout.TexC = texCoord;
    vout.PosH = float4(texCoord.x * 2.0f - 1.0f, -(texCoord.y * 2.0f - 1.0f), 0.0f, 1.0f);
    return vout;
}

float3 HueToRGB(float h)
{
    float r = abs(h * 6.0f - 3.0f) - 1.0f;
    float g = 2.0f - abs(h * 6.0f - 2.0f);
    float b = 2.0f - abs(h * 6.0f - 4.0f);
    return saturate(float3(r, g, b));
}

float4 PS(VSOutput pin) : SV_Target
{
    float4 c = gTex.Sample(gSampler, pin.TexC);

    if (gMode == 1)
    {
        return float4(c.xyz * 0.5f + 0.5f, 1.0f);
    }
    if (gMode == 2)
    {
        float linDepth = (gNearZ * gFarZ) / max(gFarZ - c.r * (gFarZ - gNearZ), 1e-6f);
        float t = (log2(linDepth) - log2(gNearZ)) / (log2(gFarZ) - log2(gNearZ));
        return float4(HueToRGB(saturate(t)), 1.0f);
    }
    return float4(c.xyz, 1.0f);
}