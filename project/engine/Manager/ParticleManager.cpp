#include "ParticleManager.h"
#include "D3D12Util.h"
#include "SrvManager.h"
#include "MathUtil.h"
#include <cassert>
#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846f
#endif

static Vector3 Subtract(const Vector3& v1, const Vector3& v2) {
    return { v1.x - v2.x, v1.y - v2.y, v1.z - v2.z };
}

static float Length(const Vector3& v) {
    return std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
}

void ParticleManager::Initialize(ID3D12Device* device) {
    device_ = device;

    std::random_device seedGenerator;
    randomEngine_.seed(seedGenerator());

    // ---------------------------------------------------------
    // 1. 通常/回転パーティクル用 (最大2000インスタンス)
    // ---------------------------------------------------------
    instancingResource_ = CreateBufferResource(device_, sizeof(ParticleForGPU) * kNumInstances);
    instancingResource_->Map(0, nullptr, reinterpret_cast<void**>(&instancingData_));

    uint32_t instancingSrvIndex = SrvManager::GetInstance()->Allocate();
    instancingSrvHandleGPU_ = SrvManager::GetInstance()->GetGPUDescriptorHandle(instancingSrvIndex);
    SrvManager::GetInstance()->CreateSRVforStructuredBuffer(
        instancingSrvIndex, 
        instancingResource_.Get(), 
        kNumInstances, 
        sizeof(ParticleForGPU)
    );

    particles_.resize(kNumInstances);
    for (uint32_t i = 0; i < kNumInstances; ++i) {
        particles_[i].currentTime = 9999.0f; // 初期は非表示（寿命切れ状態）
        particles_[i].lifeTime = 1.0f;
    }

    // ---------------------------------------------------------
    // 2. リングパーティクル用 (最大100インスタンス)
    // ---------------------------------------------------------
    ringInstancingResource_ = CreateBufferResource(device_, sizeof(ParticleForGPU) * kRingInstanceCount);
    ringInstancingResource_->Map(0, nullptr, reinterpret_cast<void**>(&ringInstancingData_));

    uint32_t ringSrvIndex = SrvManager::GetInstance()->Allocate();
    ringInstancingSrvHandleGPU_ = SrvManager::GetInstance()->GetGPUDescriptorHandle(ringSrvIndex);
    SrvManager::GetInstance()->CreateSRVforStructuredBuffer(
        ringSrvIndex, 
        ringInstancingResource_.Get(), 
        kRingInstanceCount, 
        sizeof(ParticleForGPU)
    );

    ringParticles_.resize(kRingInstanceCount);
    for (uint32_t i = 0; i < kRingInstanceCount; ++i) {
        ringParticles_[i].currentTime = 9999.0f;
        ringParticles_[i].lifeTime = 1.0f;
    }

    // ---------------------------------------------------------
    // 3. シリンダーパーティクル用 (最大50インスタンス)
    // ---------------------------------------------------------
    cylinderInstancingResource_ = CreateBufferResource(device_, sizeof(ParticleForGPU) * kCylinderInstanceCount);
    cylinderInstancingResource_->Map(0, nullptr, reinterpret_cast<void**>(&cylinderInstancingData_));

    uint32_t cylinderSrvIndex = SrvManager::GetInstance()->Allocate();
    cylinderInstancingSrvHandleGPU_ = SrvManager::GetInstance()->GetGPUDescriptorHandle(cylinderSrvIndex);
    SrvManager::GetInstance()->CreateSRVforStructuredBuffer(
        cylinderSrvIndex, 
        cylinderInstancingResource_.Get(), 
        kCylinderInstanceCount, 
        sizeof(ParticleForGPU)
    );

    cylinderParticles_.resize(kCylinderInstanceCount);
    for (uint32_t i = 0; i < kCylinderInstanceCount; ++i) {
        cylinderParticles_[i].currentTime = 9999.0f;
        cylinderParticles_[i].lifeTime = 1.0f;
    }
}

void ParticleManager::Update(
    const Matrix4x4& viewProjectionMatrix, 
    const Matrix4x4& billboardMatrix, 
    float deltaTime,
    float cameraZ,
    const Vector3& fighterWorldPos,
    bool isBoosting,
    bool isFighterMode,
    int currentEffect,
    const Vector3& emitterPos) {

    // ── 1. 通常 / 回転パーティクルの更新 ──
    Vector3 leftJetPos = { 0.0f, 0.0f, 0.0f };
    Vector3 rightJetPos = { 0.0f, 0.0f, 0.0f };
    if (isFighterMode) {
        leftJetPos  = { fighterWorldPos.x - 0.3f, fighterWorldPos.y + 0.8f, fighterWorldPos.z - 3.0f };
        rightJetPos = { fighterWorldPos.x + 0.8f, fighterWorldPos.y + 0.8f, fighterWorldPos.z - 3.0f };
    }

    for (uint32_t i = 0; i < kNumInstances; ++i) {
        bool isStardust = (isFighterMode && i >= kNumInstances / 2);

        // 自律リポップのチェック (寿命が切れた、あるいはスターダストが画面外へ行った場合)
        if (particles_[i].currentTime >= particles_[i].lifeTime ||
            (isStardust && particles_[i].position.z < cameraZ - 10.0f)) {
            
            if (isFighterMode) {
                if (isStardust) {
                    particles_[i] = MakeNewParticle(51, fighterWorldPos, cameraZ, fighterWorldPos, isBoosting); // 51: kTypeStardust
                } else {
                    Vector3 jetPos = (i % 2 == 0) ? leftJetPos : rightJetPos;
                    particles_[i] = MakeNewParticle(50, jetPos, cameraZ, fighterWorldPos, isBoosting); // 50: kTypeJetExhaust
                }
            } else {
                // デモモードなど非戦闘機モードでは自動リポップさせず、非表示状態を維持する
                particles_[i].color.w = 0.0f;
                particles_[i].currentTime = particles_[i].lifeTime;
            }
        }

        // ブースト時のスターダスト速度・スケール調整
        float speedMultiplier = 1.0f;
        Vector3 finalScale = particles_[i].scale;
        if (isStardust) {
            if (isBoosting) {
                speedMultiplier = 8.5f;
                finalScale.z *= 5.0f;
            }
        }

        // 個別エフェクト特殊挙動の適用
        if (particles_[i].effectType == 10) {
            // 氷結：急速な減速
            float drag = 1.0f - deltaTime * 6.5f;
            if (drag < 0.0f) drag = 0.0f;
            particles_[i].velocity.x *= drag;
            particles_[i].velocity.y *= drag;
            particles_[i].velocity.z *= drag;
        } else if (particles_[i].effectType == 11) {
            // デジタルバグ：位置のガタガタしたジッターとカクカク点滅・伸縮
            std::uniform_real_distribution<float> distJitter(-0.25f, 0.25f);
            particles_[i].position.x += distJitter(randomEngine_);
            particles_[i].position.y += distJitter(randomEngine_);
            particles_[i].position.z += distJitter(randomEngine_);
            
            if (std::uniform_real_distribution<float>(0.0f, 1.0f)(randomEngine_) < 0.3f) {
                finalScale.x *= (0.6f + distJitter(randomEngine_) * 2.0f);
            }
        } else if (particles_[i].effectType == 12) {
            // 風・竜巻：速度ベクトルの回転による螺旋上昇軌道
            float rotAngle = deltaTime * 8.5f;
            float vx = particles_[i].velocity.x;
            float vz = particles_[i].velocity.z;
            particles_[i].velocity.x = vx * std::cos(rotAngle) - vz * std::sin(rotAngle);
            particles_[i].velocity.z = vx * std::sin(rotAngle) + vz * std::cos(rotAngle);
        } else if (particles_[i].effectType == 14) {
            // カオスボイド：カオスなアメーバ風のうねり・波打ち
            particles_[i].position.x += std::sin(particles_[i].currentTime * 24.0f) * 0.12f;
            particles_[i].position.y += std::cos(particles_[i].currentTime * 18.0f) * 0.12f;
        }

        // 重力適用
        if (particles_[i].gravity != 0.0f) {
            particles_[i].velocity.y -= 18.0f * particles_[i].gravity * deltaTime;
        }

        // 速度加算
        particles_[i].position.x += particles_[i].velocity.x * deltaTime * speedMultiplier;
        particles_[i].position.y += particles_[i].velocity.y * deltaTime * speedMultiplier;
        particles_[i].position.z += particles_[i].velocity.z * deltaTime * speedMultiplier;
        
        particles_[i].currentTime += deltaTime;

        // 透明度の計算 (フェードアウト)
        float alpha = 1.0f - (particles_[i].currentTime / particles_[i].lifeTime);
        particles_[i].color.w = (std::max)(alpha, 0.0f);

        // 神聖：キラキラ点滅
        if (particles_[i].effectType == 13) {
            float blink = std::sin(particles_[i].currentTime * 40.0f) * 0.35f + 0.65f;
            particles_[i].color.w *= blink;
        }

        // ワールド行列の計算 (ビルボード Plane vs 回転 Plane)
        Matrix4x4 worldMatrix;
        if (particles_[i].type == Particle::Type::kBillboard) {
            // ビルボード適用：カメラの向きをベースに、スケールと位置を適用
            worldMatrix = Multiply(MakeScaleMatrix(finalScale), billboardMatrix);
            worldMatrix.m[3][0] = particles_[i].position.x;
            worldMatrix.m[3][1] = particles_[i].position.y;
            worldMatrix.m[3][2] = particles_[i].position.z;
        } else {
            // 回転適用：個々の3次元rotateに基づいて通常アフィン行列を適用（ヒットエフェクト等）
            worldMatrix = MakeAffineMatrix(finalScale, particles_[i].rotate, particles_[i].position);
        }

        instancingData_[i].World = worldMatrix;
        instancingData_[i].WVP = Multiply(worldMatrix, viewProjectionMatrix);
        instancingData_[i].color = particles_[i].color;
        instancingData_[i].uvTransform = particles_[i].uvTransform;
    }

    // ── 2. リングパーティクルの更新 ──
    for (uint32_t i = 0; i < kRingInstanceCount; ++i) {
        if (ringParticles_[i].currentTime < ringParticles_[i].lifeTime) {
            ringParticles_[i].currentTime += deltaTime;
            float alpha = 1.0f - (ringParticles_[i].currentTime / ringParticles_[i].lifeTime);
            ringParticles_[i].color.w = (std::max)(alpha, 0.0f);
            
            // 拡大アニメーション
            ringParticles_[i].scale.x += 15.0f * deltaTime;
            ringParticles_[i].scale.y += 15.0f * deltaTime;

            // UVスクロール (V方向に移動)
            float uvScrollV = ringParticles_[i].currentTime * -2.0f;
            ringParticles_[i].uvTransform = MakeAffineMatrix(Vector3{1.0f, 1.0f, 1.0f}, Vector3{0.0f, 0.0f, 0.0f}, Vector3{0.0f, uvScrollV, 0.0f});

            Matrix4x4 worldMatrix = MakeAffineMatrix(ringParticles_[i].scale, ringParticles_[i].rotate, ringParticles_[i].position);
            ringInstancingData_[i].World = worldMatrix;
            ringInstancingData_[i].WVP = Multiply(worldMatrix, viewProjectionMatrix);
            ringInstancingData_[i].color = ringParticles_[i].color;
            ringInstancingData_[i].uvTransform = ringParticles_[i].uvTransform;
        } else {
            ringInstancingData_[i].color.w = 0.0f;
        }
    }

    // ── 3. シリンダーパーティクルの更新 ──
    for (uint32_t i = 0; i < kCylinderInstanceCount; ++i) {
        if (cylinderParticles_[i].currentTime < cylinderParticles_[i].lifeTime) {
            cylinderParticles_[i].currentTime += deltaTime;
            float alpha = 1.0f - (cylinderParticles_[i].currentTime / cylinderParticles_[i].lifeTime);
            cylinderParticles_[i].color.w = (std::max)(alpha, 0.0f);
            
            // UVスクロール ＆ 反転
            float uvScrollU = cylinderParticles_[i].currentTime * 1.0f;
            cylinderParticles_[i].uvTransform = MakeAffineMatrix(Vector3{1.0f, -1.0f, 1.0f}, Vector3{0.0f, 0.0f, 0.0f}, Vector3{uvScrollU, 1.0f, 0.0f});

            Matrix4x4 worldMatrix = MakeAffineMatrix(cylinderParticles_[i].scale, cylinderParticles_[i].rotate, cylinderParticles_[i].position);
            cylinderInstancingData_[i].World = worldMatrix;
            cylinderInstancingData_[i].WVP = Multiply(worldMatrix, viewProjectionMatrix);
            cylinderInstancingData_[i].color = cylinderParticles_[i].color;
            cylinderInstancingData_[i].uvTransform = cylinderParticles_[i].uvTransform;
        } else {
            cylinderInstancingData_[i].color.w = 0.0f;
        }
    }
}

