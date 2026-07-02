#include "Particle.hlsli"

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

    float4 transformedUV = mul(float32_t4(input.texcoord, 0.0f, 1.0f), gMaterial.uvTransform);
    float32_t4 textureColor = gTexture.Sample(gSampler, transformedUV.xy);

    // ★ 煙っぽさを排除し、くっきりはっきりした雷のソリッドな輪郭にするためのアルファエッジシャープ化
    // 閾値 0.28 未満のぼやけた境界部を完全に切り捨て、残った部分をシャープに立ち上げる
    if (textureColor.a < 0.28f)
    {
        discard;
    }
    textureColor.a = saturate((textureColor.a - 0.28f) / 0.12f);

	// ★修正: input.color を乗算して、フェードアウトを反映
    output.color = gMaterial.color * textureColor * input.color;
	
	// 透明になったら完全に消す
    if (output.color.a == 0.0f)
    {
        discard;
    }

    return output;
}