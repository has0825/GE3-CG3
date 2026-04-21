#include "Object3d.hlsli"

struct TransformationMatrix
{
    float32_t4x4 WVP;
    float32_t4x4 World;
};

// ★ここが b1 になっていたせいでモデルが消滅していました。b0 に戻します。
ConstantBuffer<TransformationMatrix> gTransformationMatrix : register(b0);

struct VertexShaderInput
{
    float32_t4 position : POSITION0;
    float32_t2 texcoord : TEXCOORD0;
    float32_t3 normal : NORMAL0;
    uint32_t4 jointIndices : BONEINDICES0;
    float32_t4 jointWeights : BONEWEIGHTS0;
};

ConstantBuffer<SkinningPalette> gSkinningPalette : register(b1);

VertexShaderOutput main(VertexShaderInput input)
{
    VertexShaderOutput output;
    
    float32_t4 skinnedPosition = float32_t4(0, 0, 0, 0);
    float32_t3 skinnedNormal = float32_t3(0, 0, 0);

    for (int i = 0; i < 4; i++) {
        if (input.jointWeights[i] > 0) {
            skinnedPosition += mul(input.position, gSkinningPalette.boneMatrices[input.jointIndices[i]]) * input.jointWeights[i];
            skinnedNormal += mul(input.normal, (float32_t3x3)gSkinningPalette.boneMatrices[input.jointIndices[i]]) * input.jointWeights[i];
        }
    }
    
    // スキニングされていないモデル（ウェイトが全て0）の場合の安全策
    if (length(skinnedPosition) == 0) {
        skinnedPosition = input.position;
        skinnedNormal = input.normal;
    }

    output.position = mul(skinnedPosition, gTransformationMatrix.WVP);
    output.texcoord = input.texcoord;
    
    output.normal = normalize(mul(skinnedNormal, (float32_t3x3) gTransformationMatrix.World));
    output.worldPosition = mul(skinnedPosition, gTransformationMatrix.World).xyz;
    
    return output;
}