void ParticleManager::Draw(
    ID3D12GraphicsCommandList* commandList,
    Model* particleModel,
    Model* ringModel,
    Model* cylinderModel,
    D3D12_GPU_DESCRIPTOR_HANDLE textureSrvHandle,
    D3D12_GPU_DESCRIPTOR_HANDLE gradationSrvHandle,
    D3D12_GPU_DESCRIPTOR_HANDLE textSrvHandle) {

    // デスクリプタヒープを設定 (StructuredBufferを使用するため)
    ID3D12DescriptorHeap* descriptorHeaps[] = { SrvManager::GetInstance()->GetDescriptorHeap() };
    commandList->SetDescriptorHeaps(1, descriptorHeaps);

    // 1. 通常 / 回転板ポリゴンの描画
    if (particleModel) {
        particleModel->Draw(commandList, kNumInstances, textureSrvHandle, instancingSrvHandleGPU_);
    }

    // 2. リングの描画
    if (ringModel) {
        ringModel->Draw(commandList, kRingInstanceCount, gradationSrvHandle, ringInstancingSrvHandleGPU_);
    }

    // 3. シリンダーの描画
    if (cylinderModel) {
        cylinderModel->Draw(commandList, kCylinderInstanceCount, gradationSrvHandle, cylinderInstancingSrvHandleGPU_);
    }
}

void ParticleManager::EmitHit(const Vector3& emitterPos) {
    // 1. 火花スパーク (回転板ポリゴン) 40発
    int sparkCount = 40;
    for (uint32_t p = 0; p < kNumInstances && sparkCount > 0; ++p) {
        if (particles_[p].currentTime >= particles_[p].lifeTime) {
            particles_[p] = MakeNewParticle(4, emitterPos, 0.0f, {0,0,0}, false); // 4: kTypeHit
            sparkCount--;
        }
    }
}

void ParticleManager::EmitRing(const Vector3& emitterPos, const Vector3& color) {
    // 2. 衝撃波リング 2枚
    int ringCount = 2;
    for (uint32_t r = 0; r < kRingInstanceCount && ringCount > 0; ++r) {
        if (ringParticles_[r].currentTime >= ringParticles_[r].lifeTime) {
            
            std::uniform_real_distribution<float> distRingScale(0.3f, 1.0f);
            std::uniform_real_distribution<float> distRingRot(-M_PI * 0.3f, M_PI * 0.3f);
            std::uniform_real_distribution<float> distColor(0.0f, 1.0f);

            float ringInitScale = distRingScale(randomEngine_);
            ringParticles_[r].scale = { ringInitScale, ringInitScale, 1.0f };
            ringParticles_[r].rotate = { distRingRot(randomEngine_), distRingRot(randomEngine_), distRingRot(randomEngine_) };
            ringParticles_[r].position = emitterPos;
            ringParticles_[r].velocity = { 0.0f, 0.0f, 0.0f };
            
            // 指定された色にランダムな明度変化を加えて反映
            float brightness = 0.7f + distColor(randomEngine_) * 0.3f;
            ringParticles_[r].color = { color.x * brightness, color.y * brightness, color.z * brightness, 1.0f };
            
            ringParticles_[r].lifeTime = 0.5f + distColor(randomEngine_) * 0.5f;
            ringParticles_[r].currentTime = 0.0f;
            ringParticles_[r].uvTransform = MakeIdentity4x4();

            ringCount--;
        }
    }
}

void ParticleManager::EmitCylinder(const Vector3& emitterPos, const Vector3& color) {
    // 3. 閃光シリンダー 1本
    int cylinderCount = 1;
    for (uint32_t c = 0; c < kCylinderInstanceCount && cylinderCount > 0; ++c) {
        if (cylinderParticles_[c].currentTime >= cylinderParticles_[c].lifeTime) {
            
            std::uniform_real_distribution<float> distCylScale(1.5f, 4.0f);
            std::uniform_real_distribution<float> distCylRot(-M_PI, M_PI);
            std::uniform_real_distribution<float> distCylVel(-5.0f, 5.0f);
            std::uniform_real_distribution<float> distColor(0.0f, 1.0f);

            float cylSc = distCylScale(randomEngine_);
            cylinderParticles_[c].scale = { cylSc, cylSc, cylSc };
            cylinderParticles_[c].rotate = { distCylRot(randomEngine_), distCylRot(randomEngine_), distCylRot(randomEngine_) };
            cylinderParticles_[c].position = emitterPos;
            
            cylinderParticles_[c].velocity = {
                distCylVel(randomEngine_),
                2.0f + std::abs(distCylVel(randomEngine_)),
                distCylVel(randomEngine_)
            };

            // 指定された色にランダムな明度変化を加えて反映
            float brightness = 0.8f + distColor(randomEngine_) * 0.2f;
            cylinderParticles_[c].color = { color.x * brightness, color.y * brightness, color.z * brightness, 1.0f };

            cylinderParticles_[c].lifeTime = 0.6f + distColor(randomEngine_) * 0.6f;
            cylinderParticles_[c].currentTime = 0.0f;
            cylinderParticles_[c].uvTransform = MakeIdentity4x4();

            cylinderCount--;
        }
    }
}

