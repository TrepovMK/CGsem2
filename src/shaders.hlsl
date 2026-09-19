Texture2D gDiffuseMap : register(t0);
Texture2D gNormalMap : register(t1);
Texture2D gDisplacementMap : register(t2);
SamplerState gLinearWrap : register(s0);

cbuffer ObjectConstants : register(b0)
{
    float4x4 gWorldViewProj;
    float4x4 gWorld;
    float4x4 gTextureTransform;
    float gTotalTime;
    float3 gObjectPadding;
};

cbuffer PassConstants : register(b1)
{
    float4x4 gInvViewProj;
    float3 gEyePosW;
    float gPassPadding;
    float4 gAmbientColor;
};

struct VSInput
{
    float3 PosL : POSITION;
    float3 NormalL : NORMAL;
    float3 TangentL : TANGENT;
    float3 BitangentL : BINORMAL;
    float2 TexC : TEXCOORD;
};

struct PixelIn
{
    float4 PosH : SV_POSITION;
    float3 PosW : POSITION;
    float3 NormalW : NORMAL;
    float3 TangentW : TANGENT;
    float3 BitangentW : BINORMAL;
    float2 TexC : TEXCOORD;
    float TessFactor : TEXCOORD1;
};

PixelIn VS_Geometry(VSInput vin)
{
    PixelIn vout;

    float4 posW = mul(float4(vin.PosL, 1.0f), gWorld);
    vout.PosW = posW.xyz;

    float3x3 world3x3 = (float3x3)gWorld;
    vout.NormalW = normalize(mul(vin.NormalL, world3x3));
    vout.TangentW = normalize(mul(vin.TangentL, world3x3));
    vout.BitangentW = normalize(mul(vin.BitangentL, world3x3));

    float4 tex = mul(float4(vin.TexC, 0.0f, 1.0f), gTextureTransform);
    vout.TexC = tex.xy;
    vout.PosH = mul(float4(vout.PosW, 1.0f), gWorldViewProj);
    vout.TessFactor = 1.0f;

    return vout;
}

struct ControlPoint
{
    float3 PosW : POSITION;
    float3 NormalW : NORMAL;
    float3 TangentW : TANGENT;
    float3 BitangentW : BINORMAL;
    float2 TexC : TEXCOORD;
};

ControlPoint VS_ControlPoint(VSInput vin)
{
    ControlPoint cp;

    float4 posW = mul(float4(vin.PosL, 1.0f), gWorld);
    cp.PosW = posW.xyz;

    float3x3 world3x3 = (float3x3)gWorld;
    cp.NormalW = normalize(mul(vin.NormalL, world3x3));
    cp.TangentW = normalize(mul(vin.TangentL, world3x3));
    cp.BitangentW = normalize(mul(vin.BitangentL, world3x3));

    float4 tex = mul(float4(vin.TexC, 0.0f, 1.0f), gTextureTransform);
    cp.TexC = tex.xy;

    return cp;
}

struct HSConstants
{
    float EdgeTess[3] : SV_TessFactor;
    float InsideTess : SV_InsideTessFactor;
};

HSConstants HS_Constants(InputPatch<ControlPoint, 3> patch, uint patchId : SV_PrimitiveID)
{
    HSConstants hsc;

    float3 center = (patch[0].PosW + patch[1].PosW + patch[2].PosW) / 3.0f;
    float distanceToCamera = length(center - gEyePosW);

    float tessFactor = max(1.0, min(8.0, lerp(8.0, 1.0, saturate((distanceToCamera - 0.2) / 1.0))));
    if (gObjectPadding.z > 0.0f)
    {
        tessFactor = max(tessFactor, gObjectPadding.z);
    }

    hsc.EdgeTess[0] = tessFactor;
    hsc.EdgeTess[1] = tessFactor;
    hsc.EdgeTess[2] = tessFactor;
    hsc.InsideTess = tessFactor;

    return hsc;
}

