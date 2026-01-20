#pragma once
#include "BaseScene.h"
#include "DirectXCommon.h"
#include "Input.h"
#include "Audio.h"
#include "Model.h"
#include "Camera.h"
#include "GraphicsPipeline.h"
#include <vector>
#include <random>
#include <memory>
#include <d3d12.h>
#include <wrl.h>

// パーティクル構造体
struct Particle {
    Camera::Transform transform;
    Vector3 velocity;
    Vector4 color;
    float lifeTime;
    float currentTime;
};

// GPU送信用構造体
struct ParticleForGPU {
    Matrix4x4 WVP;
    Matrix4x4 World;
    Vector4 color;
};

// パーティクルの種類
enum ParticleType {
    kTypeExplosion,
    kTypeFountain,
    kTypeSpiral,
    kTypeRain
};

class GamePlayScene : public BaseScene {
public:
    void Initialize() override;
    void Finalize() override;
    void Update() override;
    void Draw() override;

private:
    Particle MakeNewParticle(int type, const Vector3& emitterPos);

private:
    DirectXCommon* dxCommon_ = nullptr;
    Input* input_ = nullptr;
    Audio* audio_ = nullptr;

    std::unique_ptr<Model> particleModel_;
    std::unique_ptr<Camera> camera_;

    std::mt19937 randomEngine_;
    std::vector<Particle> particles_;

    // リソース管理
    Microsoft::WRL::ComPtr<ID3D12Resource> instancingResource_;
    ParticleForGPU* instancingData_ = nullptr;

    // ★追加: インスタンシング用のSRVハンドル
    D3D12_GPU_DESCRIPTOR_HANDLE instancingSrvHandleGPU_{};
};