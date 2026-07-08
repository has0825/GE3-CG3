#include "CopyImage.hlsli"

Texture2D<float32_t4> gTexture : register(t0);
SamplerState gSampler : register(s0);

cbuffer VignetteParameter : register(b0) {
    float scale;
    float power;
}

struct PixelShaderOutput {
    float32_t4 color : SV_TARGET0;
};

PixelShaderOutput main(VertexShaderOutput input) {
    PixelShaderOutput output;

    // power が 10.0f 以上の場合は、雷撃ポストエフェクトモードとして処理
    // 超過分 (0.0 〜 1.0) を雷の演出強度として使用する
    if (power >= 10.0f) {
        float intensity = saturate(power - 10.0f); // 0.0 (通常) 〜 1.0 (最大雷撃フラッシュ)
        
        // scale は雷が落ちた画面上の UV-X 座標 (0.2, 0.5, 0.8 など)
        float targetUvX = scale;
        float dX = abs(input.texcoord.x - targetUvX);

        // 1. 色収差 (Chromatic Aberration) のシミュレーション
        // 画面の端、および雷の主幹に近いほど激しく歪むようにブレンド
        float2 distFromCenter = input.texcoord - float2(0.5f, 0.5f);
        float distSq = dot(distFromCenter, distFromCenter);
        
        // 雷の落ちた中心に近いほど強く揺れる色ズレ
        float localGlowFactor = saturate(1.0f - dX * 4.0f);
        float2 shift = distFromCenter * (0.0f * intensity * (distSq + localGlowFactor * 0.25f));

        float r = gTexture.Sample(gSampler, input.texcoord + shift).r;
        float g = gTexture.Sample(gSampler, input.texcoord).g;
        float b = gTexture.Sample(gSampler, input.texcoord - shift).b;
        float a = gTexture.Sample(gSampler, input.texcoord).a;
        
        output.color = float32_t4(r, g, b, a);

        // 2. ローカル空間のボリュメトリック・ライト (Glowのアッテネーション)
        // 画面全体ではなく、雷の周囲の空気だけが妖しく光るようにする
        // 式: Glow = 1.0 / (a * d^2 + b * d + c)
        // a = 32.0f, b = 6.0f, c = 1.0f で一定範囲外は完全にフェード
        float glow = 1.0f / (32.0f * dX * dX + 6.0f * dX + 1.0f);
        float32_t3 flashColor = float32_t3(0.68f, 0.12f, 1.0f); // 紫色の雷光

        // 3. 自動露出とフラッシュ (Exposure & Flash Burst)
        // 激突の瞬間は雷の中心に近いほど激しく白飛びさせ、その後一時的に露出を落とす
        if (intensity > 0.65f) {
            float burst = (intensity - 0.65f) / 0.35f;
            // 雷に近い領域だけを劇的に白飛びさせる
            output.color.rgb = lerp(output.color.rgb, float32_t3(1.7f, 1.5f, 2.0f), burst * 0.9f * glow);
        }

        // 暗順応（カメラの目が眩んだように露出を落とす）
        float exposure = 1.0f;
        if (intensity > 0.0f && intensity <= 0.65f) {
            exposure = lerp(0.35f, 1.0f, intensity / 0.65f); // 激突直後は 0.35 倍まで暗く沈み、徐々に戻る
        }
        output.color.rgb *= exposure;

        // ボリュメトリックな光の散乱（空気中の紫色の残光）を雷の周囲に加算
        output.color.rgb += flashColor * (0.65f * intensity * glow);
        
    } else {
        // 通常のビネット効果
        output.color = gTexture.Sample(gSampler, input.texcoord);
        float32_t2 correct = input.texcoord * (1.0f - input.texcoord.yx);
        float vignette = correct.x * correct.y * scale;
        vignette = saturate(pow(vignette, power));
        output.color.rgb *= vignette;
    }

    return output;
}
