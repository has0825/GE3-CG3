// ハッシュ関数による簡易ノイズ
float hash(float2 p) {
    return frac(sin(dot(p, float2(127.1, 311.7))) * 43758.5453123);
}

struct PixelShaderInput {
    float4 position : SV_POSITION;
    float2 texcoord : TEXCOORD0;
    float4 color : COLOR0;
};

float4 main(PixelShaderInput input) : SV_TARGET {
    float2 uv = input.texcoord - 0.5f;
    float dist = length(uv);

    // 極座標による角度の取得
    float theta = atan2(uv.y, uv.x);

    // 角度に応じて異なる周波数のサイン波を足して、ギザギザした小石の輪郭（多角形ライク）を生成
    float radiusLimit = 0.4f + sin(theta * 5.0f) * 0.04f + cos(theta * 8.0f) * 0.03f + sin(theta * 13.0f) * 0.015f;

    // 輪郭の外側はカットする（アルファテスト）
    if (dist > radiusLimit) {
        discard;
    }

    // 岩肌のザラザラ感を表現するためのノイズ（高周波）
    float noise = hash(input.texcoord * 128.0f) * 0.15f - 0.075f;
    float3 rockColor = saturate(input.color.rgb + noise);

    // 簡易的な立体感（シェーディング）を与える：中心に近いほど明るく、外側に行くほど影をつける
    float shade = saturate(1.0f - dist / radiusLimit);
    float3 finalColor = rockColor * (0.5f + shade * 0.5f);

    // 徐々にフェードアウト（アルファは頂点色に入っている）
    return float4(finalColor, input.color.a);
}
