// ピクセルシェーダー、頂点シェーダー間でやり取りする構造体
struct VertexShaderOutput
{
    float32_t4 position : SV_POSITION;
    float32_t2 texcoord : TEXCOORD0;
    float32_t3 normal : NORMAL0;
    float32_t3 worldPosition : POSITION0;
};

// マテリアルデータ
struct Material
{
    float32_t4 color;
    int32_t enableLighting;
    float32_t4x4 uvTransform;
    float32_t shininess;
    float32_t environmentCoefficient; // 【追加】環境マップの反射係数
};

// カメラデータ
struct Camera
{
    float32_t3 worldPosition;
};

// 平行光源・スポットライト兼用光源データ
struct DirectionalLight
{
    float32_t4 color;
    float32_t3 direction;
    float32_t intensity;
    
    int32_t enableSpotLight;
    float32_t3 spotLightPos;
    float32_t spotLightRange;
    float32_t3 spotLightDir;
    float32_t spotLightCosAngle;
    float32_t3 spotLightColor;
    float32_t spotLightIntensity;
    float32_t3 padding;
};

// スキニングデータ
struct SkinningPalette
{
    float32_t4x4 boneMatrices[128];
};