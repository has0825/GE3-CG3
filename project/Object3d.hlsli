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

// 平行光源データ（必要に応じてPointLightなどもここに追加）
struct DirectionalLight
{
    float32_t4 color;
    float32_t3 direction;
    float32_t intensity;
};

// スキニングデータ
struct SkinningPalette
{
    float32_t4x4 boneMatrices[128];
};