#include "Particle.hlsli"

// ★修正: 型を ParticleForGPU に変更
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

	// ★修正: ParticleForGPU としてデータ取得
    ParticleForGPU particle = gParticle[instanceID];

    output.position = mul(input.position, particle.WVP);
    output.texcoord = input.texcoord;
    output.normal = normalize(mul(input.normal, (float32_t3x3) particle.World));
	
	// ★追加: ここで色（透明度含む）を渡すことで、消える処理が可能になる
    output.color = particle.color;
	
    return output;
}