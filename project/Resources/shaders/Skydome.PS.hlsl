// 天球専用のピクセルシェーダー（ライティングなし、テクスチャをそのまま出力）
#include "Object3d.hlsli"

ConstantBuffer<Material> gMaterial : register(b0);
Texture2D<float32_t4> gTexture     : register(t0);
SamplerState gSampler              : register(s0);
// 環境マップバインド（ルートシグネチャのスロットを合わせるためダミー宣言）
TextureCube<float32_t4> gEnvironmentTexture : register(t1);

struct PixelShaderOutput
{
    float32_t4 color : SV_TARGET0;
};

PixelShaderOutput main(VertexShaderOutput input)
{
    PixelShaderOutput output;

    // UVトランスフォームの適用
    float4 transformedUV = mul(float32_t4(input.texcoord, 0.0f, 1.0f), gMaterial.uvTransform);

    // テクスチャをそのまま出力（ライティングなし）
    output.color = gTexture.Sample(gSampler, transformedUV.xy) * gMaterial.color;

    return output;
}
