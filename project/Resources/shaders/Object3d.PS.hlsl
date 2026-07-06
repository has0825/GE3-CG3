#include "Object3d.hlsli"

ConstantBuffer<Material> gMaterial : register(b0);
ConstantBuffer<DirectionalLight> gDirectionalLight : register(b2);
ConstantBuffer<Camera> gCamera : register(b3);

Texture2D<float32_t4> gTexture : register(t0);
TextureCube<float32_t4> gEnvironmentTexture : register(t1); // 【追加】環境マップ用テクスチャ
SamplerState gSampler : register(s0);

struct PixelShaderOutput
{
    float32_t4 color : SV_TARGET0;
};

PixelShaderOutput main(VertexShaderOutput input)
{
    PixelShaderOutput output;
    
    // UVトランスフォームの適用
    float4 transformedUV = mul(float32_t4(input.texcoord, 0.0f, 1.0f), gMaterial.uvTransform);
    float32_t4 textureColor = gTexture.Sample(gSampler, transformedUV.xy);
    
    // 透過ピクセルの破棄（アルファクリッピング）
    if (textureColor.a < 0.05f)
    {
        discard;
    }
    
    // ライティング計算
    if (gMaterial.enableLighting != 0)
    {
        // --- 既存のPunctual Light (Directional Light) の計算 ---
        float32_t3 dirLightingColor = float32_t3(0.0f, 0.0f, 0.0f);
        if (gDirectionalLight.intensity > 0.0f) {
            float NdotL = dot(normalize(input.normal), -gDirectionalLight.direction);
            float cos = pow(NdotL * 0.5f + 0.5f, 2.0f); // Half Lambert
            dirLightingColor = gDirectionalLight.color.rgb * gDirectionalLight.intensity * cos;
        }

        // --- スポットライト의 計算 ---
        float32_t3 spotLightingColor = float32_t3(0.0f, 0.0f, 0.0f);
        if (gDirectionalLight.enableSpotLight != 0)
        {
            float32_t3 spotLightDirection = normalize(gDirectionalLight.spotLightPos - input.worldPosition);
            float distance = length(gDirectionalLight.spotLightPos - input.worldPosition);
            float cosAngle = dot(-spotLightDirection, normalize(gDirectionalLight.spotLightDir));
            
            if (cosAngle > gDirectionalLight.spotLightCosAngle)
            {
                // 距離減衰
                float attenuation = saturate(1.0f - (distance / gDirectionalLight.spotLightRange));
                // コーンの境界付近を滑らかにする (フォールオフ)
                float angleFalloff = saturate((cosAngle - gDirectionalLight.spotLightCosAngle) / (1.0f - gDirectionalLight.spotLightCosAngle));
                
                // 拡散反射
                float NdotLSpot = dot(normalize(input.normal), spotLightDirection);
                float cosSpot = pow(NdotLSpot * 0.5f + 0.5f, 2.0f);
                
                spotLightingColor = gDirectionalLight.spotLightColor.rgb * gDirectionalLight.spotLightIntensity * cosSpot * attenuation * angleFalloff;
            }
        }
        
        float32_t3 lightingColor = dirLightingColor + spotLightingColor;
        
        output.color.rgb = textureColor.rgb * gMaterial.color.rgb * lightingColor;
        output.color.a = textureColor.a * gMaterial.color.a;

        // --- 【追加】環境マップ (Environment Map) の計算 ---
        if (gMaterial.environmentCoefficient > 0.0f)
        {
            // カメラからピクセルへのベクトル
            float32_t3 cameraToPosition = normalize(input.worldPosition - gCamera.worldPosition);
            // 法線を使ってベクトルを反射
            float32_t3 reflectedVector = reflect(cameraToPosition, normalize(input.normal));
            // 環境マップからサンプリング
            float32_t4 environmentColor = gEnvironmentTexture.Sample(gSampler, reflectedVector);
            
            // 映り込みの強さ（スケール）を掛けて加算
            output.color.rgb += environmentColor.rgb * gMaterial.environmentCoefficient;
        }
        
    }
    else
    {
        // ライティングなし
        output.color = textureColor * gMaterial.color;
    }

    return output;
}