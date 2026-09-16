Texture2D gAlbedoMap : register(t0);
Texture2D gNormalMap : register(t1);
Texture2D gDepthMap : register(t2);
Texture2DArray gShadowMap : register(t3);
SamplerState gSampler : register(s0);
SamplerComparisonState gShadowSampler : register(s1);

cbuffer cbLighting : register(b0)
{
    float3 gLightPos;
    float gLightIntensity;
    float3 gLightColor;
    float gLightRange;
    float3 gLightDir;
    float gSpotAngle;
    float3 gAmbientColor;
    int gLightType;
    float3 gCameraPos;
    float padding;
};

cbuffer cbCamera : register(b1)
{
    float4x4 mInvViewProj;
    float3 mCameraPos;
    float padding1;
    float2 mScreenSize;
    float2 padding2;
};

cbuffer cbShadow : register(b2)
{
    float4x4 gLightViewProj[3];
    float4 gCascadeSplits;
    float4 gLightDirection;
};

static const int kNumCascades = 3;
static const float kShadowMapSize = 1024.0f;

static const int LIGHT_AMBIENT = 0;
static const int LIGHT_DIRECTIONAL = 1;
static const int LIGHT_POINT = 2;
static const int LIGHT_SPOT = 3;

struct VSInput
{
    uint vertexId : SV_VertexID;
};

struct PSInput
{
    float4 PosH : SV_POSITION;
    float2 TexC : TEXCOORD;
};

float3 ReconstructWorldPos(float2 texCoord, float depth, float4x4 invViewProj)
{
    float x = texCoord.x * 2.0f - 1.0f;
    float y = (1.0f - texCoord.y) * 2.0f - 1.0f;
    float4 clipPos = float4(x, y, depth, 1.0f);
    float4 worldPos = mul(clipPos, invViewProj);
    return worldPos.xyz / max(worldPos.w, 1e-5f);
}

// Выбор каскада по линеаризованной глубине + PCF 3x3 через SampleCmp.
float ComputeShadowFactor(float3 worldPos, float3 normalW, float depth)
{
    // Линеаризация глубины (near/far должны совпадать с CPU: 0.1 / 1000).
    const float nearZ = 0.1f;
    const float farZ = 1000.0f;
    float viewDepth = nearZ * farZ / (farZ - depth * (farZ - nearZ));

    int cascadeIndex = kNumCascades - 1;
    for (int i = 0; i < kNumCascades - 1; ++i)
    {
        if (viewDepth < ((i == 0) ? gCascadeSplits.x : ((i == 1) ? gCascadeSplits.y : gCascadeSplits.z)))
        {
            cascadeIndex = i;
            break;
        }
    }

    float4 shadowPos = mul(float4(worldPos, 1.0f), gLightViewProj[cascadeIndex]);
    float3 projCoords = shadowPos.xyz / max(shadowPos.w, 1e-5f);
    projCoords.xy = projCoords.xy * 0.5f + 0.5f;
    projCoords.y = 1.0f - projCoords.y;

    if (projCoords.x < 0.0f || projCoords.x > 1.0f ||
        projCoords.y < 0.0f || projCoords.y > 1.0f ||
        projCoords.z < 0.0f || projCoords.z > 1.0f)
    {
        return 1.0f;
    }

    float3 lightDir = normalize(-gLightDirection.xyz);
    float ndotl = saturate(dot(normalW, lightDir));
    float bias = (0.00005f + (1.0f - ndotl) * 0.00025f) * (1.0f + 0.18f * (float)cascadeIndex);

    float shadow = 0.0f;
    float2 texelSize = 1.0f / kShadowMapSize;
    for (int x = -1; x <= 1; ++x)
    {
        for (int y = -1; y <= 1; ++y)
        {
            float2 offset = float2((float)x, (float)y) * texelSize;
            shadow += gShadowMap.SampleCmpLevelZero(
                gShadowSampler,
                float3(projCoords.xy + offset, (float)cascadeIndex),
                saturate(projCoords.z - bias));
        }
    }
    return shadow / 9.0f;
}

PSInput VS(VSInput vin)
{
    PSInput vout;
    float2 texCoord = float2((vin.vertexId << 1) & 2, vin.vertexId & 2);
    vout.TexC = texCoord;
    vout.PosH = float4(texCoord.x * 2.0f - 1.0f, -(texCoord.y * 2.0f - 1.0f), 0.0f, 1.0f);
    return vout;
}

float4 PS(PSInput pin) : SV_Target
{
    float4 albedo = gAlbedoMap.Sample(gSampler, pin.TexC);
    float3 normal = normalize(gNormalMap.Sample(gSampler, pin.TexC).xyz);
    float depth = gDepthMap.Sample(gSampler, pin.TexC).r;

    if (depth > 0.99999f)
    {
        return float4(0.0f, 0.0f, 0.0f, 0.0f);
    }

    float3 worldPos = ReconstructWorldPos(pin.TexC, depth, mInvViewProj);
    float3 result = float3(0.0f, 0.0f, 0.0f);

    if (gLightType == LIGHT_AMBIENT)
    {
        result = albedo.rgb * gAmbientColor;
    }
    else if (gLightType == LIGHT_DIRECTIONAL)
    {
        float3 lightDir = normalize(-gLightDir);
        float diff = max(dot(normal, lightDir), 0.0f);
        result = diff * gLightColor * gLightIntensity * albedo.rgb;
        // Каскадные тени с PCF только для направленного света.
        result *= ComputeShadowFactor(worldPos, normal, depth);
    }
    else if (gLightType == LIGHT_POINT)
    {
        float3 lightDir = gLightPos - worldPos;
        float distance = length(lightDir);
        lightDir = normalize(lightDir);
        float attenuation = 1.0f - saturate(distance / gLightRange);
        attenuation *= attenuation;
        float diff = max(dot(normal, lightDir), 0.0f);
        result = diff * gLightColor * gLightIntensity * albedo.rgb * attenuation;
    }
    else if (gLightType == LIGHT_SPOT)
    {
        float3 lightDir = gLightPos - worldPos;
        float distance = length(lightDir);
        lightDir = normalize(lightDir);
        float3 spotDir = normalize(gLightDir);
        float cosAngle = dot(lightDir, spotDir);
        float cosCone = cos(gSpotAngle * 0.5f);

        if (cosAngle > cosCone)
        {
            float attenuation = 1.0f - saturate(distance / gLightRange);
            attenuation *= attenuation;
            float spotFactor = saturate((cosAngle - cosCone) / (1.0f - cosCone));
            float diff = max(dot(normal, lightDir), 0.0f);
            result = diff * gLightColor * gLightIntensity * albedo.rgb * attenuation * spotFactor;
        }
    }

    return float4(result, 0.0f);
}