void ParticleManager::EmitLaserThread(const Vector3& emitterPos, const Vector3& targetPos) {
    // ボスの口元からプレイヤーへの線分を分割してパーティクルを並べ、一瞬で繋がるビームを表現する
    int numBeams = 35; 
    std::uniform_real_distribution<float> distNoise(-0.3f, 0.3f);

    int emitCount = numBeams;
    for (uint32_t p = 0; p < kNumInstances && emitCount > 0; ++p) {
        if (particles_[p].currentTime >= particles_[p].lifeTime) {
            int index = numBeams - emitCount;
            float t = (float)index / (float)(numBeams - 1);

            // 線分上の補間座標
            Vector3 lerpedPos = {
                std::lerp(emitterPos.x, targetPos.x, t),
                std::lerp(emitterPos.y, targetPos.y, t),
                std::lerp(emitterPos.z, targetPos.z, t)
            };

            // ビームのブレ（放電ノイズ）を加える (少しバリバリさせる)
            if (t > 0.05f && t < 0.95f) { // 根本と先端は少しノイズを抑える
                lerpedPos.x += distNoise(randomEngine_);
                lerpedPos.y += distNoise(randomEngine_);
            }

            particles_[p].position = lerpedPos;
            particles_[p].velocity = { 0.0f, 0.0f, 0.0f }; // 静止
            particles_[p].rotate = { 0.0f, 0.0f, 0.0f };

            // 先端と根本は少し太く、中間は少し細くするなどしてディテールを出す
            float size = 1.0f;
            if (index == 0 || index == numBeams - 1) {
                size = 1.8f; // 起点と終点は大きめに光らせる
            } else {
                size = 1.0f + std::sin(t * (float)M_PI) * 0.4f; // 中央をやや太く
            }
            particles_[p].scale = { size, size, size };

            // すべて白色のレーザーに変更
            particles_[p].color = { 1.0f, 1.0f, 1.0f, 1.0f };

            particles_[p].lifeTime = 0.15f; // すぐ消えることでチラつき(バリバリ感)を演出
            particles_[p].currentTime = 0.0f;
            particles_[p].type = Particle::Type::kBillboard;
            particles_[p].uvTransform = MakeIdentity4x4();

            emitCount--;
        }
    }
}

