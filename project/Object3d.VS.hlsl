#include "Object3d.hlsli"

struct TransformationMatrix
{
    float32_t4x4 WVP;
    float32_t4x4 World;
};
ConstantBuffer<TransformationMatrix> gTransformationMatrix : register(b0);

struct VertexShaderInput
{
    float32_t4 position : POSITION0;
    float32_t2 texcoord : TEXCOORD0;
    float32_t3 normal : NORMAL0;
};

VertexShaderOutput main(VertexShaderInput input)
{
    VertexShaderOutput output;
    
    output.position = mul(input.position, gTransformationMatrix.WVP);
    output.texcoord = input.texcoord;
    
    // 法線とワールド座標の計算（環境マップに必須）
    output.normal = normalize(mul(input.normal, (float32_t3x3) gTransformationMatrix.World));
    output.worldPosition = mul(input.position, gTransformationMatrix.World).xyz;
    
    return output;
}