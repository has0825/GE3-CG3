static const uint kMaxParticles = 131072;

struct Particle {
    float3 translate;
    float3 scale;
    float lifeTime;
    float3 velocity;
    float currentTime;
    float4 color;
};

RWStructuredBuffer<Particle> gParticles : register(u0);
RWStructuredBuffer<uint32_t> gEmitterIndex : register(u1); // 累積インデックスとして流用
RWStructuredBuffer<uint32_t> gFreeList : register(u2);       // ダミー (未使用)

[numthreads(1024, 1, 1)]
void main(uint3 DTid : SV_DispatchThreadID) {
    uint particleIndex = DTid.x;
    if (particleIndex < kMaxParticles) {
        // パーティクルの初期化
        gParticles[particleIndex] = (Particle)0;
    }

    if (particleIndex == 0) {
        // 累積インデックスを0にリセット
        gEmitterIndex[0] = 0;
    }
}
