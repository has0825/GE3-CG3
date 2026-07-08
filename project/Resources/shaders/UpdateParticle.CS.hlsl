static const uint32_t kMaxParticles = 131072;

struct Particle {
    float3 translate;
    float3 scale;
    float lifeTime;
    float3 velocity;
    float currentTime;
    float4 color;
};

struct PerFrame {
    float time;
    float deltaTime;
};

RWStructuredBuffer<Particle> gParticles : register(u0);
RWStructuredBuffer<uint32_t> gEmitterIndex : register(u1); // 累積インデックスとして流用
RWStructuredBuffer<uint32_t> gFreeList : register(u2);       // ダミー (未使用)
ConstantBuffer<PerFrame> gPerFrame : register(b0);

[numthreads(1024, 1, 1)]
void main(uint3 DTid : SV_DispatchThreadID) {
    uint particleIndex = DTid.x;
    if (particleIndex < kMaxParticles) {
        if (gParticles[particleIndex].color.a != 0) {
            float dt = gPerFrame.deltaTime;
            
            // 位置の更新 (deltaTimeを適用)
            gParticles[particleIndex].translate += gParticles[particleIndex].velocity * dt;
            
            // 重力適用
            gParticles[particleIndex].velocity.y -= 9.8f * 4.0f * dt;
            
            // 接地判定 (地面kFloorY = -20.0fに当たったら反発・摩擦減衰)
            float kFloorY = -20.0f;
            if (gParticles[particleIndex].translate.y < kFloorY) {
                gParticles[particleIndex].translate.y = kFloorY;
                gParticles[particleIndex].velocity.y = -gParticles[particleIndex].velocity.y * 0.35f; // 反発
                gParticles[particleIndex].velocity.x *= 0.55f; // 摩擦
                gParticles[particleIndex].velocity.z *= 0.55f;
                
                if (abs(gParticles[particleIndex].velocity.y) < 1.0f) {
                    gParticles[particleIndex].velocity = float3(0.0f, 0.0f, 0.0f);
                }
            }

            gParticles[particleIndex].currentTime += dt;
            
            float alpha = 1.0f - (gParticles[particleIndex].currentTime / gParticles[particleIndex].lifeTime);
            gParticles[particleIndex].color.a = saturate(alpha);

            if (gParticles[particleIndex].color.a == 0) {
                // スケールに0を入れておいてVertexShader出力で棄却されるようにする
                gParticles[particleIndex].scale = float3(0.0f, 0.0f, 0.0f);
            }
        }
    }
}
