#include "Object3d.hlsli"

struct InstancingData
{
    float32_t4x4 WVP;
    float32_t4x4 World;
};

StructuredBuffer<InstancingData> gInstancingData : register(t1);

struct VertexShaderInput
{
    float32_t4 position : POSITION0;
    float32_t2 texcoord : TEXCOORD0;
    float32_t3 normal : NORMAL0;
};

VertexShaderOutput main(VertexShaderInput input, uint instanceID : SV_InstanceID)
{
    VertexShaderOutput output;

    InstancingData data = gInstancingData[instanceID];

    // 座標変換
    output.position = mul(input.position, data.WVP);
    
    // ワールド座標の計算（ライティング用）
    output.worldPosition = mul(input.position, data.World).xyz;

    // 法線の変換 (非均一スケール対応のため正規化は必須)
    // 本来はWorldInverseTransposeを使うのが正確ですが、簡易的にWorld行列で変換して正規化します
    output.normal = normalize(mul(input.normal, (float32_t3x3) data.World));

    output.texcoord = input.texcoord;

    return output;
}