struct PixelShaderInput {
    float4 position : SV_POSITION;
    float2 texcoord : TEXCOORD0;
    float4 color : COLOR0;
};

float4 main(PixelShaderInput input) : SV_TARGET {
    float2 uv = input.texcoord - 0.5f;
    float x = abs(uv.x);
    float y = abs(uv.y);

    // 十字の光（中央ほど明るく、軸に沿って伸びる）
    float cross = 0.008f / max(x, 0.001f) + 0.008f / max(y, 0.001f);
    
    // 円形の減衰（外側に行くほど消える）
    float dist = length(uv);
    float mask = saturate(1.0f - dist * 2.2f);
    
    // 合成
    float star = saturate(cross * mask);
    
    // 中心部の輝きを少し強める
    star += pow(mask, 8.0f) * 0.8f;

    return input.color * star;
}
