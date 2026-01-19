#include "Object3d.hlsli"

// インスタンシング用のデータを受け取るバッファ
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

    // インスタンスIDを使って現在のモデルの行列を取得
    InstancingData data = gInstancingData[instanceID];

    // 座標変換 (モデル座標 -> クリップ座標)
    output.position = mul(input.position, data.WVP);
    
    // ワールド座標の計算（ピクセルシェーダーでのライティング計算用）
    output.worldPosition = mul(input.position, data.World).xyz;

    // ★重要: 非均一スケール対応
    // 法線は World行列そのものではなく、Worldの逆転置行列(InverseTranspose)で変換する
    // これにより、モデルを引き伸ばしても法線が正しい方向を向く
    output.normal = normalize(mul(input.normal, (float32_t3x3) data.WorldInverseTranspose));

    output.texcoord = input.texcoord;

    return output;
}