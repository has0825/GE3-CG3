#include "Particle.hlsli"

// ★修正: InstancingData ではなく、hlsliで定義した TransformationMatrix を使う
StructuredBuffer<TransformationMatrix> gInstancingData : register(t1);

struct VertexShaderInput
{
    float32_t4 position : POSITION0;
    float32_t2 texcoord : TEXCOORD0;
    float32_t3 normal : NORMAL0;
};

VertexShaderOutput main(VertexShaderInput input, uint instanceID : SV_InstanceID)
{
    VertexShaderOutput output;

    // ★修正: 型名を合わせる
    TransformationMatrix instancingData = gInstancingData[instanceID];

    output.position = mul(input.position, instancingData.WVP);
    output.texcoord = input.texcoord;
    output.normal = normalize(mul(input.normal, (float32_t3x3) instancingData.World));
    
    return output;
}