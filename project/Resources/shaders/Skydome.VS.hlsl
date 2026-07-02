// Object3d.hlsli をインクルード（VertexShaderOutput などの型定義を流用）
#include "Object3d.hlsli"

struct TransformationMatrix
{
    float32_t4x4 WVP;
    float32_t4x4 World;
};
ConstantBuffer<TransformationMatrix> gTransformationMatrix : register(b1);

struct VertexShaderInput
{
    float32_t4 position : POSITION0;
    float32_t2 texcoord : TEXCOORD0;
    float32_t3 normal   : NORMAL0;
    uint32_t4  jointIndices : BONEINDICES0;
    float32_t4 jointWeights : BONEWEIGHTS0;
};

ConstantBuffer<SkinningPalette> gSkinningPalette : register(b4);

VertexShaderOutput main(VertexShaderInput input)
{
    VertexShaderOutput output;

    // WVP変換
    float32_t4 pos = mul(input.position, gTransformationMatrix.WVP);

    // ★ z を w と同じ値にすることで、透視除算後に z/w = 1.0（最遠平面）になる
    //   → 天球はどんなオブジェクトよりも必ず背後に描画される
    pos.z = pos.w;

    output.position     = pos;
    output.texcoord     = input.texcoord;
    output.normal       = float32_t3(0.0f, 1.0f, 0.0f); // 天球はライティング不要
    output.worldPosition = float32_t3(0.0f, 0.0f, 0.0f);

    return output;
}
