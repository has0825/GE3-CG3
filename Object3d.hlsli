// Object3d.hlsli

struct VertexShaderOutput
{
    float32_t4 position : SV_POSITION;
    float32_t2 texcoord : TEXCOORD0;
    float32_t3 normal : NORMAL0;
    float32_t3 worldPosition : TEXCOORD1; // ライティング計算用にワールド座標を追加
};

struct Material
{
    float32_t4 color;
    int32_t enableLighting; // 0:なし, 1:Lambert, 2:Phong, 3:BlinnPhong
    float32_t shininess; // 光沢度 (paddingを利用)
    float32_t environment; // 環境光係数 (paddingを利用)
    float32_t padding;
    float32_t4x4 uvTransform;
};

struct TransformationMatrix
{
    float32_t4x4 WVP;
    float32_t4x4 World;
};

struct DirectionalLight
{
    float32_t4 color;
    float32_t3 direction;
    float intensity;
};

// 視線ベクトル計算用にカメラ座標が必要
struct Camera
{
    float32_t3 worldPosition;
};