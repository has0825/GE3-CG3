#include "CopyImage.hlsli"

Texture2D<float32_t4> gTexture : register(t0);
SamplerState gSampler : register(s0);

cbuffer GaussianFilterParameter : register(b0) {
    int32_t kernelSize; // k
    float32_t sigma;
}

struct PixelShaderOutput {
    float32_t4 color : SV_TARGET0;
};

static const float32_t PI = 3.14159265f;

float32_t gauss(float32_t x, float32_t y, float32_t s) {
    float32_t exponent = -(x * x + y * y) * rcp(2.0f * s * s);
    float32_t denominator = 2.0f * PI * s * s;
    return exp(exponent) * rcp(denominator);
}

PixelShaderOutput main(VertexShaderOutput input) {
    uint32_t width, height;
    gTexture.GetDimensions(width, height);
    float32_t2 uvStepSize = float32_t2(rcp(width), rcp(height));

    PixelShaderOutput output;
    output.color.rgb = float32_t3(0.0f, 0.0f, 0.0f);
    output.color.a = 1.0f;

    int32_t k = kernelSize;
    float32_t weight = 0.0f;

    for (int32_t x = -k; x <= k; ++x) {
        for (int32_t y = -k; y <= k; ++y) {
            float32_t2 texcoord = input.texcoord + float32_t2(x, y) * uvStepSize;
            float32_t3 fetchColor = gTexture.Sample(gSampler, texcoord).rgb;
            
            float32_t w = gauss((float32_t)x, (float32_t)y, sigma);
            output.color.rgb += fetchColor * w;
            weight += w;
        }
    }

    output.color.rgb *= rcp(weight);

    return output;
}
