#include "object3d.hlsli"

// 1インスタンスあたりのデータ構造
// C++側のInstancingData構造体と一致させる
struct InstancingData
{
    float32_t4x4 WVP;
    float32_t4x4 World;
};

// 全インスタンスのデータを受け取るための構造化バッファ
// t1レジスタにバインドする
StructuredBuffer<InstancingData> gInstancingData : register(t1);

struct VertexShaderInput
{
    float32_t4 position : POSITION0;
    float32_t2 texcoord : TEXCOORD0;
    float32_t3 normal : NORMAL0;
};


// main関数の引数にインスタンスIDを追加
VertexShaderOutput main(VertexShaderInput input, uint instanceID : SV_InstanceID)
{
    VertexShaderOutput output;

    // インスタンスIDを使って、この頂点に対応するインスタンスのデータを取得
    InstancingData instancingData = gInstancingData[instanceID];

    // 取得したインスタンス固有の行列を使って座標変換
    output.position = mul(input.position, instancingData.WVP);
    output.texcoord = input.texcoord;
    output.normal = normalize(mul(input.normal, (float32_t3x3) instancingData.World));
    output.worldPosition = mul(input.position, instancingData.World).xyz; // worldPositionも忘れずに計算

    return output;
}