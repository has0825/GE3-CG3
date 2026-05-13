static const uint kMaxParticles = 1024;

struct Particle {
    float3 translate;
    float3 scale;
    float lifeTime;
    float3 velocity;
    float currentTime;
    float4 color;
};

RWStructuredBuffer<Particle> gParticles : register(u0);
RWStructuredBuffer<int32_t> gFreeListIndex : register(u1);
RWStructuredBuffer<uint32_t> gFreeList : register(u2);

[numthreads(1024, 1, 1)]
void main(uint3 DTid : SV_DispatchThreadID) {
    uint particleIndex = DTid.x;
    if (particleIndex < kMaxParticles) {
        // パーティクルの初期化
        gParticles[particleIndex] = (Particle)0;
        
        // FreeListを連番で初期化
        gFreeList[particleIndex] = particleIndex;
    }

    if (particleIndex == 0) {
        // Indexが末尾を指すようにする
        gFreeListIndex[0] = kMaxParticles - 1;
    }
}