ParticleManager::Particle ParticleManager::MakeNewParticle(
    int type, 
    const Vector3& emitterPos, 
    float cameraZ, 
    const Vector3& fighterWorldPos, 
    bool isBoosting) {

    Particle particle;
    particle.scale = { 1.0f, 1.0f, 1.0f };
    particle.rotate = { 0.0f, 0.0f, 0.0f };
    particle.currentTime = 0.0f;
    particle.uvTransform = MakeIdentity4x4();
    particle.type = Particle::Type::kBillboard; // 基本はビルボード
    particle.effectType = type;

    std::uniform_real_distribution<float> distPos(-1.0f, 1.0f);
    std::uniform_real_distribution<float> distVel(-1.0f, 1.0f);
    std::uniform_real_distribution<float> distColor(0.0f, 1.0f);
    std::uniform_real_distribution<float> distLife(1.0f, 3.0f);

    switch (type) {
    case 0: // kTypeExplosion
    default:
        particle.position = {
            emitterPos.x + distPos(randomEngine_) * 0.5f,
            emitterPos.y + distPos(randomEngine_) * 0.5f,
            emitterPos.z + distPos(randomEngine_) * 0.5f
        };
        particle.velocity = { distVel(randomEngine_), distVel(randomEngine_), distVel(randomEngine_) };
        particle.lifeTime = distLife(randomEngine_);
        particle.color = { distColor(randomEngine_), distColor(randomEngine_), distColor(randomEngine_), 1.0f };
        break;
        
    case 1: // kTypeFountain
        particle.position = {
            emitterPos.x + distPos(randomEngine_) * 0.2f,
            emitterPos.y,
            emitterPos.z + distPos(randomEngine_) * 0.2f
        };
        particle.velocity = { distVel(randomEngine_) * 0.5f, 2.0f + std::abs(distVel(randomEngine_)), distVel(randomEngine_) * 0.5f };
        particle.lifeTime = 2.0f;
        particle.color = { 0.2f, 0.5f, 1.0f, 1.0f };
        break;
        
    case 2: // kTypeSpiral
        {
            float angle = distPos(randomEngine_) * M_PI;
            float radius = 1.5f;
            particle.position = {
                emitterPos.x + std::cos(angle) * radius,
                emitterPos.y,
                emitterPos.z + std::sin(angle) * radius
            };
            particle.velocity = { 0.0f, 1.0f, 0.0f };
            particle.lifeTime = 3.0f;
            particle.color = { distColor(randomEngine_), distColor(randomEngine_), distColor(randomEngine_), 1.0f };
        }
        break;
        
    case 3: // kTypeRain
        particle.position = {
            emitterPos.x + distPos(randomEngine_) * 5.0f,
            emitterPos.y + 5.0f,
            emitterPos.z + distPos(randomEngine_) * 5.0f
        };
        particle.velocity = { 0.0f, -3.0f, 0.0f };
        particle.lifeTime = 3.0f;
        particle.color = { 0.8f, 0.8f, 1.0f, 1.0f };
        particle.scale = { 0.2f, 1.0f, 0.2f };
        break;
        
    case 4: // kTypeHit (板ポリゴンの回転によるヒットエフェクト！ビルボード共存)
        {
            particle.type = Particle::Type::kRotation; // ★ビルボードを適用せず独自の3次元回転を行う！

            std::uniform_real_distribution<float> distScale(0.5f, 2.0f);
            std::uniform_real_distribution<float> distRotate(-M_PI, M_PI);
            std::uniform_real_distribution<float> distSparkVel(-15.0f, 15.0f);

            float sc = distScale(randomEngine_);
            particle.scale = { sc * 0.5f, sc * 0.5f, sc * 0.5f };
            particle.rotate = { distRotate(randomEngine_), distRotate(randomEngine_), distRotate(randomEngine_) };

            particle.position = {
                emitterPos.x + distPos(randomEngine_) * 3.0f,
                emitterPos.y + distPos(randomEngine_) * 3.0f,
                emitterPos.z + distPos(randomEngine_) * 3.0f
            };

            particle.velocity = {
                distSparkVel(randomEngine_),
                distSparkVel(randomEngine_),
                distSparkVel(randomEngine_)
            };

            float colorSelect = distColor(randomEngine_);
            if (colorSelect < 0.2f) {
                particle.color = { 1.0f, 1.0f, 0.9f, 1.0f }; // 白熱
            } else if (colorSelect < 0.45f) {
                particle.color = { 1.0f, 0.9f, 0.2f, 1.0f }; // 黄金
            } else if (colorSelect < 0.7f) {
                particle.color = { 1.0f, 0.5f, 0.0f, 1.0f }; // オレンジ
            } else if (colorSelect < 0.85f) {
                particle.color = { 1.0f, 0.2f, 0.0f, 1.0f }; // 深紅
            } else {
                particle.color = { 0.8f, 0.1f, 0.0f, 1.0f }; // 残り火
            }
            particle.lifeTime = 0.3f + distColor(randomEngine_) * 0.7f;
        }
        break;

    case 50: // kTypeJetExhaust (ツインエンジン噴射)
        {
            std::uniform_real_distribution<float> distSpread(-0.1f, 0.1f);
            particle.position = {
                emitterPos.x + distSpread(randomEngine_),
                emitterPos.y + distSpread(randomEngine_),
                emitterPos.z + distSpread(randomEngine_)
            };

            std::uniform_real_distribution<float> distVelZ(5.0f, 15.0f);
            std::uniform_real_distribution<float> distVelSpread(-0.8f, 0.8f);
            particle.velocity = {
                distVelSpread(randomEngine_),
                distVelSpread(randomEngine_),
                distVelZ(randomEngine_)
            };

            std::uniform_real_distribution<float> distScale(0.3f, 0.6f);
            float sc = distScale(randomEngine_);
            particle.scale = { sc, sc, sc };

            std::uniform_real_distribution<float> distLife(0.2f, 0.5f);
            particle.lifeTime = distLife(randomEngine_);

            float colorSelect = distColor(randomEngine_);
            if (colorSelect < 0.5f) {
                particle.color = { 1.0f, 0.1f, 0.0f, 1.0f };
            } else if (colorSelect < 0.8f) {
                particle.color = { 1.0f, 0.5f, 0.0f, 1.0f };
            } else {
                particle.color = { 1.0f, 1.0f, 0.8f, 1.0f };
            }
        }
        break;

    case 51: // kTypeStardust (流星スピードライン)
        {
            std::uniform_real_distribution<float> distStardustX(-60.0f, 60.0f);
            std::uniform_real_distribution<float> distStardustY(-45.0f, 45.0f);
            std::uniform_real_distribution<float> distStardustZ(-30.0f, 250.0f);

            // カメラZ基準の前方
            particle.position = {
                fighterWorldPos.x + distStardustX(randomEngine_),
                fighterWorldPos.y + distStardustY(randomEngine_),
                cameraZ + distStardustZ(randomEngine_)
            };

            std::uniform_real_distribution<float> distScaleWidth(0.04f, 0.12f);
            std::uniform_real_distribution<float> distScaleLength(1.5f, 4.0f);
            float w = distScaleWidth(randomEngine_);
            float l = distScaleLength(randomEngine_);
            particle.scale = { w, w, l };

            std::uniform_real_distribution<float> distVelStardustZ(-45.0f, -25.0f);
            particle.velocity = { 0.0f, 0.0f, distVelStardustZ(randomEngine_) };

            std::uniform_real_distribution<float> distLife(1.5f, 3.5f);
            particle.lifeTime = distLife(randomEngine_);

            float val = distLife(randomEngine_);
            particle.color = { val / 3.5f, val / 3.5f + 0.05f, 1.0f, 0.8f };
        }
        break;

    case 60: // kTypeFirework (花火の火花)
        {
            particle.position = emitterPos;
            std::uniform_real_distribution<float> distScale(0.3f, 0.7f);
            float sc = distScale(randomEngine_);
            particle.scale = { sc, sc, sc };
            particle.type = Particle::Type::kBillboard;
        }
        break;

    case 61: // kTypeFireworkTrail (花火の打ち上げ軌跡用の火の粉)
        {
            particle.position = emitterPos;
            std::uniform_real_distribution<float> distScale(0.1f, 0.3f);
            float sc = distScale(randomEngine_);
            particle.scale = { sc, sc, sc };
            particle.type = Particle::Type::kBillboard;
        }
        break;

    case 5: // kTypeFlameBurst (火炎バースト)
        {
            std::uniform_real_distribution<float> distSpread(-0.5f, 0.5f);
            particle.position = {
                emitterPos.x + distSpread(randomEngine_) * 2.0f,
                emitterPos.y + distSpread(randomEngine_) * 2.0f,
                emitterPos.z + distSpread(randomEngine_) * 2.0f
            };
            
            float angle = distPos(randomEngine_) * static_cast<float>(M_PI);
            float speedXZ = distColor(randomEngine_) * 4.0f;
            particle.velocity = {
                std::cos(angle) * speedXZ,
                distColor(randomEngine_) * 18.0f + 2.0f,
                std::sin(angle) * speedXZ
            };
            
            std::uniform_real_distribution<float> distScale(0.8f, 2.2f);
            float sc = distScale(randomEngine_);
            particle.scale = { sc, sc, sc };
            particle.lifeTime = 0.5f + distColor(randomEngine_) * 0.4f;
            
            float colT = distColor(randomEngine_);
            particle.color = { 1.0f, 0.3f + colT * 0.6f, 0.0f, 1.0f };
            particle.type = Particle::Type::kBillboard;
        }
        break;

    case 6: // kTypeLightningBolt (放電スパーク)
        {
            particle.type = Particle::Type::kRotation;
            
            std::uniform_real_distribution<float> distRotate(-M_PI, M_PI);
            particle.rotate = { distRotate(randomEngine_), distRotate(randomEngine_), distRotate(randomEngine_) };
            
            particle.position = emitterPos;
            
            float yaw = distRotate(randomEngine_);
            float pitch = distRotate(randomEngine_);
            float sp = 30.0f + distColor(randomEngine_) * 25.0f;
            particle.velocity = {
                std::cos(yaw) * std::cos(pitch) * sp,
                std::sin(pitch) * sp,
                std::sin(yaw) * std::cos(pitch) * sp
            };
            
            float w = 0.15f + distColor(randomEngine_) * 0.2f;
            float l = 2.5f + distColor(randomEngine_) * 3.5f;
            particle.scale = { w, w, l };
            particle.lifeTime = 0.12f + distColor(randomEngine_) * 0.15f;
            
            float colT = distColor(randomEngine_);
            if (colT < 0.3f) {
                particle.color = { 1.0f, 1.0f, 1.0f, 1.0f };
            } else if (colT < 0.8f) {
                particle.color = { 0.1f, 0.8f, 1.0f, 1.0f };
            } else {
                particle.color = { 0.05f, 0.2f, 1.0f, 1.0f };
            }
        }
        break;

    case 7: // kTypeSlashArc (斬撃軌跡)
        {
            particle.type = Particle::Type::kRotation;
            
            std::uniform_real_distribution<float> distRotate(-M_PI, M_PI);
            particle.rotate = { distRotate(randomEngine_) * 0.2f, distRotate(randomEngine_), distRotate(randomEngine_) * 0.2f };
            
            particle.position = {
                emitterPos.x + distPos(randomEngine_) * 0.5f,
                emitterPos.y + distPos(randomEngine_) * 0.5f,
                emitterPos.z + distPos(randomEngine_) * 0.5f
            };
            
            float angle = distRotate(randomEngine_);
            float sp = 15.0f + distColor(randomEngine_) * 15.0f;
            particle.velocity = {
                std::cos(angle) * sp,
                std::sin(angle) * sp * 0.5f,
                std::sin(angle) * sp
            };
            
            float w = 0.2f + distColor(randomEngine_) * 0.3f;
            float l = 4.0f + distColor(randomEngine_) * 8.0f;
            particle.scale = { w, w, l };
            
            particle.lifeTime = 0.2f + distColor(randomEngine_) * 0.25f;
            
            float colT = distColor(randomEngine_);
            if (colT < 0.4f) {
                particle.color = { 0.2f, 1.0f, 0.5f, 1.0f };
            } else if (colT < 0.8f) {
                particle.color = { 0.6f, 1.0f, 0.8f, 1.0f };
            } else {
                particle.color = { 1.0f, 1.0f, 1.0f, 1.0f };
            }
        }
        break;

    case 8: // kTypeGravityIn (重力吸い込み)
        {
            particle.type = Particle::Type::kBillboard;
            
            float yaw = distPos(randomEngine_) * static_cast<float>(M_PI);
            float pitch = distPos(randomEngine_) * static_cast<float>(M_PI);
            float radius = 6.0f + distColor(randomEngine_) * 4.0f;
            
            Vector3 offset = {
                std::cos(yaw) * std::cos(pitch) * radius,
                std::sin(pitch) * radius,
                std::sin(yaw) * std::cos(pitch) * radius
            };
            
            particle.position = { emitterPos.x + offset.x, emitterPos.y + offset.y, emitterPos.z + offset.z };
            
            float sp = 18.0f + distColor(randomEngine_) * 12.0f;
            Vector3 dir = Normalize(offset);
            particle.velocity = {
                -dir.x * sp,
                -dir.y * sp,
                -dir.z * sp
            };
            
            float sc = 0.4f + distColor(randomEngine_) * 0.6f;
            particle.scale = { sc, sc, sc };
            particle.lifeTime = 0.35f;
            
            float colT = distColor(randomEngine_);
            if (colT < 0.5f) {
                particle.color = { 0.4f, 0.05f, 0.8f, 1.0f };
            } else {
                particle.color = { 0.1f, 0.0f, 0.4f, 1.0f };
            }
        }
        break;

    case 9: // kTypeGravityOut (重力爆発)
        {
            particle.type = Particle::Type::kRotation;
            
            std::uniform_real_distribution<float> distRotate(-M_PI, M_PI);
            particle.rotate = { distRotate(randomEngine_), distRotate(randomEngine_), distRotate(randomEngine_) };
            
            particle.position = emitterPos;
            
            float yaw = distRotate(randomEngine_);
            float pitch = distRotate(randomEngine_);
            float sp = 10.0f + distColor(randomEngine_) * 20.0f;
            particle.velocity = {
                std::cos(yaw) * std::cos(pitch) * sp,
                std::sin(pitch) * sp,
                std::sin(yaw) * std::cos(pitch) * sp
            };
            
            std::uniform_real_distribution<float> distScale(0.5f, 2.0f);
            float sc = distScale(randomEngine_);
            particle.scale = { sc * 0.4f, sc * 0.4f, sc * 1.5f };
            particle.lifeTime = 0.4f + distColor(randomEngine_) * 0.4f;
            
            float colT = distColor(randomEngine_);
            if (colT < 0.4f) {
                particle.color = { 0.5f, 0.0f, 0.9f, 1.0f };
            } else if (colT < 0.8f) {
                particle.color = { 0.2f, 0.0f, 0.5f, 1.0f };
            } else {
                particle.color = { 0.9f, 0.1f, 0.6f, 1.0f };
            }
        }
        break;

    case 10: // 氷結・氷華 (Glacial Freeze)
        {
            particle.type = Particle::Type::kRotation;
            std::uniform_real_distribution<float> distRotate(-M_PI, M_PI);
            particle.rotate = { distRotate(randomEngine_), distRotate(randomEngine_), distRotate(randomEngine_) };
            
            std::uniform_real_distribution<float> distOffset(-0.8f, 0.8f);
            particle.position = {
                emitterPos.x + distOffset(randomEngine_),
                emitterPos.y + distOffset(randomEngine_),
                emitterPos.z + distOffset(randomEngine_)
            };
            
            float yaw = distRotate(randomEngine_);
            float pitch = distRotate(randomEngine_);
            float sp = 15.0f + distColor(randomEngine_) * 20.0f;
            particle.velocity = {
                std::cos(yaw) * std::cos(pitch) * sp,
                std::sin(pitch) * sp,
                std::sin(yaw) * std::cos(pitch) * sp
            };
            
            float w = 0.2f + distColor(randomEngine_) * 0.3f;
            float l = 2.0f + distColor(randomEngine_) * 4.5f;
            particle.scale = { w, w, l };
            
            particle.lifeTime = 0.8f + distColor(randomEngine_) * 0.7f;
            
            float colT = distColor(randomEngine_);
            if (colT < 0.4f) {
                particle.color = { 0.85f, 0.95f, 1.0f, 1.0f }; // 白
            } else if (colT < 0.8f) {
                particle.color = { 0.4f, 0.8f, 1.0f, 1.0f };  // 水色
            } else {
                particle.color = { 0.1f, 0.5f, 0.9f, 1.0f };  // 青
            }
        }
        break;

    case 11: // デジタルバグ (Digital Glitch)
        {
            particle.type = Particle::Type::kBillboard;
            
            std::uniform_real_distribution<float> distOffset(-1.8f, 1.8f);
            particle.position = {
                emitterPos.x + distOffset(randomEngine_) * 1.5f,
                emitterPos.y + distOffset(randomEngine_) * 1.5f,
                emitterPos.z + distOffset(randomEngine_) * 1.5f
            };
            
            std::uniform_real_distribution<float> distVelGlitch(-30.0f, 30.0f);
            particle.velocity = {
                distVelGlitch(randomEngine_),
                distVelGlitch(randomEngine_) * 0.25f,
                distVelGlitch(randomEngine_)
            };
            
            float sizeSelect = distColor(randomEngine_);
            if (sizeSelect < 0.4f) {
                // 正方形のデジタルグリッド
                float sc = 0.15f + distColor(randomEngine_) * 0.35f;
                particle.scale = { sc, sc, 1.0f };
            } else {
                // 横長のデジタルバグライン
                float w = 1.5f + distColor(randomEngine_) * 4.0f;
                float h = 0.06f + distColor(randomEngine_) * 0.10f;
                particle.scale = { w, h, 1.0f };
            }
            
            particle.lifeTime = 0.18f + distColor(randomEngine_) * 0.22f;
            
            float colorSelect = distColor(randomEngine_);
            if (colorSelect < 0.6f) {
                particle.color = { 0.0f, 1.0f, 0.3f, 1.0f }; // 蛍光緑
            } else if (colorSelect < 0.85f) {
                particle.color = { 0.0f, 0.9f, 1.0f, 1.0f }; // シアン
            } else {
                particle.color = { 1.0f, 0.0f, 0.8f, 1.0f }; // マゼンタ
            }
        }
        break;

    case 12: // 風・竜巻 (Aero Wind)
        {
            particle.type = Particle::Type::kRotation;
            std::uniform_real_distribution<float> distRotate(-M_PI, M_PI);
            particle.rotate = { distRotate(randomEngine_), distRotate(randomEngine_), distRotate(randomEngine_) };
            
            float angle = distRotate(randomEngine_);
            float radius = 0.5f + distColor(randomEngine_) * 1.5f;
            particle.position = {
                emitterPos.x + std::cos(angle) * radius,
                emitterPos.y + distPos(randomEngine_) * 0.5f,
                emitterPos.z + std::sin(angle) * radius
            };
            
            float spiralSpeed = 6.0f + distColor(randomEngine_) * 8.0f;
            float radialSpeed = 3.0f + distColor(randomEngine_) * 4.0f;
            float upSpeed = 6.0f + distColor(randomEngine_) * 10.0f;
            particle.velocity = {
                -std::sin(angle) * spiralSpeed + std::cos(angle) * radialSpeed,
                upSpeed,
                std::cos(angle) * spiralSpeed + std::sin(angle) * radialSpeed
            };
            
            float w = 0.15f + distColor(randomEngine_) * 0.2f;
            float l = 1.5f + distColor(randomEngine_) * 3.5f;
            particle.scale = { w, w, l };
            
            particle.lifeTime = 0.45f + distColor(randomEngine_) * 0.35f;
            
            float colT = distColor(randomEngine_);
            if (colT < 0.6f) {
                particle.color = { 0.7f, 0.95f, 0.8f, 0.8f }; // エメラルド白
            } else {
                particle.color = { 0.9f, 0.95f, 0.95f, 0.7f }; // 白
            }
        }
        break;

    case 13: // 神聖・天光 (Holy Light)
        {
            particle.type = Particle::Type::kBillboard;
            
            std::uniform_real_distribution<float> distOffset(-2.0f, 2.0f);
            std::uniform_real_distribution<float> distYOffset(-0.5f, 6.0f);
            particle.position = {
                emitterPos.x + distOffset(randomEngine_),
                emitterPos.y + distYOffset(randomEngine_),
                emitterPos.z + distOffset(randomEngine_)
            };
            
            std::uniform_real_distribution<float> distHorizontal(-4.0f, 4.0f);
            std::uniform_real_distribution<float> distFall(-1.5f, -6.5f);
            particle.velocity = {
                distHorizontal(randomEngine_),
                distFall(randomEngine_),
                distHorizontal(randomEngine_)
            };
            
            float sc = 0.2f + distColor(randomEngine_) * 0.45f;
            particle.scale = { sc, sc, sc };
            
            particle.lifeTime = 0.9f + distColor(randomEngine_) * 0.7f;
            
            float colT = distColor(randomEngine_);
            if (colT < 0.6f) {
                particle.color = { 1.0f, 0.95f, 0.6f, 1.0f }; // 黄金
            } else {
                particle.color = { 1.0f, 1.0f, 0.9f, 1.0f };  // 輝く白
            }
        }
        break;

    case 14: // カオスボイド・闇物質 (Chaos Void)
        {
            particle.type = Particle::Type::kRotation;
            std::uniform_real_distribution<float> distRotate(-M_PI, M_PI);
            particle.rotate = { distRotate(randomEngine_), distRotate(randomEngine_), distRotate(randomEngine_) };
            
            std::uniform_real_distribution<float> distOffset(-1.2f, 1.2f);
            particle.position = {
                emitterPos.x + distOffset(randomEngine_) * 0.6f,
                emitterPos.y + distOffset(randomEngine_) * 0.6f,
                emitterPos.z + distOffset(randomEngine_) * 0.6f
            };
            
            float yaw = distRotate(randomEngine_);
            float pitch = distRotate(randomEngine_);
            float sp = 6.0f + distColor(randomEngine_) * 12.0f;
            particle.velocity = {
                std::cos(yaw) * std::cos(pitch) * sp,
                std::sin(pitch) * sp,
                std::sin(yaw) * std::cos(pitch) * sp
            };
            
            float w = 0.5f + distColor(randomEngine_) * 0.7f;
            float l = 0.5f + distColor(randomEngine_) * 1.3f;
            particle.scale = { w, w, l };
            
            particle.lifeTime = 0.5f + distColor(randomEngine_) * 0.5f;
            
            float colT = distColor(randomEngine_);
            if (colT < 0.5f) {
                particle.color = { 0.25f, 0.0f, 0.45f, 1.0f }; // 暗紫
            } else if (colT < 0.85f) {
                particle.color = { 0.85f, 0.0f, 0.45f, 1.0f }; // マゼンタ
            } else {
                particle.color = { 0.03f, 0.0f, 0.07f, 1.0f }; // 漆黒
            }
        }
        break;
    }

    return particle;
}

