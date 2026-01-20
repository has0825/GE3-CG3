#include "GamePlayScene.h"
#include "WinApp.h"
#include "SceneManager.h"
#include "D3D12Util.h"
#include "TextureManager.h"
#include <cassert>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// --- 算術ヘルパー関数 ---
static Vector3 Add(const Vector3& v1, const Vector3& v2) {
    return { v1.x + v2.x, v1.y + v2.y, v1.z + v2.z };
}
static Vector3 Scale(const Vector3& v, float s) {
    return { v.x * s, v.y * s, v.z * s };
}

// 乱数生成器
std::random_device seed_gen;
std::mt19937 randomEngine(seed_gen());
std::uniform_real_distribution<float> distPos(-5.0f, 5.0f);
std::uniform_real_distribution<float> distVel(-1.0f, 1.0f);

void GamePlayScene::Initialize() {
    dxCommon_ = DirectXCommon::GetInstance();
    input_ = Input::GetInstance();
    audio_ = Audio::GetInstance();

    // 1. モデル生成
    particleModel_ = Model::CreateParticleModel(dxCommon_->GetDevice());

    // 2. カメラ生成
    camera_ = std::make_unique<Camera>(1280, 720);
    camera_->SetTranslate({ 0.0f, 5.0f, -20.0f });

    // 3. パーティクル初期化
    particles_.clear();

    // 4. テクスチャ読み込み
    TextureManager::GetInstance()->LoadTexture("circle.png");

    // 5. インスタンシング用リソース作成
    const UINT kMaxParticles = 1024;
    size_t sizeInBytes = sizeof(ParticleForGPU) * kMaxParticles;
    instancingResource_ = CreateBufferResource(dxCommon_->GetDevice(), sizeInBytes);

    instancingResource_->Map(0, nullptr, reinterpret_cast<void**>(&instancingData_));

    // 6. インスタンシング用のSRV作成 (★ここを修正)
    // 自前でヒープを作らず、TextureManagerに作ってもらうことでヒープを統一する
    {
        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.Format = DXGI_FORMAT_UNKNOWN;
        srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
        srvDesc.Buffer.FirstElement = 0;
        srvDesc.Buffer.NumElements = kMaxParticles;
        srvDesc.Buffer.StructureByteStride = sizeof(ParticleForGPU);
        srvDesc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;

        // TextureManager経由で作成
        instancingSrvHandleGPU_ = TextureManager::GetInstance()->CreateSRV(instancingResource_.Get(), srvDesc);
    }
}

void GamePlayScene::Finalize() {
    particles_.clear();
}

void GamePlayScene::Update() {
    camera_->Update();

    // 雨のように発生
    particles_.push_back(MakeNewParticle(kTypeRain, { 0, 5, 0 }));

    // 更新処理
    for (auto it = particles_.begin(); it != particles_.end(); ) {
        it->currentTime += 1.0f / 60.0f;
        if (it->currentTime >= it->lifeTime) {
            it = particles_.erase(it);
            continue;
        }
        it->transform.translate = Add(it->transform.translate, Scale(it->velocity, 1.0f / 60.0f));
        ++it;
    }

    // GPU転送
    Matrix4x4 viewProjection = camera_->GetViewProjectionMatrix();

    // ビルボード行列計算
    Matrix4x4 cameraWorld = Inverse(camera_->GetViewMatrix());
    Matrix4x4 billboardMatrix = cameraWorld;
    billboardMatrix.m[3][0] = 0.0f;
    billboardMatrix.m[3][1] = 0.0f;
    billboardMatrix.m[3][2] = 0.0f;

    UINT count = 0;
    const UINT kMaxParticles = 1024;

    for (const auto& particle : particles_) {
        if (count >= kMaxParticles) break;

        Matrix4x4 worldMatrix = MakeAffineMatrix(
            particle.transform.scale,
            { 0,0,0 },
            particle.transform.translate
        );
        worldMatrix = Multiply(billboardMatrix, worldMatrix);

        instancingData_[count].World = worldMatrix;
        instancingData_[count].WVP = Multiply(worldMatrix, viewProjection);
        instancingData_[count].color = particle.color;

        count++;
    }
}

void GamePlayScene::Draw() {
    ID3D12GraphicsCommandList* commandList = dxCommon_->GetCommandList();

    if (!particles_.empty()) {
        D3D12_GPU_DESCRIPTOR_HANDLE textureHandle = TextureManager::GetInstance()->GetSrvHandleGPU("circle.png");

        // 保存しておいたハンドルを使う
        particleModel_->Draw(
            commandList,
            (UINT)particles_.size(),
            textureHandle,
            instancingSrvHandleGPU_ // ★メンバ変数を使用
        );
    }
}

Particle GamePlayScene::MakeNewParticle(int type, const Vector3& emitterPos) {
    Particle particle{};
    particle.transform.scale = { 1.0f, 1.0f, 1.0f };
    particle.transform.rotate = { 0.0f, 0.0f, 0.0f };
    particle.transform.translate = emitterPos;
    particle.currentTime = 0.0f;

    // Rain設定
    particle.transform.translate = {
        emitterPos.x + distPos(randomEngine),
        emitterPos.y,
        emitterPos.z + distPos(randomEngine)
    };
    particle.velocity = { 0.0f, -2.0f, 0.0f };
    particle.lifeTime = 3.0f;
    particle.color = { 1.0f, 1.0f, 1.0f, 1.0f };

    return particle;
}