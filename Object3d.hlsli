// Object3d.hlsli

#define NUM_DIR_LIGHTS 1
#define NUM_POINT_LIGHTS 3
#define NUM_SPOT_LIGHTS 1 // 追加

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
    float32_t4 color;
    float32_t3 direction;
    float intensity;
};

struct PointLight
{
    float32_t4 color;
    float32_t3 position;
    float intensity;
    float radius;
    float decay;
    float32_t padding[2]; // C++と合わせる
};

struct SpotLight
{
    float32_t4 color;
    float32_t3 position;
    float32_t intensity;
    float32_t3 direction;
    float32_t distance;
    float32_t decay;
    float32_t cosAngle;
    float32_t cosFalloffStart; // 追加
};

struct LightGroup
{
    DirectionalLight directionalLights[NUM_DIR_LIGHTS];
    PointLight pointLights[NUM_POINT_LIGHTS];
    SpotLight spotLights[NUM_SPOT_LIGHTS]; // 追加
    int numDirectionalLights;
    int numPointLights;
    int numSpotLights; // 追加
    float padding;
};

struct Camera
{
    float32_t3 worldPosition;
};