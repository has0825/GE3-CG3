#include "Object3d.hlsli"

// ★正しいレジスタ順序に戻しました
ConstantBuffer<Material> gMaterial : register(b0);
ConstantBuffer<DirectionalLight> gDirectionalLight : register(b1);
ConstantBuffer<Camera> gCamera : register(b2);

Texture2D<float32_t4> gTexture : register(t0);
SamplerState gSampler : register(s0);

struct PixelShaderOutput
{
    float32_t4 color : SV_TARGET0;
};

PixelShaderOutput main(VertexShaderOutput input)
{
    PixelShaderOutput output;
    
    float4 transformedUV = mul(float32_t4(input.texcoord, 0.0f, 1.0f), gMaterial.uvTransform);
    float32_t4 textureColor = gTexture.Sample(gSampler, transformedUV.xy);
    
    if (gMaterial.enableLighting != 0)
    {
        float32_t3 normal = normalize(input.normal);
        float32_t3 lightDir = normalize(-gDirectionalLight.direction);
        float NdotL = dot(normal, lightDir);
        float cos = pow(NdotL * 0.5f + 0.5f, 2.0f); // Half Lambert
        float32_t3 diffuse = gMaterial.color.rgb * textureColor.rgb * gDirectionalLight.color.rgb * gDirectionalLight.intensity * cos;

        output.color.rgb = diffuse;
        output.color.a = gMaterial.color.a * textureColor.a;
    }
    else
    {
        output.color = gMaterial.color * textureColor;
    }

    return output;
}