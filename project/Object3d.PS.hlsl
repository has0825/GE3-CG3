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
    
    // ライティング計算
    if (gMaterial.enableLighting != 0)
    {
        // --- 既存のPunctual Light (Directional Light) の計算 ---
        float NdotL = dot(normalize(input.normal), -gDirectionalLight.direction);
        float cos = pow(NdotL * 0.5f + 0.5f, 2.0f); // Half Lambert
        float32_t3 lightingColor = gDirectionalLight.color.rgb * gDirectionalLight.intensity * cos;
        
        output.color.rgb = textureColor.rgb * gMaterial.color.rgb * lightingColor;
        output.color.a = textureColor.a * gMaterial.color.a;

        // --- 【追加】環境マップ (Environment Map) の計算 ---
        // カメラからピクセルへのベクトル
        float32_t3 cameraToPosition = normalize(input.worldPosition - gCamera.worldPosition);
        // 法線を使ってベクトルを反射
        float32_t3 reflectedVector = reflect(cameraToPosition, normalize(input.normal));
        // 環境マップからサンプリング
        float32_t4 environmentColor = gEnvironmentTexture.Sample(gSampler, reflectedVector);
        
        // 映り込みの強さ（スケール）を掛けて加算
        output.color.rgb += environmentColor.rgb * gMaterial.environmentCoefficient;
        
    }
    else
    {
        // ライティングなし
        output.color = textureColor * gMaterial.color;
    }

    return output;
}