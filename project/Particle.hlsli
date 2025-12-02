// 標準的な型名を使えるようにする定義
typedef float4 float32_t4;
typedef float3 float32_t3;
typedef float2 float32_t2;
typedef float4x4 float32_t4x4;
typedef int int32_t;

// 共通の構造体定義
struct VertexShaderOutput
{
    float32_t4 position : SV_POSITION;
    float32_t2 texcoord : TEXCOORD0;
    float32_t3 normal : NORMAL0;
    float32_t4 color : COLOR0;
};

// 頂点シェーダー用
struct ParticleForGPU
{
    float32_t4x4 WVP;
    float32_t4x4 World;
    float32_t4 color;
};

// ピクセルシェーダー用
struct Material
{
    float32_t4 color;
    int32_t enableLighting;
    float32_t4x4 uvTransform;
};