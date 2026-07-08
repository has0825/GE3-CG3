#include "CopyImage.hlsli"

Texture2D<float32_t4> gTexture : register(t0);
Texture2D<float32_t4> gMaskTexture : register(t1);
SamplerState gSampler : register(s0);

cbuffer DissolveParameter : register(b0) {
    float32_t threshold;
    float32_t3 edgeColor;
    float32_t edgeRange;
}

struct PixelShaderOutput {
    float32_t4 color : SV_TARGET0;
};

PixelShaderOutput main(VertexShaderOutput input) {
    float32_t mask = gMaskTexture.Sample(gSampler, input.texcoord).r;
    
    if (mask <= threshold) {
        discard;
    }
    
    PixelShaderOutput output;
    output.color = gTexture.Sample(gSampler, input.texcoord);
    
    float32_t edge = 1.0f - smoothstep(threshold, threshold + edgeRange, mask);
    // output.color.rgb += edge * edgeColor; // Disable red edge glow to show textures naturally
    
    return output;
}