void ParticleManager::EmitFlame(const Vector3& emitterPos, float speed, int count, const Vector3& color) {
    int emitCount = count;
    for (uint32_t p = 0; p < kNumInstances && emitCount > 0; ++p) {
        if (particles_[p].currentTime >= particles_[p].lifeTime) {
            particles_[p] = MakeNewParticle(5, emitterPos, 0.0f, {0,0,0}, false);
            particles_[p].velocity.x *= (speed / 15.0f);
            particles_[p].velocity.y *= (speed / 15.0f);
            particles_[p].velocity.z *= (speed / 15.0f);
            particles_[p].color.x = color.x;
            particles_[p].color.y = color.y;
            particles_[p].color.z = color.z;
            emitCount--;
        }
    }
}

void ParticleManager::EmitLightning(const Vector3& emitterPos, float speed, int count, const Vector3& color) {
    int emitCount = count;
    for (uint32_t p = 0; p < kNumInstances && emitCount > 0; ++p) {
        if (particles_[p].currentTime >= particles_[p].lifeTime) {
            particles_[p] = MakeNewParticle(6, emitterPos, 0.0f, {0,0,0}, false);
            particles_[p].velocity.x *= (speed / 20.0f);
            particles_[p].velocity.y *= (speed / 20.0f);
            particles_[p].velocity.z *= (speed / 20.0f);
            particles_[p].color.x = color.x;
            particles_[p].color.y = color.y;
            particles_[p].color.z = color.z;
            emitCount--;
        }
    }
}

