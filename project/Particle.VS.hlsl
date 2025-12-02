#include "Particle.hlsli"

// 構造体定義は削除しました

// Instancing用の構造化バッファ (register t1)
StructuredBuffer<ParticleForGPU> gParticle : register(t1);

struct VertexShaderInput
{
    float32_t4 position : POSITION0;
    float32_t2 texcoord : TEXCOORD0;
    float32_t3 normal : NORMAL0;
};

VertexShaderOutput main(VertexShaderInput input, uint instanceID : SV_InstanceID)
{
    VertexShaderOutput output;

	// インスタンスIDを使って個別のパーティクル情報を取得
    ParticleForGPU particle = gParticle[instanceID];

	// 座標変換
    output.position = mul(input.position, particle.WVP);
    output.texcoord = input.texcoord;
    output.normal = normalize(mul(input.normal, (float3x3) particle.World));
	
	// 色情報をピクセルシェーダーへ渡す
    output.color = particle.color;

    return output;
}