#include "Object3d.hlsli"

ConstantBuffer<Material> gMaterial : register(b0);
ConstantBuffer<DirectionalLight> gDirectionalLight : register(b1);
ConstantBuffer<Camera> gCamera : register(b2);

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

    // ★修正: 色によるdiscard（黒を透明にする処理）を削除しました。
    // これがあるとモンスターボールの黒いラインが消えてしまいます。
    // もし画像の透明度を使いたい場合は textureColor.a を確認してください。

    // ライティングなしならそのまま返す
    if (gMaterial.enableLighting == 0)
    {
        output.color = gMaterial.color * textureColor;
        return output;
    }

    // --- ライティング計算 ---
    float3 normal = normalize(input.normal);
    float3 lightDir = normalize(-gDirectionalLight.direction);
    float3 viewDir = normalize(gCamera.worldPosition - input.worldPosition); // 視線ベクトル

    // 拡散反射 (Lambert)
    // N dot L (法線とライト方向の内積)
    float NdotL = dot(normal, lightDir);
    float cos = saturate(NdotL); // max(0, val) と同じ意味

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
        float3 reflectDir = reflect(gDirectionalLight.direction, normal); // HLSLのreflectは入射ベクトルと法線をとる
        // R dot V (反射と視線の内積)
        float RdotV = dot(reflectDir, viewDir);
        float spec = pow(saturate(RdotV), gMaterial.shininess);
        specular = lightColor * spec;
    }
    else if (gMaterial.enableLighting == 3) // 3: Blinn-Phong Reflection
    {
        // ハーフベクトル H = (L + V) / |L + V|
        float3 halfVector = normalize(lightDir + viewDir);
        // N dot H (法線とハーフベクトルの内積)
        float NdotH = dot(normal, halfVector);
        float spec = pow(saturate(NdotH), gMaterial.shininess);
        specular = lightColor * spec;
    }

    // 最終カラー計算: (拡散光 + 鏡面光) * テクスチャ + 環境光(今回は省略)
    // テクスチャの色は拡散反射部分に乗算し、Specularは光の色として加算するのが一般的です
    output.color.rgb = (gMaterial.color.rgb * textureColor.rgb * lightColor * cos) + specular;
    output.color.a = gMaterial.color.a * textureColor.a;

    return output;
}