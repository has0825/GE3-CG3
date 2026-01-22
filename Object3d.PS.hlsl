#include "Object3d.hlsli"

ConstantBuffer<Material> gMaterial : register(b0);
ConstantBuffer<LightGroup> gLights : register(b1); // DirectionalLight単体ではなくGroupで受け取る
ConstantBuffer<Camera> gCamera : register(b2);

Texture2D<float32_t4> gTexture : register(t0);
SamplerState gSampler : register(s0);

struct PixelShaderOutput
{
    float32_t4 color : SV_Target0;
};

// 共通関数: Lambert拡散反射
float3 CalculateDiffuse(float3 N, float3 L)
{
    float NdotL = dot(N, L);
    return saturate(NdotL);
}

// 共通関数: スペキュラ反射 (Phong / Blinn-Phong)
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

    // テクスチャサンプリング
    float4 transformedUV = mul(float32_t4(input.texcoord, 0.0f, 1.0f), gMaterial.uvTransform);
    float32_t4 textureColor = gTexture.Sample(gSampler, transformedUV.xy);

    // ライティングなしならそのまま返す
    if (gMaterial.enableLighting == 0)
    {
        output.color = gMaterial.color * textureColor;
        return output;
    }

    float3 N = normalize(input.normal);
    float3 V = normalize(gCamera.worldPosition - input.worldPosition);
    
    // 結果格納用
    float3 totalDiffuse = float3(0, 0, 0);
    float3 totalSpecular = float3(0, 0, 0);

    // -----------------------------------------------------------------------
    // 1. Directional Light Loop (平行光源)
    // -----------------------------------------------------------------------
    for (int i = 0; i < gLights.numDirectionalLights; ++i)
    {
        DirectionalLight light = gLights.directionalLights[i];
        float3 L = normalize(-light.direction); // ライト方向の逆
        float3 lightColor = light.color.rgb * light.intensity;

        totalDiffuse += CalculateDiffuse(N, L) * lightColor;
        if (gMaterial.enableLighting >= 2) {
            totalSpecular += CalculateSpecular(N, L, V, gMaterial.shininess, gMaterial.enableLighting) * lightColor;
        }
    }

    // -----------------------------------------------------------------------
    // 2. Point Light Loop (点光源)
    // -----------------------------------------------------------------------
    for (int j = 0; j < gLights.numPointLights; ++j)
    {
        PointLight light = gLights.pointLights[j];
        float3 directionToLight = light.position - input.worldPosition;
        float distance = length(directionToLight);
        
        // 範囲外なら計算しない
        if (distance < light.radius) 
        {
            float3 L = normalize(directionToLight);
            float3 lightColor = light.color.rgb * light.intensity;

            // 減衰計算 (逆二乗の法則に近い形 + 半径制御)
            float attenuation = saturate(1.0f - distance / light.radius);
            attenuation *= attenuation; 

            totalDiffuse += CalculateDiffuse(N, L) * lightColor * attenuation;
            if (gMaterial.enableLighting >= 2) {
                totalSpecular += CalculateSpecular(N, L, V, gMaterial.shininess, gMaterial.enableLighting) * lightColor * attenuation;
            }
        }
    }

    // -----------------------------------------------------------------------
    // 3. Spot Light Loop (スポットライト)
    // -----------------------------------------------------------------------
    for (int k = 0; k < gLights.numSpotLights; ++k)
    {
        SpotLight light = gLights.spotLights[k];
        float3 directionToLight = light.position - input.worldPosition;
        float distance = length(directionToLight);

        if (distance < light.distance)
        {
            float3 L = normalize(directionToLight);
            float3 lightColor = light.color.rgb * light.intensity;
            
            // 距離減衰
            float distAttenuation = saturate(1.0f - distance / light.distance);
            distAttenuation *= distAttenuation;

            // 角度減衰
            float3 spotDir = normalize(light.direction);
            float angleCos = dot(-L, spotDir); // ライトの向きと、点への方向の逆(=ライトから見た方向)の内積

            float spotFactor = 0.0f;
            if (angleCos > light.cosAngle)
            {
                // smoothstep等で境界をぼかす
                spotFactor = smoothstep(light.cosAngle, light.cosFalloffStart, angleCos);
                spotFactor = pow(spotFactor, light.decay);
            }

            if (spotFactor > 0.0f)
            {
                totalDiffuse += CalculateDiffuse(N, L) * lightColor * distAttenuation * spotFactor;
                if (gMaterial.enableLighting >= 2) {
                    totalSpecular += CalculateSpecular(N, L, V, gMaterial.shininess, gMaterial.enableLighting) * lightColor * distAttenuation * spotFactor;
                }
            }
        }
    }

    // -----------------------------------------------------------------------
    // 4. Area Light (Rect) Loop (面光源・簡易版)
    // -----------------------------------------------------------------------
    for (int m = 0; m < gLights.numAreaLights; ++m)
    {
        AreaLight light = gLights.areaLights[m];
        float3 lightColor = light.color.rgb * light.intensity;

        // 面の中心へのベクトル
        float3 toLight = light.position - input.worldPosition;
        float distToCenter = length(toLight);
        float3 L = normalize(toLight);

        // 簡易減衰
        float attenuation = 1.0f / (1.0f + 0.1f * distToCenter + 0.01f * distToCenter * distToCenter);
        
        // 裏面チェック
        float sideCheck = dot(-L, normalize(light.direction));
        
        if (sideCheck > 0.0f) {
            totalDiffuse += CalculateDiffuse(N, L) * lightColor * attenuation * sideCheck;
            
            // スペキュラ（中心点近似）
            if (gMaterial.enableLighting >= 2) {
                 totalSpecular += CalculateSpecular(N, L, V, gMaterial.shininess, gMaterial.enableLighting) * lightColor * attenuation * sideCheck;
            }
        }
    }

    // -----------------------------------------------------------------------
    // 最終合成
    // -----------------------------------------------------------------------
    
    // ★環境光 (Ambient) の追加 (黒くなるのを防ぐ)
    float3 ambientColor = float3(0.1f, 0.1f, 0.1f);
    float3 finalDiffuse = totalDiffuse + ambientColor;

    // (拡散光 + 環境光) * テクスチャ色 * マテリアル色 + スペキュラ(加算)
    float3 finalColor = (finalDiffuse * textureColor.rgb * gMaterial.color.rgb) + totalSpecular;

    output.color = float4(finalColor, gMaterial.color.a * textureColor.a);
    
    return output;
}