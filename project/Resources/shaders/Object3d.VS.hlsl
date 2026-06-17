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
    float32_t3 normal : NORMAL0;
    uint32_t4 jointIndices : BONEINDICES0;
    float32_t4 jointWeights : BONEWEIGHTS0;
};

ConstantBuffer<SkinningPalette> gSkinningPalette : register(b4);

VertexShaderOutput main(VertexShaderInput input)
{
    VertexShaderOutput output;
    
    float32_t4 skinnedPosition = input.position;
    float32_t3 skinnedNormal = input.normal;

    // ボーンウェイトが定義されているオブジェクト（歩行モデル等）のみスキニング計算を行う (非スキニングモデルの頂点処理を極限まで軽量化)
    if (input.jointWeights[0] > 0.0f)
    {
        skinnedPosition = float32_t4(0, 0, 0, 0);
        skinnedNormal = float32_t3(0, 0, 0);
        for (int i = 0; i < 4; i++) {
            if (input.jointWeights[i] > 0) {
                skinnedPosition += mul(input.position, gSkinningPalette.boneMatrices[input.jointIndices[i]]) * input.jointWeights[i];
                skinnedNormal += mul(input.normal, (float32_t3x3)gSkinningPalette.boneMatrices[input.jointIndices[i]]) * input.jointWeights[i];
            }
        }
    }

    output.position = mul(skinnedPosition, gTransformationMatrix.WVP);
    output.texcoord = input.texcoord;
    
    // 法線とワールド座標の計算（環境マップに必須）
    output.normal = normalize(mul(skinnedNormal, (float32_t3x3) gTransformationMatrix.World));
    output.worldPosition = mul(skinnedPosition, gTransformationMatrix.World).xyz;
    
    return output;
}