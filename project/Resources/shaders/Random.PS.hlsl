#include "CopyImage.hlsli"

Texture2D<float32_t4> gTexture : register(t0);
SamplerState gSampler : register(s0);

cbuffer RandomParameter : register(b0) {
    float32_t time;
    float32_t noiseScale;
    float32_t noiseStrength;
    float32_t isColorNoise;
    float32_t isMultiplyNoise;
    float32_t3 padding;
}

struct PixelShaderOutput {
    float32_t4 color : SV_TARGET0;
};

// 2Dから1Dの疑似乱数生成
float32_t rand2dTo1d(float32_t2 value, float32_t2 dotDir = float32_t2(12.9898f, 78.233f)) {
    float32_t2 smallValue = sin(value);
    float32_t random = dot(smallValue, dotDir);
    random = frac(sin(random) * 43758.5453f);
    return random;
}

PixelShaderOutput main(VertexShaderOutput input) {
    PixelShaderOutput output;
    output.color = gTexture.Sample(gSampler, input.texcoord);

    // 経過時間timeをシード値のオフセットとして加算する（ズームバグの防止）
    // timeに一定の係数を掛けてフレームごとの変化を激しくランダムにする
    float32_t2 seed = input.texcoord * noiseScale + float32_t2(time * 51.37f, time * 37.19f);
    
    float32_t random = rand2dTo1d(seed);
    
    float32_t3 noiseColor;
    if (isColorNoise > 0.5f) {
        // カラーノイズ：RGBそれぞれで異なる乱数
        float32_t randomR = random;
        float32_t randomG = rand2dTo1d(seed + float32_t2(13.41f, 7.82f));
        float32_t randomB = rand2dTo1d(seed + float32_t2(5.19f, 21.63f));
        noiseColor = float32_t3(randomR, randomG, randomB);
    } else {
        // モノクロノイズ
        noiseColor = float32_t3(random, random, random);
    }
    
    // 生成した乱数の値をノイズ強度に基づいて元の画像に合成
    float32_t3 finalColor;
    if (isMultiplyNoise > 0.5f) {
        // 乗算：生成した乱数の値（noiseColor）を元の画像に乗算する
        float32_t3 multiplyColor = output.color.rgb * noiseColor;
        finalColor = lerp(output.color.rgb, multiplyColor, noiseStrength);
    } else {
        // 直接（TVの砂嵐）：元の画像と直接lerpする
        finalColor = lerp(output.color.rgb, noiseColor, noiseStrength);
    }
    output.color.rgb = finalColor;

    return output;
}
