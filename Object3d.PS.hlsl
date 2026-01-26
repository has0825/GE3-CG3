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

// Lambert拡散反射
float3 CalculateDiffuse(float3 N, float3 L)
{
    float NdotL = dot(N, L);
    return saturate(NdotL);
}

// スペキュラ反射
float3 CalculateSpecular(float3 N, float3 L, float3 V, float shininess, int mode)
{
    float3 specColor = float3(0, 0, 0);
    if (mode == 2) // Phong
    {
        float3 R = reflect(-L, N);
        float RdotV = dot(R, V);
        specColor = pow(saturate(RdotV), shininess);
    }
    else if (mode >= 3) // Blinn-Phong
    {
        float3 H = normalize(L + V);
        float NdotH = dot(N, H);
        specColor = pow(saturate(NdotH), shininess);
    }
    return specColor;
}

PixelShaderOutput main(VertexShaderOutput input)
{
    PixelShaderOutput output;

    float4 textureColor = gTexture.Sample(gSampler, input.texcoord);
    
    // アルファテスト
    if (textureColor.a < 0.1f)
    {
        discard;
    }

    if (gMaterial.enableLighting == 0)
    {
        output.color = gMaterial.color * textureColor;
        return output;
    }

    float3 N = normalize(input.normal);
    float3 V = normalize(gCamera.worldPosition - input.worldPosition);
    
    float3 totalDiffuse = float3(0, 0, 0);
    float3 totalSpecular = float3(0, 0, 0);

    // Directional Light
    for (int k = 0; k < NUM_DIR_LIGHTS; ++k)
    {
        DirectionalLight light = gLights.directionalLights[k];
        if (light.intensity <= 0.0f)
            continue;

        float3 L = normalize(-light.direction);
        float3 lightColor = light.color.rgb * light.intensity;

        totalDiffuse += CalculateDiffuse(N, L) * lightColor;
        if (gMaterial.enableLighting >= 2)
        {
            totalSpecular += CalculateSpecular(N, L, V, gMaterial.shininess, gMaterial.enableLighting) * lightColor;
        }
    }

    // Point Light
    for (int i = 0; i < NUM_POINT_LIGHTS; ++i)
    {
        PointLight light = gLights.pointLights[i];
        if (light.intensity <= 0.0f)
            continue;

        float3 toLight = light.position - input.worldPosition;
        float dist = length(toLight);
        float3 L = normalize(toLight);

        // ★修正: radiusとdecayを使った減衰
        float distRate = saturate(1.0f - dist / light.radius);
        float attenuation = pow(distRate, light.decay);

        float sideCheck = dot(N, L);
        if (sideCheck > 0.0f && attenuation > 0.0f)
        {
            float3 lightColor = light.color.rgb * light.intensity;
            totalDiffuse += CalculateDiffuse(N, L) * lightColor * attenuation;
            if (gMaterial.enableLighting >= 2)
            {
                totalSpecular += CalculateSpecular(N, L, V, gMaterial.shininess, gMaterial.enableLighting) * lightColor * attenuation;
            }
        }
    }

    // Spot Light
   // --- Spot Light ---
// --- Spot Light ---
    for (int k = 0; k < NUM_SPOT_LIGHTS; ++k)
    {
        SpotLight light = gLights.spotLights[k];
    
    // ライトが無効ならスキップ
        if (light.intensity <= 0.0f)
            continue;

    // 1. 入射光ベクトル（ライトの位置から、物体表面へのベクトル）
    // User Code: float32_t3 spotLightDirectionOnSurface = normalize(input.worldPosition - gSpotlight.position);
        float3 spotLightDirectionOnSurface = normalize(input.worldPosition - light.position);

    // 2. 角度の計算 (ライトの向きと、表面への入射光ベクトルの内積)
    // User Code: float32_t cosAngle = dot(spotLightDirectionOnSurface, gSpotLight.direction);
        float cosAngle = dot(spotLightDirectionOnSurface, light.direction);

    // 3. Falloff (フォールオフ) の計算
    // User Code: float32_t falloffFactor = saturate((cosAngle - gSpotLight.cosAngle) / (1.0f - gSpotLight.cosAngle));
    // 角度が中心(1.0)に近いほど明るく、指定角度(cosAngle)で0になる
        float falloffFactor = saturate((cosAngle - light.cosAngle) / (1.0f - light.cosAngle));

    // 範囲外なら計算しない
        if (falloffFactor <= 0.0f)
            continue;

    // 4. 距離減衰 (PointLightと同様)
        float distance = length(input.worldPosition - light.position); // 正確な距離
        float distRate = saturate(1.0f - distance / light.distance);
        float attenuationFactor = pow(distRate, light.decay);

    // 5. 拡散反射・鏡面反射の計算
    // 光の入射方向 L は、表面からライトへの向きなので -spotLightDirectionOnSurface
        float3 L = normalize(-spotLightDirectionOnSurface);
    
        float NdotL = dot(N, L);
        if (NdotL > 0.0f)
        {
        // 結果として減衰を考慮した光の色
        // User Code: gSpotLight.color.rgb * gSpotLight.intensity * attenuationFactor * falloffFactor;
            float3 lightColor = light.color.rgb * light.intensity * attenuationFactor * falloffFactor;

        // 拡散反射
            totalDiffuse += CalculateDiffuse(N, L) * lightColor;

        // 鏡面反射
            if (gMaterial.enableLighting >= 2)
            {
                totalSpecular += CalculateSpecular(N, L, V, gMaterial.shininess, gMaterial.enableLighting) * lightColor;
            }
        }
    }

    // 環境光 (Ambient) を少し足して真っ暗になるのを防ぐ
    float3 ambientColor = float3(0.1f, 0.1f, 0.1f);
    float3 finalDiffuse = totalDiffuse + ambientColor;
    
    float3 finalColor = finalDiffuse * gMaterial.color.rgb * textureColor.rgb + totalSpecular;
    output.color = float4(finalColor, gMaterial.color.a * textureColor.a);

    return output;
}