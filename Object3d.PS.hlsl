#include "Object3d.hlsli"

ConstantBuffer<Material> gMaterial : register(b0);
ConstantBuffer<DirectionalLight> gDirectionalLight : register(b1);
ConstantBuffer<Camera> gCamera : register(b2); // カメラ座標を追加

Texture2D<float32_t4> gTexture : register(t0);
SamplerState gSampler : register(s0);

struct PixelShaderOutput
{
    float32_t4 color : SV_Target0;
};

PixelShaderOutput main(VertexShaderOutput input)
{
    PixelShaderOutput output;

    // テクスチャサンプリング
    float4 transformedUV = mul(float32_t4(input.texcoord, 0.0f, 1.0f), gMaterial.uvTransform);
    float32_t4 textureColor = gTexture.Sample(gSampler, transformedUV.xy);

    // 簡易的なアルファテスト (黒に近い色を抜く場合)
    if (textureColor.r < 0.1f && textureColor.g < 0.1f && textureColor.b < 0.1f)
    {
        discard;
    }

    // ライティングなしならそのまま返す
    if (gMaterial.enableLighting == 0)
    {
        output.color = gMaterial.color * textureColor;
        return output;
    }

    // --- ライティング計算の準備 ---
    float3 normal = normalize(input.normal);
    float3 lightDir = normalize(-gDirectionalLight.direction);
    float3 viewDir = normalize(gCamera.worldPosition - input.worldPosition); // 視線ベクトル

    // 拡散反射 (Lambert)
    // N dot L (法線とライト方向の内積)
    float NdotL = dot(normal, lightDir);
    float cos = max(0.0f, NdotL);

    // 鏡面反射 (Specular)
    float3 specular = float3(0.0f, 0.0f, 0.0f);
    float3 lightColor = gDirectionalLight.color.rgb * gDirectionalLight.intensity;

    if (gMaterial.enableLighting == 1) // 1: Lambert (拡散のみ)
    {
        specular = float3(0.0f, 0.0f, 0.0f);
    }
    else if (gMaterial.enableLighting == 2) // 2: Phong Reflection
    {
        // 反射ベクトル R = reflect(-L, N)
        float3 reflectDir = reflect(-lightDir, normal);
        // R dot V (反射と視線の内積)
        float RdotV = dot(reflectDir, viewDir);
        float spec = pow(max(0.0f, RdotV), gMaterial.shininess);
        specular = lightColor * spec;
    }
    else if (gMaterial.enableLighting == 3) // 3: Blinn-Phong Reflection
    {
        // ハーフベクトル H = (L + V) / |L + V|
        float3 halfVector = normalize(lightDir + viewDir);
        // N dot H (法線とハーフベクトルの内積)
        float NdotH = dot(normal, halfVector);
        float spec = pow(max(0.0f, NdotH), gMaterial.shininess);
        specular = lightColor * spec;
    }

    // 最終カラー計算: (拡散光 + 鏡面光) * テクスチャ
    // 環境光成分などは一旦考慮せず、シンプルに加算
    output.color.rgb = (gMaterial.color.rgb * textureColor.rgb * lightColor * cos) + specular;
    output.color.a = gMaterial.color.a * textureColor.a;

    return output;
}