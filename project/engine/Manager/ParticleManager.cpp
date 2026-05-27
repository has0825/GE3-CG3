#include "ParticleManager.h"
#include "D3D12Util.h"
#include "SrvManager.h"
#include "MathUtil.h"
#include <cassert>
#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846f
#endif

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
                particles_[i] = MakeNewParticle(currentEffect, emitterPos, cameraZ, fighterWorldPos, isBoosting);
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

        // 速度加算
        particles_[i].position.x += particles_[i].velocity.x * deltaTime * speedMultiplier;
        particles_[i].position.y += particles_[i].velocity.y * deltaTime * speedMultiplier;
        particles_[i].position.z += particles_[i].velocity.z * deltaTime * speedMultiplier;
        
        particles_[i].currentTime += deltaTime;

        // 透明度の計算 (フェードアウト)
        float alpha = 1.0f - (particles_[i].currentTime / particles_[i].lifeTime);
        particles_[i].color.w = (std::max)(alpha, 0.0f);

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

void ParticleManager::EmitRing(const Vector3& emitterPos) {
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
            
            float ringCol = distColor(randomEngine_);
            if (ringCol < 0.5f) {
                ringParticles_[r].color = { 1.0f, 0.7f, 0.1f, 1.0f }; // ゴールデンオレンジ
            } else {
                ringParticles_[r].color = { 1.0f, 0.3f, 0.05f, 1.0f }; // ディープレッド
            }
            
            ringParticles_[r].lifeTime = 0.5f + distColor(randomEngine_) * 0.5f;
            ringParticles_[r].currentTime = 0.0f;
            ringParticles_[r].uvTransform = MakeIdentity4x4();

            ringCount--;
        }
    }
}

void ParticleManager::EmitCylinder(const Vector3& emitterPos) {
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

            float cylCol = distColor(randomEngine_);
            if (cylCol < 0.5f) {
                cylinderParticles_[c].color = { 1.0f, 0.5f, 0.05f, 1.0f }; // 炎のオレンジ
            } else {
                cylinderParticles_[c].color = { 1.0f, 0.8f, 0.3f, 1.0f }; // 黄金色の閃光
            }

            cylinderParticles_[c].lifeTime = 0.6f + distColor(randomEngine_) * 0.6f;
            cylinderParticles_[c].currentTime = 0.0f;
            cylinderParticles_[c].uvTransform = MakeIdentity4x4();

            cylinderCount--;
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
    }

    return particle;
}