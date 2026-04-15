#include "Object3d.hlsli"

ConstantBuffer<Material> gMaterial : register(b0);
ConstantBuffer<DirectionalLight> gDirectionalLight : register(b1);

Texture2D<float32_t4> gTexture : register(t0);
SamplerState gSampler : register(s0);

struct PixelShaderOutput
{
    float32_t4 color : SV_Target0;
};

PixelShaderOutput main(VertexShaderOutput input)
{
    PixelShaderOutput output;

    // UV座標を同次座標系に拡張して、アフィン変換を適用する (x, y, 1.0)
    float4 transformedUV = mul(float32_t4(input.texcoord, 0.0f, 1.0f), gMaterial.uvTransform);
    
    // 変換されたUV座標を使ってテクスチャをサンプリング
    float32_t4 textureColor = gTexture.Sample(gSampler, transformedUV.xy);
        
    if (gMaterial.enableLighting != 0) // Lightingする場合
    {
        // 法線を正規化（ピクセル間補間により長さが1ではなくなる可能性があるため）
        float32_t3 normal = normalize(input.normal);
        
        // ライトの逆方向ベクトル（光が来る方向に向かうベクトル）
        float32_t3 lightDir = normalize(-gDirectionalLight.direction);
        
        // ハーフランバート反射の計算（光の当たり具合を計算し、少し明るめに補正する）
        float NdotL = dot(normal, lightDir);
        float cos = pow(NdotL * 0.5f + 0.5f, 2.0f);
        
        // テクスチャ色 × マテリアル色 × ライト色 × 反射率 × 輝度
        output.color.rgb = textureColor.rgb * gMaterial.color.rgb * gDirectionalLight.color.rgb * cos * gDirectionalLight.intensity;
        output.color.a = textureColor.a * gMaterial.color.a;
    }
    else // Lightingしない場合
    {
        output.color = textureColor * gMaterial.color;
    }
    
    return output;
}