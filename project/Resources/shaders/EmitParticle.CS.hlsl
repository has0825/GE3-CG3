static const uint32_t kMaxParticles = 131072;

struct Particle {
    float3 translate;
    float3 scale;
    float lifeTime;
    float3 velocity;
    float currentTime;
    float4 color;
};

struct EmitterSphere {
    float3 translate;
    float radius;
    uint32_t count;
    float frequency;
    float frequencyTime;
    uint32_t emit;
};

struct PerFrame {
    float time;
    float deltaTime;
};

RWStructuredBuffer<Particle> gParticles : register(u0);
RWStructuredBuffer<uint32_t> gEmitterIndex : register(u1); // 累積インデックスとして流用
RWStructuredBuffer<uint32_t> gFreeList : register(u2);       // ダミー (未使用)
ConstantBuffer<EmitterSphere> gEmitter : register(b0);
ConstantBuffer<PerFrame> gPerFrame : register(b1);

// 乱数生成関数
float3 rand3dTo3d(float3 value) {
    return float3(
        frac(sin(dot(value, float3(12.9898, 78.233, 45.164))) * 43758.5453),
        frac(sin(dot(value, float3(13.9898, 79.233, 46.164))) * 44758.5453),
        frac(sin(dot(value, float3(14.9898, 80.233, 47.164))) * 45758.5453)
    );
}

float rand3dTo1d(float3 value) {
    return frac(sin(dot(value, float3(12.9898, 78.233, 45.164))) * 43758.5453);
}

// 資料に基づく RandomGenerator クラス
class RandomGenerator {
    float3 seed;
    float3 Generate3d() {
        seed = rand3dTo3d(seed);
        return seed;
    }
    float Generate1d() {
        float result = rand3dTo1d(seed);
        seed.x = result;
        return result;
    }
};

[numthreads(1024, 1, 1)]
void main(uint3 DTid : SV_DispatchThreadID) {
    uint32_t threadIndex = DTid.x;
    if (threadIndex >= gEmitter.count) {
        return;
    }

    RandomGenerator generator;
    // スレッドIDと時間に基づくユニークなシード
    generator.seed = (DTid + gPerFrame.time) * (gPerFrame.time + 0.137f);

    int32_t emitIndex;
    InterlockedAdd(gEmitterIndex[0], 1, emitIndex);

    // 累積インデックスを最大数で割った余りをパーティクルインデックスとする
    uint32_t particleIndex = uint32_t(emitIndex) % kMaxParticles;
    
    // スケールを小さめにして高密度に表現する (0.3〜1.0のランダム)
    float sz = 0.3f + generator.Generate1d() * 0.7f;
    gParticles[particleIndex].scale = float3(sz, sz, sz);

    // 位置 (地面から少し上に向かってオフセットし、地面下に埋もれないようにする)
    float3 offset;
    offset.x = (generator.Generate1d() * 2.0f - 1.0f) * 6.0f;
    offset.y = generator.Generate1d() * 4.0f; // 地面から0〜4m上に散らす
    offset.z = (generator.Generate1d() * 2.0f - 1.0f) * 6.0f;
    gParticles[particleIndex].translate = gEmitter.translate + offset;

    // 土と石のランダムな配色 (茶色系〜灰色系)
    float r = generator.Generate1d();
    float3 baseColor;
    if (r < 0.5f) {
        // 土（茶色〜暗褐色）
        baseColor = lerp(float3(0.25f, 0.15f, 0.08f), float3(0.48f, 0.32f, 0.18f), generator.Generate1d());
    } else {
        // 石（暗灰色〜明灰色）
        baseColor = lerp(float3(0.20f, 0.20f, 0.20f), float3(0.55f, 0.55f, 0.55f), generator.Generate1d());
    }
    gParticles[particleIndex].color = float4(baseColor, 1.0f);
    
    // 速度 (当初の約0.8倍に調整し、遅すぎず速すぎないダイナミックな飛び散りを実現する)
    float angle = generator.Generate1d() * 3.14159265f * 2.0f;
    float speed = 8.0f + generator.Generate1d() * 16.0f; // 水平方向の拡散速度 (8〜24)
    gParticles[particleIndex].velocity.x = cos(angle) * speed;
    gParticles[particleIndex].velocity.y = 20.0f + generator.Generate1d() * 25.0f; // 上方向への噴出 (20〜45)
    // プレイヤー方向（手前 -Z方向）への吹き飛ばしバイアス (-12.0f〜-24.0f)
    gParticles[particleIndex].velocity.z = sin(angle) * speed - (12.0f + generator.Generate1d() * 12.0f);
    
    // 寿命 (1.5〜3.5秒、自機を通り過ぎるまで長持ちさせる)
    gParticles[particleIndex].lifeTime = 1.5f + generator.Generate1d() * 2.0f;
    gParticles[particleIndex].currentTime = 0.0f;
}
