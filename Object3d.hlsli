// Object3d.hlsli

#define NUM_DIR_LIGHTS 1
#define NUM_POINT_LIGHTS 3
#define NUM_SPOT_LIGHTS 1 

struct VertexShaderOutput
{
    float32_t4 position : SV_POSITION;
    float32_t2 texcoord : TEXCOORD0;
    float32_t3 normal : NORMAL0;
    float32_t3 worldPosition : TEXCOORD1;
};

struct Material
{
    float32_t4 color;
    int32_t enableLighting;
    float32_t shininess;
    float32_t padding[2];
    float32_t4x4 uvTransform;
};

struct InstancingData
{
    float32_t4x4 WVP;
    float32_t4x4 World;
    float32_t4x4 WorldInverseTranspose;
};

// --- ライト構造体 ---

struct DirectionalLight
{
    float32_t4 color; // 16 bytes
    float32_t3 direction; // 12 bytes
    float intensity; // 4 bytes
}; // Total 32 bytes

struct PointLight
{
    float32_t4 color; // 16 bytes
    float32_t3 position; // 12 bytes
    float intensity; // 4 bytes
    float radius; // 4 bytes
    float decay; // 4 bytes
    float32_t padding[2]; // 8 bytes (C++のpadding[2]と合わせる)
}; // Total 48 bytes

struct SpotLight
{
    float32_t4 color; // 16 bytes (offset 0)
    float32_t3 position; // 12 bytes (offset 16)
    float intensity; // 4 bytes  (offset 28)
    float32_t3 direction; // 12 bytes (offset 32)
    float distance; // 4 bytes  (offset 44)
    float decay; // 4 bytes  (offset 48)
    float cosAngle; // 4 bytes  (offset 52)
    float cosFalloffStart; // 4 bytes  (offset 56) ★ここが重要
    float32_t padding; // 4 bytes  (offset 60) ★C++のfloat paddingと合わせる
}; // Total 64 bytes

struct LightGroup
{
    DirectionalLight directionalLights[NUM_DIR_LIGHTS];
    PointLight pointLights[NUM_POINT_LIGHTS];
    SpotLight spotLights[NUM_SPOT_LIGHTS];
};

struct Camera
{
    float32_t3 worldPosition;
};