struct VertexShaderOutput {
    float4 position : SV_POSITION;
    float2 texcoord : TEXCOORD0;
};

Texture2D<float4> gTexture : register(t0);
SamplerState gSampler : register(s0);

struct PixelShaderOutput {
    float4 color : SV_TARGET0;
};

PixelShaderOutput main(VertexShaderOutput input) {
    PixelShaderOutput output;
    float4 textureColor = gTexture.Sample(gSampler, input.texcoord);
    
    // ネガポジ反転（色反転）
    output.color.rgb = float3(1.0f - textureColor.r, 1.0f - textureColor.g, 1.0f - textureColor.b);
    output.color.a = textureColor.a;
    
    return output;
}
