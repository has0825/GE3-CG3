// Object3d.hlsli

// 最大ライト数の定義（C++側と合わせる）
#define NUM_DIR_LIGHTS 1
#define NUM_POINT_LIGHTS 3
#define NUM_SPOT_LIGHTS 3
#define NUM_AREA_LIGHTS 1

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
    int32_t enableLighting; // 0:なし, 1:Lambert, 2:Phong, 3:BlinnPhong
    float32_t shininess; // 光沢度
    float32_t environment; // 環境光係数 (padding)
    float32_t padding;
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
    float padding[2];
};

struct SpotLight
{
    float32_t4 color;
    float32_t3 position;
    float intensity;
    float32_t3 direction;
    float distance;
    float decay;
    float cosAngle;
    float cosFalloffStart;
    float padding[2];
};

struct AreaLight
{
    float32_t4 color;
    float32_t3 position;
    float intensity;
    float32_t3 up;
    float height;
    float32_t3 right;
    float width;
    float32_t3 direction;
    float decay;
};

// 定数バッファ構造体
struct LightGroup
{
    DirectionalLight directionalLights[NUM_DIR_LIGHTS];
    PointLight pointLights[NUM_POINT_LIGHTS];
    SpotLight spotLights[NUM_SPOT_LIGHTS];
    AreaLight areaLights[NUM_AREA_LIGHTS];

    int numDirectionalLights;
    int numPointLights;
    int numSpotLights;
    int numAreaLights;
};

struct Camera
{
    float32_t3 worldPosition;
};