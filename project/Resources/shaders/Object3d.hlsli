// 頂点シェーダーからピクセルシェーダーへの出力用構造体
struct VertexShaderOutput
{
    float32_t4 position : SV_POSITION;
    float32_t2 texcoord : TEXCOORD0;
    float32_t3 normal : NORMAL0;
};

// マテリアル（色、ライティングの有無、UV変換行列）
struct Material
{
    float32_t4 color;
    int32_t enableLighting;
    float32_t4x4 uvTransform;
};

// 座標変換行列（WVP行列とWorld行列）
struct TransformationMatrix
{
    float32_t4x4 WVP;
    float32_t4x4 World;
};

// 平行光源（ディレクショナルライト）
struct DirectionalLight
{
    float32_t4 color;
    float32_t3 direction;
    float intensity;
};