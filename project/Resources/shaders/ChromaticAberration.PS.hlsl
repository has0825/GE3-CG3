struct VertexShaderOutput {
    float4 position : SV_POSITION;
    float2 texcoord : TEXCOORD0;
};

Texture2D<float4> gTexture : register(t0);
SamplerState gSampler : register(s0);

cbuffer ChromaticAberrationParameter : register(b0) {
    float intensity; // 色ズレの強度
};

struct PixelShaderOutput {
    float4 color : SV_TARGET0;
};

PixelShaderOutput main(VertexShaderOutput input) {
    PixelShaderOutput output;
    
    // 画面中心からの距離に応じたオフセットを計算
    float2 dir = input.texcoord - float2(0.5f, 0.5f);
    float dist = length(dir);
    float2 offset = dir * (dist * intensity);
    
    // 赤(R)、緑(G)、青(B)の色チャンネルをずらしてサンプリング
    float r = gTexture.Sample(gSampler, input.texcoord + offset).r;
    float g = gTexture.Sample(gSampler, input.texcoord).g;
    float b = gTexture.Sample(gSampler, input.texcoord - offset).b;
    float a = gTexture.Sample(gSampler, input.texcoord).a;
    
    output.color = float4(r, g, b, a);
    return output;
}