void ParticleManager::EmitLSystemLightning(const Vector3& startPos, const Vector3& endPos, int depth, float scale, const Vector3& color) {
    if (depth <= 0) return;

    bool isMain = (scale > 0.0f);
    float absScale = std::abs(scale);

    // ── 進行比率 t (0.0: 上空, 1.0: 地面) ──
    float t = 1.0f - (float)depth / 4.0f;

    // ── マルチ周波数ノイズ ＋ 空間クランプ ──
    Vector3 diff = Subtract(endPos, startPos);
    Vector3 midPos = {
        (startPos.x + endPos.x) * 0.5f,
        (startPos.y + endPos.y) * 0.5f,
        (startPos.z + endPos.z) * 0.5f
    };

    std::uniform_real_distribution<float> distOffset(-1.0f, 1.0f);
    
    // 3Dカールノイズのサンプリング
    float noiseFreq = 0.2f;
    Vector3 curlNoise = {
        std::sin(midPos.y * noiseFreq) * std::cos(midPos.z * noiseFreq),
        std::sin(midPos.z * noiseFreq) * std::cos(midPos.x * noiseFreq),
        std::sin(midPos.x * noiseFreq) * std::cos(midPos.y * noiseFreq)
    };

    // 低周波（大きなうねり）＋ 高周波（細かなギザギザ）のマルチ周波数ブレンド
    float lowFreqAmount = 8.5f * (float)depth * absScale;
    float highFreqAmount = 2.5f * absScale;
    Vector3 displacement = {
        (distOffset(randomEngine_) * 0.7f + curlNoise.x * 0.3f) * lowFreqAmount + distOffset(randomEngine_) * highFreqAmount,
        (distOffset(randomEngine_) * 0.7f + curlNoise.y * 0.3f) * lowFreqAmount + distOffset(randomEngine_) * highFreqAmount,
        (distOffset(randomEngine_) * 0.7f + curlNoise.z * 0.3f) * lowFreqAmount + distOffset(randomEngine_) * highFreqAmount
    };

    midPos.x += displacement.x;
    midPos.y += displacement.y;
    midPos.z += displacement.z;

    // 空間クランプ (主幹からの最大離散半径 R 内に押し込める)
    float maxRadius = isMain ? (4.2f * absScale) : (9.0f * absScale);
    Vector3 mainAxis = Subtract(endPos, startPos);
    float axisLen = Length(mainAxis);
    if (axisLen > 0.01f) {
        Vector3 axisNorm = { mainAxis.x / axisLen, mainAxis.y / axisLen, mainAxis.z / axisLen };
        Vector3 toMid = Subtract(midPos, startPos);
        float projection = toMid.x * axisNorm.x + toMid.y * axisNorm.y + toMid.z * axisNorm.z;
        Vector3 onAxis = { startPos.x + axisNorm.x * projection, startPos.y + axisNorm.y * projection, startPos.z + axisNorm.z * projection };
        Vector3 radialVec = Subtract(midPos, onAxis);
        float radDist = Length(radialVec);
        if (radDist > maxRadius) {
            midPos.x = onAxis.x + (radialVec.x / radDist) * maxRadius;
            midPos.y = onAxis.y + (radialVec.y / radDist) * maxRadius;
            midPos.z = onAxis.z + (radialVec.z / radDist) * maxRadius;
        }
    }

    // ── テーパー（太さの制御）の決定 ──
    // 上空が最も太く、地面に向かって徐々に細くなる。ただし地面激突付近は急激に太くして衝撃を表現。
    float width = 0.0f;
    if (isMain) {
        width = (3.2f - t * 2.2f) * absScale; // 3.2 ➔ 1.0
        if (t > 0.9f) {
            width = 4.5f * absScale; // 地面激突付近の極太化
        }
    } else {
        width = 0.35f * absScale; // 追従するプラズマの細い枝
    }

    // ── 純白コアの重ね描き ──
    // 主幹の時は、中心に「純白（超高輝度）のコア線」を重ねて描画することで、光学的なリアリティを何倍にも引き上げる
    int passCount = isMain ? 2 : 1;

    for (int pass = 0; pass < passCount; ++pass) {
        Vector3 finalColor = color;
        float finalWidth = width;
        if (isMain && pass == 1) {
            // 純白のコア
            finalColor = { 1.0f, 1.0f, 1.0f };
            finalWidth = width * 0.35f;
        }

        // 前半セグメント
        for (uint32_t p = 0; p < kNumInstances; ++p) {
            if (particles_[p].currentTime >= particles_[p].lifeTime) {
                particles_[p].currentTime = 0.0f;
                particles_[p].lifeTime = 0.18f + distOffset(randomEngine_) * 0.04f; // 高速明滅
                
                float len = Length(Subtract(midPos, startPos));
                particles_[p].scale = { finalWidth, finalWidth, len * 1.08f };
                particles_[p].position = startPos;
                
                Vector3 dirNorm = { 0.0f, 1.0f, 0.0f };
                if (len > 0.001f) {
                    dirNorm = { (midPos.x - startPos.x) / len, (midPos.y - startPos.y) / len, (midPos.z - startPos.z) / len };
                }
                
                float yaw = std::atan2(dirNorm.x, dirNorm.z);
                float pitch = -std::asin(dirNorm.y);
                particles_[p].rotate = { pitch, yaw, 0.0f };
                particles_[p].type = Particle::Type::kRotation;
                particles_[p].color = { finalColor.x, finalColor.y, finalColor.z, 1.0f };
                particles_[p].velocity = { 0.0f, 0.0f, 0.0f };
                break;
            }
        }

        // 後半セグメント
        for (uint32_t p = 0; p < kNumInstances; ++p) {
            if (particles_[p].currentTime >= particles_[p].lifeTime) {
                particles_[p].currentTime = 0.0f;
                particles_[p].lifeTime = 0.18f + distOffset(randomEngine_) * 0.04f;
                
                float len = Length(Subtract(endPos, midPos));
                particles_[p].scale = { finalWidth, finalWidth, len * 1.08f };
                particles_[p].position = midPos;
                
                Vector3 dirNorm = { 0.0f, 1.0f, 0.0f };
                if (len > 0.001f) {
                    dirNorm = { (endPos.x - midPos.x) / len, (endPos.y - midPos.y) / len, (endPos.z - midPos.z) / len };
                }
                
                float yaw = std::atan2(dirNorm.x, dirNorm.z);
                float pitch = -std::asin(dirNorm.y);
                particles_[p].rotate = { pitch, yaw, 0.0f };
                particles_[p].type = Particle::Type::kRotation;
                particles_[p].color = { finalColor.x, finalColor.y, finalColor.z, 1.0f };
                particles_[p].velocity = { 0.0f, 0.0f, 0.0f };
                break;
            }
        }
    }

    // ── L-system 再帰的ブランチ生成 ──
    EmitLSystemLightning(startPos, midPos, depth - 1, scale, color);
    EmitLSystemLightning(midPos, endPos, depth - 1, scale, color);

    // 枝分かれサブ雷の再帰
    std::uniform_real_distribution<float> distBranch(0.0f, 1.0f);
    if (depth > 1 && distBranch(randomEngine_) < 0.65f) {
        Vector3 angleOffset = {
            distOffset(randomEngine_) * 20.0f,
            distOffset(randomEngine_) * 25.0f,
            distOffset(randomEngine_) * 20.0f
        };
        Vector3 branchDir = {
            diff.x + angleOffset.x,
            diff.y + angleOffset.y,
            diff.z + angleOffset.z
        };
        
        Vector3 branchEnd = {
            midPos.x + branchDir.x * 0.6f,
            midPos.y + branchDir.y * 0.6f,
            midPos.z + branchDir.z * 0.6f
        };
        
        // 分岐したサブ枝は scale を負にして細い枝として再帰
        EmitLSystemLightning(midPos, branchEnd, depth - 1, -absScale * 0.6f, color);
    }
}

void ParticleManager::EmitSlash(const Vector3& emitterPos, float speed, int count, const Vector3& color) {
    int emitCount = count;
    for (uint32_t p = 0; p < kNumInstances && emitCount > 0; ++p) {
        if (particles_[p].currentTime >= particles_[p].lifeTime) {
            particles_[p] = MakeNewParticle(7, emitterPos, 0.0f, {0,0,0}, false);
            particles_[p].velocity.x *= (speed / 15.0f);
            particles_[p].velocity.y *= (speed / 15.0f);
            particles_[p].velocity.z *= (speed / 15.0f);
            particles_[p].color.x = color.x;
            particles_[p].color.y = color.y;
            particles_[p].color.z = color.z;
            emitCount--;
        }
    }
}

