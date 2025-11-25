#include "Particle.hlsli"

ConstantBuffer<Material> gMaterial : register(b0);

// テクスチャは t0 レジスタを使用
Texture2D<float32_t4> gTexture : register(t0);
SamplerState gSampler : register(s0);

struct PixelShaderOutput
{
    float32_t4 color : SV_Target0;
};

PixelShaderOutput main(VertexShaderOutput input)
{
    PixelShaderOutput output;

    float4 transformedUV = mul(float32_t4(input.texcoord, 0.0f, 1.0f), gMaterial.uvTransform);
    float32_t4 textureColor = gTexture.Sample(gSampler, transformedUV.xy);

    // テクスチャの色が暗い場合描画しない（簡易的なアルファテスト）
    if (textureColor.r < 0.1f && textureColor.g < 0.1f && textureColor.b < 0.1f)
    {
        discard;
    }

    // ライティング計算は行わず、テクスチャとマテリアルカラーを乗算して出力
    output.color = gMaterial.color * textureColor;
    
    return output;
}