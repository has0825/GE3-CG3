#include "CopyImage.hlsli"

Texture2D<float32_t4> gTexture : register(t0);       // 元のカラー画像
Texture2D<float32_t> gDepthTexture : register(t1);   // 深度画像
SamplerState gSampler : register(s0);                // カラー用バイリニアサンプラー
SamplerState gPointSampler : register(s1);           // 深度用ポイントサンプラー

struct PixelShaderOutput {
    float32_t4 color : SV_TARGET0;
};

// 3x3 Prewittフィルターのカーネル
static const float32_t kPrewittHorizontalKernel[3][3] = {
    { -1.0f / 6.0f, 0.0f, 1.0f / 6.0f},
    { -1.0f / 6.0f, 0.0f, 1.0f / 6.0f},
    { -1.0f / 6.0f, 0.0f, 1.0f / 6.0f},
};

static const float32_t kPrewittVerticalKernel[3][3] = {
    { -1.0f / 6.0f, -1.0f / 6.0f, -1.0f / 6.0f },
    {  0.0f,  0.0f,  0.0f },
    {  1.0f / 6.0f,  1.0f / 6.0f,  1.0f / 6.0f },
};

// 深度値を線形深度に変換する関数
// カメラ設定: nearClip_ = 5.0f, farClip_ = 1500.0f
float32_t LinearDepth(float32_t d) {
    float32_t n = 5.0f;
    float32_t f = 1500.0f;
    return n * f / (f - d * (f - n));
}

PixelShaderOutput main(VertexShaderOutput input) {
    uint32_t width, height;
    gDepthTexture.GetDimensions(width, height);
    float32_t2 uvStepSize = float32_t2(rcp((float32_t)width), rcp((float32_t)height));

    float32_t2 difference = float32_t2(0.0f, 0.0f);

    for (int32_t x = 0; x < 3; ++x) {
        for (int32_t y = 0; y < 3; ++y) {
            float32_t2 texOffset = float32_t2((float32_t)y - 1.0f, (float32_t)x - 1.0f);
            float32_t2 texcoord = input.texcoord + texOffset * uvStepSize;
            
            // 深度テクスチャから深度値をサンプリング (ポイントサンプラーを使用)
            float32_t rawDepth = gDepthTexture.Sample(gPointSampler, texcoord);
            // 線形化
            float32_t linearDepth = LinearDepth(rawDepth);
            
            difference.x += linearDepth * kPrewittHorizontalKernel[x][y];
            difference.y += linearDepth * kPrewittVerticalKernel[x][y];
        }
    }

    // エッジの強さを計算
    float32_t weight = length(difference);
    
    // エッジの検出感度を調整する
    weight = saturate(weight * 2.0f);

    PixelShaderOutput output;
    float32_t3 originalColor = gTexture.Sample(gSampler, input.texcoord).rgb;
    
    // 輪郭線を黒で重ね合わせる
    output.color.rgb = (1.0f - weight) * originalColor;
    output.color.a = 1.0f;

    return output;
}
