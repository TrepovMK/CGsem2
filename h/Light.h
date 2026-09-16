#pragma once

#include <DirectXMath.h>

using namespace DirectX;

enum LightType
{
    LIGHT_AMBIENT = 0,
    LIGHT_DIRECTIONAL = 1,
    LIGHT_POINT = 2,
    LIGHT_SPOT = 3
};

struct Light
{
    LightType Type = LIGHT_POINT;
    XMFLOAT3 Position = XMFLOAT3(0.0f, 0.0f, 0.0f);
    XMFLOAT3 Color = XMFLOAT3(1.0f, 1.0f, 1.0f);
    float Intensity = 1.0f;
    XMFLOAT3 Direction = XMFLOAT3(0.0f, -1.0f, 0.0f);
    float Range = 10.0f;
    float SpotAngle = XM_PIDIV4;
    float SpotFalloff = 1.0f;
    XMFLOAT3 AmbientColor = XMFLOAT3(0.2f, 0.2f, 0.2f);

    static Light CreateAmbientLight(const XMFLOAT3& color)
    {
        Light light;
        light.Type = LIGHT_AMBIENT;
        light.AmbientColor = color;
        return light;
    }

    static Light CreateDirectionalLight(const XMFLOAT3& dir, const XMFLOAT3& color, float intensity)
    {
        Light light;
        light.Type = LIGHT_DIRECTIONAL;
        light.Direction = dir;
        light.Color = color;
        light.Intensity = intensity;
        return light;
    }

    static Light CreatePointLight(const XMFLOAT3& pos, const XMFLOAT3& color, float intensity, float range)
    {
        Light light;
        light.Type = LIGHT_POINT;
        light.Position = pos;
        light.Color = color;
        light.Intensity = intensity;
        light.Range = range;
        return light;
    }

    static Light CreateSpotLight(
        const XMFLOAT3& pos,
        const XMFLOAT3& dir,
        const XMFLOAT3& color,
        float intensity,
        float range,
        float angle)
    {
        Light light;
        light.Type = LIGHT_SPOT;
        light.Position = pos;
        light.Direction = dir;
        light.Color = color;
        light.Intensity = intensity;
        light.Range = range;
        light.SpotAngle = angle;
        return light;
    }
};

struct LightConstants
{
    XMFLOAT3 LightPos;
    float LightIntensity;
    XMFLOAT3 LightColor;
    float LightRange;
    XMFLOAT3 LightDir;
    float SpotAngle;
    XMFLOAT3 AmbientColor;
    int LightTypeValue;
    XMFLOAT3 CameraPos;
    float Padding;

    LightConstants()
        : LightPos(0.0f, 0.0f, 0.0f)
        , LightIntensity(1.0f)
        , LightColor(1.0f, 1.0f, 1.0f)
        , LightRange(10.0f)
        , LightDir(0.0f, -1.0f, 0.0f)
        , SpotAngle(XM_PIDIV4)
        , AmbientColor(0.2f, 0.2f, 0.2f)
        , LightTypeValue(0)
        , CameraPos(0.0f, 0.0f, 0.0f)
        , Padding(0.0f)
    {
    }

    void SetFromLight(const Light& light, const XMFLOAT3& cameraPos)
    {
        LightPos = light.Position;
        LightIntensity = light.Intensity;
        LightColor = light.Color;
        LightRange = light.Range;
        LightDir = light.Direction;
        SpotAngle = light.SpotAngle;
        AmbientColor = light.AmbientColor;
        LightTypeValue = static_cast<int>(light.Type);
        CameraPos = cameraPos;
    }
};

// Каскадные теневые карты (как в KG_Sem4_Laba5).
static constexpr unsigned int kNumCascades = 3;
static constexpr unsigned int kShadowMapSize = 1024;

struct ShadowConstants
{
    XMFLOAT4X4 LightViewProj[kNumCascades];
    // Дистанции сплитов каскадов во view-space (x, y, z; w не используется).
    XMFLOAT4 CascadeSplits = XMFLOAT4(0.0f, 0.0f, 0.0f, 0.0f);
    XMFLOAT4 LightDirection = XMFLOAT4(0.0f, -1.0f, 0.0f, 0.0f);
};
