#include "CopyImage.hlsli"

Texture2D<float32_t4> gTexture : register(t0);
SamplerState gSampler : register(s0);

cbuffer RadialBlurParameter : register(b0) {
    float32_t2 center;
    float32_t blurWidth;
}

struct PixelShaderOutput {
    float32_t4 color : SV_TARGET0;
};

PixelShaderOutput main(VertexShaderOutput input) {
    const int32_t kNumSamples = 10;
    
    float32_t2 direction = input.texcoord - center;
    float32_t3 outputColor = float32_t3(0.0f, 0.0f, 0.0f);
    
    for (int32_t sampleIndex = 0; sampleIndex < kNumSamples; ++sampleIndex) {
        float32_t2 texcoord = input.texcoord + direction * blurWidth * float32_t(sampleIndex);
        outputColor.rgb += gTexture.Sample(gSampler, texcoord).rgb;
    }
    
    outputColor.rgb *= rcp((float32_t)kNumSamples);
    
    PixelShaderOutput output;
    output.color.rgb = outputColor;
    output.color.a = 1.0f;
    return output;
}
