#pragma once
#include "Framework.h"
#include "Model.h"
#include "Camera.h"
#include "MathUtil.h"
#include <random>
#include <vector>
#include <cmath>

// パーティクル関連の定義
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

class Game : public Framework {
public:
    void Initialize() override;
    void Finalize() override;
    void Update() override;
    void Draw() override;

private:
    // 内部ヘルパー関数
    Particle MakeNewParticle(int type, const Vector3& emitterPos);

    // ゲーム固有メンバ変数
    Model* particleModel_ = nullptr;
    Camera* camera_ = nullptr;

    // リソース
    Microsoft::WRL::ComPtr<ID3D12Resource> instancingResource_;
    ParticleForGPU* instancingData_ = nullptr;

    Microsoft::WRL::ComPtr<ID3D12Resource> spriteInstancingResource_;
    ParticleForGPU* spriteInstancingData_ = nullptr;

    // テクスチャリソース
    Microsoft::WRL::ComPtr<ID3D12Resource> textureResource_;
    Microsoft::WRL::ComPtr<ID3D12Resource> textTextureResource_;

    // アップロード用一時リソース（保持しておく必要がある場合用）
    Microsoft::WRL::ComPtr<ID3D12Resource> intermediateResource_;
    Microsoft::WRL::ComPtr<ID3D12Resource> textIntermediateResource_;

    // ハンドル
    D3D12_GPU_DESCRIPTOR_HANDLE textureSrvHandleGPU_;
    D3D12_GPU_DESCRIPTOR_HANDLE instancingSrvHandleGPU_;
    D3D12_GPU_DESCRIPTOR_HANDLE textSrvHandleGPU_;
    D3D12_GPU_DESCRIPTOR_HANDLE spriteInstancingSrvHandleGPU_;

    // ゲームロジック用
    std::mt19937 randomEngine_;
    std::vector<Particle> particles_;

    int currentEffect_ = kTypeExplosion;
    bool useGravity_ = false;
    bool useAdditiveBlend_ = true;
    Vector3 emitterPos_ = { 0.0f, 0.0f, 0.0f };
    bool isSpacePressed_ = false;

    // 定数
    static const UINT kNumInstances = 1000;
    static const UINT kSpriteInstanceCount = 1;

    // 音声データ
    SoundData bgmData_;
    SoundData jumpSE_;
};