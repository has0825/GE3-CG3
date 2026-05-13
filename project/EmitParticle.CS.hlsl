static const uint32_t kMaxParticles = 1024;

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
RWStructuredBuffer<int32_t> gFreeCounter : register(u1);
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

[numthreads(1, 1, 1)]
void main(uint3 DTid : SV_DispatchThreadID) {
    if (gEmitter.emit != 0) {
        RandomGenerator generator;
        generator.seed = (DTid + gPerFrame.time) * gPerFrame.time;

        for (uint32_t i = 0; i < gEmitter.count; ++i) {
            int32_t originalIndex;
            InterlockedAdd(gFreeCounter[0], 1, originalIndex);
            int32_t particleIndex = originalIndex % kMaxParticles;

            if (particleIndex < kMaxParticles) {
                // スケールを資料に合わせて小さく、密度の高い見た目に
                float s = generator.Generate1d() * 0.8f + 0.2f;
                gParticles[particleIndex].scale = float3(s, s, s);

                // 位置
                gParticles[particleIndex].translate = gEmitter.translate + (generator.Generate3d() - 0.5f) * gEmitter.radius;

                // 虹色（レインボー）の配色：インデックスと時間で色を回す
                float h = frac(particleIndex * 0.05f + gPerFrame.time * 0.1f + generator.Generate1d() * 0.1f);
                float3 rgb;
                float h6 = h * 6.0f;
                if (h6 < 1.0f) rgb = float3(1.0f, h6, 0.0f);
                else if (h6 < 2.0f) rgb = float3(2.0f - h6, 1.0f, 0.0f);
                else if (h6 < 3.0f) rgb = float3(0.0f, 1.0f, h6 - 2.0f);
                else if (h6 < 4.0f) rgb = float3(0.0f, 4.0f - h6, 1.0f);
                else if (h6 < 5.0f) rgb = float3(h6 - 4.0f, 0.0f, 1.0f);
                else rgb = float3(1.0f, 0.0f, 6.0f - h6);

                gParticles[particleIndex].color = float4(rgb, 0.1f); // さらに薄く（0.1）
                
                // 速度
                gParticles[particleIndex].velocity = (generator.Generate3d() - 0.5f) * 1.5f;
                
                // 寿命
                gParticles[particleIndex].lifeTime = 1.0f + generator.Generate1d() * 2.0f;
                gParticles[particleIndex].currentTime = 0.0f;
            }
        }
    }
}
