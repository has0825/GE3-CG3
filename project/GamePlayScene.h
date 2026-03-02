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
#include <d3d12.h>
#include <wrl.h>

// Game.cpp にあった構造体定義
struct Particle {
    Camera::Transform transform;
    Vector3 velocity;
    Vector4 color;
    float lifeTime;
    float currentTime;
};

struct ParticleForGPU {
    Matrix4x4 WVP;
    Matrix4x4 World;
    Vector4 color;
};

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
    // エンジン機能へのポインタ
    DirectXCommon* dxCommon_ = nullptr;
    Input* input_ = nullptr;

    // シーン内で独自に管理する必要があるもの
    Audio* audio_ = nullptr;
    GraphicsPipeline* graphicsPipeline_ = nullptr;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> srvDescriptorHeap_;

    // Game.cpp から移植した変数
    std::unique_ptr<Camera> camera_;
    std::unique_ptr<Model> particleModel_;
    std::mt19937 randomEngine_;
    std::vector<Particle> particles_;

    // リソース
    Microsoft::WRL::ComPtr<ID3D12Resource> instancingResource_;
    ParticleForGPU* instancingData_ = nullptr;

    Microsoft::WRL::ComPtr<ID3D12Resource> spriteInstancingResource_;
    ParticleForGPU* spriteInstancingData_ = nullptr;

    // テクスチャリソース
    Microsoft::WRL::ComPtr<ID3D12Resource> textureResource_;
    Microsoft::WRL::ComPtr<ID3D12Resource> textTextureResource_;

    // アップロード用一時リソース
    Microsoft::WRL::ComPtr<ID3D12Resource> intermediateResource_;
    Microsoft::WRL::ComPtr<ID3D12Resource> textIntermediateResource_;

    // ハンドル
    D3D12_GPU_DESCRIPTOR_HANDLE textureSrvHandleGPU_;
    D3D12_GPU_DESCRIPTOR_HANDLE instancingSrvHandleGPU_;
    D3D12_GPU_DESCRIPTOR_HANDLE textSrvHandleGPU_;
    D3D12_GPU_DESCRIPTOR_HANDLE spriteInstancingSrvHandleGPU_;

    // 定数
    const uint32_t kNumInstances = 2000;
    const uint32_t kSpriteInstanceCount = 1;
    UINT descriptorSizeSRV_ = 0;

    // ゲーム状態
    int currentEffect_ = kTypeExplosion;
    bool useGravity_ = false;
    bool useAdditiveBlend_ = true;
    Vector3 emitterPos_ = { 0, 0, 0 };
    bool isSpacePressed_ = false;

    // スプライトの座標制御用変数（初期値を100.0f, 150.0fに設定）
    Vector3 spritePos_ = { 100.0f, 150.0f, 0.0f };

    // 音声データ
    SoundData bgmData_;
    SoundData jumpSE_;
};