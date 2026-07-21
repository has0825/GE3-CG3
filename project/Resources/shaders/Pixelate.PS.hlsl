struct VertexShaderOutput {
    float4 position : SV_POSITION;
    float2 texcoord : TEXCOORD0;
};

Texture2D<float4> gTexture : register(t0);
SamplerState gSampler : register(s0);

cbuffer PixelateParameter : register(b0) {
    float2 numPixels; // モザイク解像度 (例: 320 x 180)
};

struct PixelShaderOutput {
    float4 color : SV_TARGET0;
};

PixelShaderOutput main(VertexShaderOutput input) {
    PixelShaderOutput output;
    
    // UV座標を量子化してピクセル化（モザイク効果）
    float2 uv = floor(input.texcoord * numPixels) / numPixels;
    output.color = gTexture.Sample(gSampler, uv);
    
    return output;
}
