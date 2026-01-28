#include "Object3d.hlsli"

ConstantBuffer<Material> gMaterial : register(b0);
ConstantBuffer<LightGroup> gLights : register(b1);
ConstantBuffer<Camera> gCamera : register(b2);

Texture2D<float32_t4> gTexture : register(t0);
SamplerState gSampler : register(s0);

struct PixelShaderOutput
{
    float32_t4 color : SV_Target0;
};

float3 CalculateDiffuse(float3 N, float3 L)
{
    return saturate(dot(N, L));
}

float3 CalculateSpecular(float3 N, float3 L, float3 V, float shininess, int mode)
{
    float3 specColor = float3(0, 0, 0);
    if (mode == 2) // Phong
    {
        float3 R = reflect(-L, N);
        specColor = pow(saturate(dot(R, V)), shininess);
    }
    else if (mode >= 3) // Blinn-Phong
    {
        float3 H = normalize(L + V);
        specColor = pow(saturate(dot(N, H)), shininess);
    }
    return specColor;
}

PixelShaderOutput main(VertexShaderOutput input)
{
    PixelShaderOutput output;
    float4 textureColor = gTexture.Sample(gSampler, input.texcoord);
    
    // アルファテスト
    if (textureColor.a < 0.1f)
        discard;

    if (gMaterial.enableLighting == 0)
    {
        output.color = gMaterial.color * textureColor;
        return output;
    }

    float3 N = normalize(input.normal);
    float3 V = normalize(gCamera.worldPosition - input.worldPosition);
    
    float3 totalDiffuse = float3(0, 0, 0);
    float3 totalSpecular = float3(0, 0, 0);

    // 1. Directional Light
    for (int k = 0; k < NUM_DIR_LIGHTS; ++k)
    {
        if (gLights.directionalLights[k].intensity <= 0.0f)
            continue;
        
        float3 L = normalize(-gLights.directionalLights[k].direction);
        float3 lightColor = gLights.directionalLights[k].color.rgb * gLights.directionalLights[k].intensity;

        totalDiffuse += CalculateDiffuse(N, L) * lightColor;
        if (gMaterial.enableLighting >= 2)
            totalSpecular += CalculateSpecular(N, L, V, gMaterial.shininess, gMaterial.enableLighting) * lightColor;
    }

    // 2. Point Light
    for (int i = 0; i < NUM_POINT_LIGHTS; ++i)
    {
        if (gLights.pointLights[i].intensity <= 0.0f)
            continue;

        float3 lightPos = gLights.pointLights[i].position;
        float3 toLight = lightPos - input.worldPosition;
        float dist = length(toLight);
        float3 L = normalize(toLight);
        
        float radius = gLights.pointLights[i].radius;
        float decay = gLights.pointLights[i].decay;

        float distRate = saturate(1.0f - dist / radius);
        float atten = pow(distRate, decay);

        if (atten > 0.0f)
        {
            float3 lightColor = gLights.pointLights[i].color.rgb * gLights.pointLights[i].intensity * atten;
            totalDiffuse += CalculateDiffuse(N, L) * lightColor;
            if (gMaterial.enableLighting >= 2)
                totalSpecular += CalculateSpecular(N, L, V, gMaterial.shininess, gMaterial.enableLighting) * lightColor;
        }
    }
    
    
  // 3. Spot Light
    for (int j = 0; j < NUM_SPOT_LIGHTS; ++j)
    {
        if (gLights.spotLights[j].intensity <= 0.0f)
            continue;

    // ポイントライトと同様の距離減衰計算
        float3 lightPos = gLights.spotLights[j].position;
        float3 toLight = lightPos - input.worldPosition;
        float dist = length(toLight);
        float3 L = normalize(toLight);

        float distRate = saturate(1.0f - dist / gLights.spotLights[j].distance);
        float attenuation = pow(distRate, gLights.spotLights[j].decay);

    // スポットライト特有の角度減衰 (Falloff)
        float3 spotDir = normalize(gLights.spotLights[j].direction);
        float cosAngle = dot(-L, spotDir); // 光源から表面への方向とライトの向き
        float falloffFactor = saturate((cosAngle - gLights.spotLights[j].cosAngle) /
                          (gLights.spotLights[j].cosFalloffStart - gLights.spotLights[j].cosAngle));

        if (attenuation > 0.0f && falloffFactor > 0.0f)
        {
        // 距離減衰と角度減衰を乗算
            float3 lightColor = gLights.spotLights[j].color.rgb * gLights.spotLights[j].intensity * attenuation * falloffFactor;
        
            totalDiffuse += CalculateDiffuse(N, L) * lightColor;
            if (gMaterial.enableLighting >= 2)
                totalSpecular += CalculateSpecular(N, L, V, gMaterial.shininess, gMaterial.enableLighting) * lightColor;
        }
    }


    float3 ambient = float3(0.1f, 0.1f, 0.1f);
    float3 finalColor = (totalDiffuse + ambient) * gMaterial.color.rgb * textureColor.rgb + totalSpecular;
    
    output.color = float4(finalColor, gMaterial.color.a * textureColor.a);
    return output;
}