#include "Particle.hlsli"

// 構造体定義は削除しました

ConstantBuffer<Material> gMaterial : register(b0);
Texture2D<float32_t4> gTexture : register(t0);
SamplerState gSampler : register(s0);

struct PixelShaderOutput
{
    float32_t4 color : SV_Target0;
};

PixelShaderOutput main(VertexShaderOutput input)
{
    PixelShaderOutput output;

	// UV変換
    float4 transformedUV = mul(float4(input.texcoord, 0.0f, 1.0f), gMaterial.uvTransform);
	
	// テクスチャサンプリング
    float32_t4 textureColor = gTexture.Sample(gSampler, transformedUV.xy);

	// 頂点シェーダーから来た色(パーティクルの色・透明度)を乗算
    output.color = gMaterial.color * textureColor * input.color;

	// 透明度が0になったら描画を破棄
    if (output.color.a == 0.0f)
    {
        discard;
    }

    return output;
}