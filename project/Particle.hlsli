struct VertexShaderOutput
{
    float32_t4 position : SV_POSITION;
    float32_t2 texcoord : TEXCOORD0;
    float32_t3 normal : NORMAL0;
	// ★追加: シェーダー間で色を渡すための変数
    float32_t4 color : COLOR0;
};

struct Material
{
    float32_t4 color;
    int32_t enableLighting;
    float32_t4x4 uvTransform;
};

// ★修正: C++の ParticleForGPU 構造体と一致させます
struct ParticleForGPU
{
    float32_t4x4 WVP;
    float32_t4x4 World;
    float32_t4 color;
    float32_t4x4 uvTransform;
};

struct DirectionalLight
{
    float32_t4 color;
    float32_t3 direction;
    float intensity;
};