void ParticleManager::EmitGravityVortex(const Vector3& emitterPos, float speed, int count, const Vector3& color) {
    int emitCount = count;
    for (uint32_t p = 0; p < kNumInstances && emitCount > 0; ++p) {
        if (particles_[p].currentTime >= particles_[p].lifeTime) {
            particles_[p] = MakeNewParticle(8, emitterPos, 0.0f, {0,0,0}, false);
            particles_[p].velocity.x *= (speed / 15.0f);
            particles_[p].velocity.y *= (speed / 15.0f);
            particles_[p].velocity.z *= (speed / 15.0f);
            particles_[p].color.x = color.x;
            particles_[p].color.y = color.y;
            particles_[p].color.z = color.z;
            emitCount--;
        }
    }
}

void ParticleManager::EmitGravityOut(const Vector3& emitterPos, int count, const Vector3& color) {
    int emitCount = count;
    for (uint32_t p = 0; p < kNumInstances && emitCount > 0; ++p) {
        if (particles_[p].currentTime >= particles_[p].lifeTime) {
            particles_[p] = MakeNewParticle(9, emitterPos, 0.0f, {0,0,0}, false);
            particles_[p].color.x = color.x;
            particles_[p].color.y = color.y;
            particles_[p].color.z = color.z;
            emitCount--;
        }
    }
}

void ParticleManager::EmitCustomSparks(const Vector3& emitterPos, float speed, int count, const Vector3& color, float gravity) {
    int emitCount = count;
    for (uint32_t p = 0; p < kNumInstances && emitCount > 0; ++p) {
        if (particles_[p].currentTime >= particles_[p].lifeTime) {
            particles_[p] = MakeNewParticle(4, emitterPos, 0.0f, {0,0,0}, false);
            particles_[p].velocity.x *= (speed / 15.0f);
            particles_[p].velocity.y *= (speed / 15.0f);
            particles_[p].velocity.z *= (speed / 15.0f);
            particles_[p].gravity = gravity;
            particles_[p].color.x = color.x;
            particles_[p].color.y = color.y;
            particles_[p].color.z = color.z;
            emitCount--;
        }
    }
}

void ParticleManager::EmitGlacial(const Vector3& emitterPos, float speed, int count, const Vector3& color) {
    int emitCount = count;
    for (uint32_t p = 0; p < kNumInstances && emitCount > 0; ++p) {
        if (particles_[p].currentTime >= particles_[p].lifeTime) {
            particles_[p] = MakeNewParticle(10, emitterPos, 0.0f, {0,0,0}, false);
            particles_[p].velocity.x *= (speed / 15.0f);
            particles_[p].velocity.y *= (speed / 15.0f);
            particles_[p].velocity.z *= (speed / 15.0f);
            particles_[p].color.x = color.x;
            particles_[p].color.y = color.y;
            particles_[p].color.z = color.z;
            emitCount--;
        }
    }
}

void ParticleManager::EmitDigitalGlitch(const Vector3& emitterPos, float speed, int count, const Vector3& color) {
    int emitCount = count;
    for (uint32_t p = 0; p < kNumInstances && emitCount > 0; ++p) {
        if (particles_[p].currentTime >= particles_[p].lifeTime) {
            particles_[p] = MakeNewParticle(11, emitterPos, 0.0f, {0,0,0}, false);
            particles_[p].velocity.x *= (speed / 15.0f);
            particles_[p].velocity.y *= (speed / 15.0f);
            particles_[p].velocity.z *= (speed / 15.0f);
            particles_[p].color.x = color.x;
            particles_[p].color.y = color.y;
            particles_[p].color.z = color.z;
            emitCount--;
        }
    }
}

void ParticleManager::EmitAeroWind(const Vector3& emitterPos, float speed, int count, const Vector3& color) {
    int emitCount = count;
    for (uint32_t p = 0; p < kNumInstances && emitCount > 0; ++p) {
        if (particles_[p].currentTime >= particles_[p].lifeTime) {
            particles_[p] = MakeNewParticle(12, emitterPos, 0.0f, {0,0,0}, false);
            particles_[p].velocity.x *= (speed / 15.0f);
            particles_[p].velocity.y *= (speed / 15.0f);
            particles_[p].velocity.z *= (speed / 15.0f);
            particles_[p].color.x = color.x;
            particles_[p].color.y = color.y;
            particles_[p].color.z = color.z;
            emitCount--;
        }
    }
}

void ParticleManager::EmitHolyLight(const Vector3& emitterPos, float speed, int count, const Vector3& color) {
    int emitCount = count;
    for (uint32_t p = 0; p < kNumInstances && emitCount > 0; ++p) {
        if (particles_[p].currentTime >= particles_[p].lifeTime) {
            particles_[p] = MakeNewParticle(13, emitterPos, 0.0f, {0,0,0}, false);
            particles_[p].velocity.x *= (speed / 15.0f);
            particles_[p].velocity.y *= (speed / 15.0f);
            particles_[p].velocity.z *= (speed / 15.0f);
            particles_[p].color.x = color.x;
            particles_[p].color.y = color.y;
            particles_[p].color.z = color.z;
            emitCount--;
        }
    }
}

void ParticleManager::EmitChaosVoid(const Vector3& emitterPos, float speed, int count, const Vector3& color) {
    int emitCount = count;
    for (uint32_t p = 0; p < kNumInstances && emitCount > 0; ++p) {
        if (particles_[p].currentTime >= particles_[p].lifeTime) {
            particles_[p] = MakeNewParticle(14, emitterPos, 0.0f, {0,0,0}, false);
            particles_[p].velocity.x *= (speed / 15.0f);
            particles_[p].velocity.y *= (speed / 15.0f);
            particles_[p].velocity.z *= (speed / 15.0f);
            particles_[p].color.x = color.x;
            particles_[p].color.y = color.y;
            particles_[p].color.z = color.z;
            emitCount--;
        }
    }
}

