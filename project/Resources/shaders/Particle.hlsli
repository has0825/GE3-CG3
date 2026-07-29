struct VertexShaderOutput
{
    float4 position : SV_POSITION;
    float2 texcoord : TEXCOORD0;
    float3 normal : NORMAL0;
	//  シェーダー間で色を渡すための変数
    float4 color : COLOR0;
};

struct Material
{
    float4 color;
    int enableLighting;
    float4x4 uvTransform;
};

//  C++の ParticleForGPU 構造体と一致させます
struct ParticleForGPU
{
    float4x4 WVP;
    float4x4 World;
    float4 color;
    float4x4 uvTransform;
};

struct DirectionalLight
{
    float4 color;
    float3 direction;
    float intensity;
};