[domain("tri")]
[partitioning("fractional_odd")]
[outputtopology("triangle_cw")]
[outputcontrolpoints(3)]
[patchconstantfunc("HS_Constants")]
ControlPoint HS_Main(InputPatch<ControlPoint, 3> patch, uint cpId : SV_OutputControlPointID)
{
    return patch[cpId];
}

[domain("tri")]
PixelIn DS_Main(HSConstants hsc, float3 bary : SV_DomainLocation, const OutputPatch<ControlPoint, 3> patch)
{
    PixelIn outV;

    float3 posW = bary.x * patch[0].PosW + bary.y * patch[1].PosW + bary.z * patch[2].PosW;
    float3 normalW = normalize(bary.x * patch[0].NormalW + bary.y * patch[1].NormalW + bary.z * patch[2].NormalW);
    float3 tangentW = normalize(bary.x * patch[0].TangentW + bary.y * patch[1].TangentW + bary.z * patch[2].TangentW);
    float3 bitangentW = normalize(bary.x * patch[0].BitangentW + bary.y * patch[1].BitangentW + bary.z * patch[2].BitangentW);
    float2 texC = bary.x * patch[0].TexC + bary.y * patch[1].TexC + bary.z * patch[2].TexC;

    float height = gDisplacementMap.SampleLevel(gLinearWrap, texC, 0.0f).r;
    float displacement = (height - 0.5f) * 2.0f;
    float dispStrength = (gObjectPadding.y > 0.0f) ? gObjectPadding.y : 0.2f;

    posW += normalW * (displacement * dispStrength);

    outV.PosW = posW;
    outV.NormalW = normalW;
    outV.TangentW = tangentW;
    outV.BitangentW = bitangentW;
    outV.TexC = texC;
    outV.PosH = mul(float4(posW, 1.0f), gWorldViewProj);
    outV.TessFactor = hsc.InsideTess;

    return outV;
}

struct GBufferOut
{
    float4 Albedo : SV_Target0;
    float4 Normal : SV_Target1;
    float Depth : SV_Target2;
};

GBufferOut PS_Geometry(PixelIn pin)
{
    GBufferOut pout;

    float4 albedo = gDiffuseMap.Sample(gLinearWrap, pin.TexC);
    float3 normalTS = gNormalMap.Sample(gLinearWrap, pin.TexC).xyz * 2.0f - 1.0f;

    float3 T = normalize(pin.TangentW);
    float3 B = normalize(pin.BitangentW);
    float3 N = normalize(pin.NormalW);

    float3x3 TBN = float3x3(T, B, N);
    float3 normalW = normalize(mul(normalTS, TBN));

    if (gObjectPadding.x >= 1.5f && gObjectPadding.x < 2.5f)
    {
        albedo = float4(normalW * 0.5f + 0.5f, 1.0f);
    }
    else if (gObjectPadding.x >= 2.5f)
    {
        float t = saturate((pin.TessFactor - 1.0f) / 7.0f);
        float3 tessColor = lerp(float3(0.1f, 0.2f, 1.0f), float3(1.0f, 0.1f, 0.05f), t);
        albedo = float4(tessColor, 1.0f);
    }

    pout.Albedo = albedo;
    pout.Normal = float4(normalW * 0.5f + 0.5f, 1.0f);
    pout.Depth = pin.PosH.z;

    return pout;
}

// ---- Debug lines for minimap (top-down view: frustum, object boxes, octree) ----
cbuffer DebugLineConstants : register(b0)
{
    float4x4 gDebugViewProj;
};

struct DebugVSInput
{
    float3 PosL : POSITION;
    float3 Color : COLOR;
};

struct DebugPSInput
{
    float4 PosH : SV_POSITION;
    float3 Color : COLOR;
};

DebugPSInput VS_DebugLine(DebugVSInput vin)
{
    DebugPSInput vout;
    vout.PosH = mul(float4(vin.PosL, 1.0f), gDebugViewProj);
    vout.Color = vin.Color;
    return vout;
}

float4 PS_DebugLine(DebugPSInput pin) : SV_TARGET
{
    return float4(pin.Color, 1.0f);
}