void ParticleManager::EmitWhiteCross(const Vector3& emitterPos) {
    // 空きスロット（非アクティブ）を探し、無ければ最も寿命が尽きかけているアクティブスロットを強制上書きするヘルパー
    auto find_slot = [this]() -> uint32_t {
        // 1. 非アクティブスロットの検索
        for (uint32_t p = 0; p < kNumInstances; ++p) {
            if (particles_[p].currentTime >= particles_[p].lifeTime) {
                return p;
            }
        }
        // 2. 最も寿命が進んでいる（currentTime / lifeTime が最大）アクティブスロットの検索
        uint32_t bestIdx = 0;
        float maxProgress = -1.0f;
        for (uint32_t p = 0; p < kNumInstances; ++p) {
            float progress = particles_[p].currentTime / particles_[p].lifeTime;
            if (progress > maxProgress) {
                maxProgress = progress;
                bestIdx = p;
            }
        }
        return bestIdx;
    };

    // 1. 中心（コア）に巨大な白い丸型パーティクル粒子を複数重ねて、中心部がまばゆく輝くコアを作る
    for (int i = 0; i < 25; ++i) {
        uint32_t p = find_slot();
        particles_[p].position = emitterPos;
        particles_[p].velocity = { 0.0f, 0.0f, 0.0f };
        std::uniform_real_distribution<float> distScale(12.0f, 24.0f);
        float sc = distScale(randomEngine_);
        particles_[p].scale = { sc, sc, sc };
        particles_[p].rotate = { 0.0f, 0.0f, 0.0f };
        particles_[p].color = { 1.0f, 1.0f, 1.0f, 1.0f };
        particles_[p].lifeTime = 0.5f + (float)i * 0.03f;
        particles_[p].currentTime = 0.0f;
        particles_[p].uvTransform = MakeIdentity4x4();
        particles_[p].gravity = 0.0f;
        particles_[p].effectType = 21; // 21: 丸型パーティクル
        particles_[p].type = Particle::Type::kBillboard;
    }

    // 2. 十字の方向（上、下、左、右）に高密度で綺麗な丸型パーティクル粒子を射出
    Vector3 crossDirs[4] = {
        { 0.0f, 1.0f, 0.0f },  // 上
        { 0.0f, -1.0f, 0.0f }, // 下
        { -1.0f, 0.0f, 0.0f }, // 左
        { 1.0f, 0.0f, 0.0f }   // 右
    };

    // 各方向に90発ずつ（計360発）
    for (int d = 0; d < 4; ++d) {
        for (int c = 0; c < 90; ++c) {
            uint32_t p = find_slot();

            // 速度の分布（中心近くにとどまる遅いものから、遠くへ吹き飛ぶ高速なものまで分散させて綺麗な光線を引く）
            std::uniform_real_distribution<float> distSpeed(15.0f, 160.0f);
            // 横ブレ（ノイズ）は極めて小さくして綺麗な直線にする
            std::uniform_real_distribution<float> distNoise(-0.25f, 0.25f);
            std::uniform_real_distribution<float> distLife(0.8f, 1.3f);
            std::uniform_real_distribution<float> distScale(3.0f, 8.5f); // 粒子の大きさ

            float speed = distSpeed(randomEngine_);
            Vector3 vel = {
                crossDirs[d].x * speed + distNoise(randomEngine_),
                crossDirs[d].y * speed + distNoise(randomEngine_),
                crossDirs[d].z * speed + distNoise(randomEngine_)
            };

            particles_[p].position = emitterPos;
            particles_[p].velocity = vel;
            float sc = distScale(randomEngine_);
            particles_[p].scale = { sc, sc, sc };
            particles_[p].rotate = { 0.0f, 0.0f, 0.0f };
            particles_[p].color = { 1.0f, 1.0f, 1.0f, 1.0f };
            particles_[p].lifeTime = distLife(randomEngine_);
            particles_[p].currentTime = 0.0f;
            particles_[p].uvTransform = MakeIdentity4x4();
            particles_[p].gravity = 0.0f;
            particles_[p].effectType = 21;
            particles_[p].type = Particle::Type::kBillboard;
        }
    }

    // 3. 斜め4方向にも少し少なめ・遅めに射出して、全体の綺麗な星型の炸裂感を整える
    Vector3 diagonalDirs[4] = {
        { 0.707f, 0.707f, 0.0f },   // 右上
        { 0.707f, -0.707f, 0.0f },  // 右下
        { -0.707f, 0.707f, 0.0f },  // 左上
        { -0.707f, -0.707f, 0.0f }  // 左下
    };

    // 各方向に45発ずつ（計180発）
    for (int d = 0; d < 4; ++d) {
        for (int c = 0; c < 45; ++c) {
            uint32_t p = find_slot();

            std::uniform_real_distribution<float> distSpeed(10.0f, 90.0f); // 少し遅め
            std::uniform_real_distribution<float> distNoise(-0.2f, 0.2f);
            std::uniform_real_distribution<float> distLife(0.6f, 1.0f);
            std::uniform_real_distribution<float> distScale(2.0f, 5.0f);

            float speed = distSpeed(randomEngine_);
            Vector3 vel = {
                diagonalDirs[d].x * speed + distNoise(randomEngine_),
                diagonalDirs[d].y * speed + distNoise(randomEngine_),
                diagonalDirs[d].z * speed + distNoise(randomEngine_)
            };

            particles_[p].position = emitterPos;
            particles_[p].velocity = vel;
            float sc = distScale(randomEngine_);
            particles_[p].scale = { sc, sc, sc };
            particles_[p].rotate = { 0.0f, 0.0f, 0.0f };
            particles_[p].color = { 1.0f, 1.0f, 1.0f, 1.0f };
            particles_[p].lifeTime = distLife(randomEngine_);
            particles_[p].currentTime = 0.0f;
            particles_[p].uvTransform = MakeIdentity4x4();
            particles_[p].gravity = 0.0f;
            particles_[p].effectType = 13; // 13: 高速点滅キラキラ明滅
            particles_[p].type = Particle::Type::kBillboard;
        }
    }
}

void ParticleManager::Clear() {
    for (uint32_t i = 0; i < kNumInstances; ++i) {
        particles_[i].currentTime = particles_[i].lifeTime;
        particles_[i].color.w = 0.0f;
    }
}

void ParticleManager::EmitMegaRing(const Vector3& emitterPos, const Vector3& color) {
    int ringCount = 2; // 2枚重ねの多重衝撃波リング
    for (uint32_t r = 0; r < kRingInstanceCount && ringCount > 0; ++r) {
        if (ringParticles_[r].currentTime >= ringParticles_[r].lifeTime) {
            std::uniform_real_distribution<float> distRingScale(15.0f, 30.0f); // 巨大な初期サイズ
            std::uniform_real_distribution<float> distRingRot(-M_PI, M_PI);
            std::uniform_real_distribution<float> distColor(0.0f, 1.0f);

            float ringInitScale = distRingScale(randomEngine_);
            ringParticles_[r].scale = { ringInitScale, ringInitScale, 1.0f };
            ringParticles_[r].rotate = { 0.0f, 0.0f, distRingRot(randomEngine_) };
            ringParticles_[r].position = emitterPos;
            ringParticles_[r].velocity = { 0.0f, 0.0f, 0.0f };
            
            float brightness = 0.8f + distColor(randomEngine_) * 0.2f;
            ringParticles_[r].color = { color.x * brightness, color.y * brightness, color.z * brightness, 1.0f };
            
            ringParticles_[r].lifeTime = 0.7f + distColor(randomEngine_) * 0.3f;
            ringParticles_[r].currentTime = 0.0f;
            ringParticles_[r].uvTransform = MakeIdentity4x4();

            ringCount--;
        }
    }
}

void ParticleManager::EmitMegaCylinder(const Vector3& emitterPos, const Vector3& color) {
    int cylinderCount = 1;
    for (uint32_t c = 0; c < kCylinderInstanceCount && cylinderCount > 0; ++c) {
        if (cylinderParticles_[c].currentTime >= cylinderParticles_[c].lifeTime) {
            std::uniform_real_distribution<float> distCylScale(15.0f, 35.0f); // 巨大な円柱
            std::uniform_real_distribution<float> distCylRot(-M_PI, M_PI);
            std::uniform_real_distribution<float> distCylVel(-10.0f, 10.0f);
            std::uniform_real_distribution<float> distColor(0.0f, 1.0f);

            float cylSc = distCylScale(randomEngine_);
            cylinderParticles_[c].scale = { cylSc, cylSc, cylSc };
            cylinderParticles_[c].rotate = { distCylRot(randomEngine_), distCylRot(randomEngine_), distCylRot(randomEngine_) };
            cylinderParticles_[c].position = emitterPos;
            
            cylinderParticles_[c].velocity = {
                distCylVel(randomEngine_),
                4.0f + std::abs(distCylVel(randomEngine_)),
                distCylVel(randomEngine_)
            };

            float brightness = 0.8f + distColor(randomEngine_) * 0.2f;
            cylinderParticles_[c].color = { color.x * brightness, color.y * brightness, color.z * brightness, 1.0f };

            cylinderParticles_[c].lifeTime = 0.8f + distColor(randomEngine_) * 0.4f;
            cylinderParticles_[c].currentTime = 0.0f;
            cylinderParticles_[c].uvTransform = MakeIdentity4x4();

            cylinderCount--;
        }
    }
}

void ParticleManager::EmitFirework(const Vector3& emitterPos, const Vector3& color) {
    int emitCount = 80;
    for (uint32_t p = 0; p < kNumInstances && emitCount > 0; ++p) {
        if (particles_[p].currentTime >= particles_[p].lifeTime) {
            particles_[p] = MakeNewParticle(60, emitterPos, 0.0f, {0,0,0}, false);
            
            std::uniform_real_distribution<float> distTheta(0.0f, 2.0f * 3.14159265f);
            std::uniform_real_distribution<float> distPhi(0.0f, 3.14159265f);
            std::uniform_real_distribution<float> distSpeed(5.0f, 15.0f);
            
            float theta = distTheta(randomEngine_);
            float phi = distPhi(randomEngine_);
            float speed = distSpeed(randomEngine_);
            
            particles_[p].velocity = {
                speed * std::sin(phi) * std::cos(theta),
                speed * std::cos(phi),
                speed * std::sin(phi) * std::sin(theta)
            };
            
            particles_[p].color = { color.x, color.y, color.z, 1.0f };
            particles_[p].gravity = 0.2f;
            particles_[p].lifeTime = 1.0f + std::uniform_real_distribution<float>(0.0f, 0.8f)(randomEngine_);
            emitCount--;
        }
    }
    
    EmitRing(emitterPos, color);
}

void ParticleManager::EmitFireworkTrail(const Vector3& emitterPos, const Vector3& color) {
    int emitCount = 3;
    for (uint32_t p = 0; p < kNumInstances && emitCount > 0; ++p) {
        if (particles_[p].currentTime >= particles_[p].lifeTime) {
            particles_[p] = MakeNewParticle(61, emitterPos, 0.0f, {0,0,0}, false);
            
            std::uniform_real_distribution<float> distVel(-1.5f, 1.5f);
            particles_[p].velocity = {
                distVel(randomEngine_),
                -2.0f - std::uniform_real_distribution<float>(0.0f, 2.0f)(randomEngine_),
                distVel(randomEngine_)
            };
            
            particles_[p].color = { color.x, color.y, color.z, 1.0f };
            particles_[p].gravity = 0.1f;
            particles_[p].lifeTime = 0.4f + std::uniform_real_distribution<float>(0.0f, 0.3f)(randomEngine_);
            emitCount--;
        }
    }
}