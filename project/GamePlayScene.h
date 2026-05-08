#pragma once
#include "BaseScene.h"
#include "DirectXCommon.h"
#include "Input.h"
#include "Audio.h"
#include "Model.h"
#include "Camera.h"
#include "GraphicsPipeline.h"
#include "Skybox.h"
#include "DataTypes.h"
#include "AdvancedAnimation.h"
#include <vector>
#include <random>
#include <memory>
#include <d3d12.h>
#include <wrl.h>
#include <windows.h>

struct Particle {
    EulerTransform transform;
    Vector3 velocity;
    Vector4 color;
    float lifeTime;
    float currentTime;
    Matrix4x4 uvTransform;
};

struct ParticleForGPU {
    Matrix4x4 WVP;
    Matrix4x4 World;
    Vector4 color;
    Matrix4x4 uvTransform;
};

struct CameraDataCB {
    Vector3 worldPosition;
    float padding;
};

enum ParticleType {
    kTypeExplosion,
    kTypeFountain,
    kTypeSpiral,
    kTypeRain,
    kTypeHit,
    kTypeRing,
    kTypeCylinder
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

    std::unique_ptr<Audio> audio_;
    std::unique_ptr<GraphicsPipeline> graphicsPipeline_;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> srvDescriptorHeap_;

    std::unique_ptr<Camera> camera_;
    std::unique_ptr<Model> particleModel_;
    std::unique_ptr<Model> ringModel_;
    std::unique_ptr<Model> cylinderModel_;

    std::unique_ptr<Model> playerModel_;
    std::unique_ptr<Skybox> skybox_;

    std::mt19937 randomEngine_;
    std::vector<Particle> particles_;

    Microsoft::WRL::ComPtr<ID3D12Resource> instancingResource_;
    ParticleForGPU* instancingData_ = nullptr;

    Microsoft::WRL::ComPtr<ID3D12Resource> spriteInstancingResource_;
    ParticleForGPU* spriteInstancingData_ = nullptr;

    Microsoft::WRL::ComPtr<ID3D12Resource> ringInstancingResource_;
    ParticleForGPU* ringInstancingData_ = nullptr;
    std::vector<Particle> ringParticles_;

    Microsoft::WRL::ComPtr<ID3D12Resource> cylinderInstancingResource_;
    ParticleForGPU* cylinderInstancingData_ = nullptr;
    std::vector<Particle> cylinderParticles_;

    Microsoft::WRL::ComPtr<ID3D12Resource> textureResource_;
    Microsoft::WRL::ComPtr<ID3D12Resource> textTextureResource_;

    Microsoft::WRL::ComPtr<ID3D12Resource> intermediateResource_;
    Microsoft::WRL::ComPtr<ID3D12Resource> textIntermediateResource_;
    Microsoft::WRL::ComPtr<ID3D12Resource> gradationTextureResource_;
    Microsoft::WRL::ComPtr<ID3D12Resource> gradationIntermediateResource_;

    D3D12_GPU_DESCRIPTOR_HANDLE textureSrvHandleGPU_;
    D3D12_GPU_DESCRIPTOR_HANDLE instancingSrvHandleGPU_;
    D3D12_GPU_DESCRIPTOR_HANDLE textSrvHandleGPU_;
    D3D12_GPU_DESCRIPTOR_HANDLE spriteInstancingSrvHandleGPU_;
    D3D12_GPU_DESCRIPTOR_HANDLE ringInstancingSrvHandleGPU_;
    D3D12_GPU_DESCRIPTOR_HANDLE cylinderInstancingSrvHandleGPU_;
    D3D12_GPU_DESCRIPTOR_HANDLE gradationSrvHandleGPU_;

    Microsoft::WRL::ComPtr<ID3D12Resource> transformResource_;
    TransformationMatrix* transformData_ = nullptr;

    Microsoft::WRL::ComPtr<ID3D12Resource> directionalLightResource_;
    DirectionalLight* directionalLightData_ = nullptr;

    Microsoft::WRL::ComPtr<ID3D12Resource> cameraResource_;
    CameraDataCB* cameraDataCB_ = nullptr;

    const uint32_t kNumInstances = 2000;
    const uint32_t kSpriteInstanceCount = 1;
    const uint32_t kRingInstanceCount = 100;
    const uint32_t kCylinderInstanceCount = 50;
    UINT descriptorSizeSRV_ = 0;

    int currentEffect_ = kTypeExplosion;
    Vector3 emitterPos_ = { 0.0f, 0.0f, 0.0f };
    bool useGravity_ = false;
    bool useAdditiveBlend_ = true;

    SoundData bgmData_;
    SoundData jumpSE_;

    bool isCameraMode_ = false;
    float modelEnvCoefficient_ = 1.0f;
    float mouseSensitivity_ = 0.005f;
    Vector2 spritePos_ = { 0.0f, 0.0f };

    // AnimatedCube
    AdvAnim::AnimatedModel cubeModel_;
    AdvAnim::Animation cubeAnimation_;
    AdvAnim::Skeleton cubeSkeleton_;
    float cubeAnimationTime_ = 0.0f;
    std::unique_ptr<Model> cubeRenderModel_;
    Microsoft::WRL::ComPtr<ID3D12Resource> cubeTransformResource_;
    TransformationMatrix* cubeTransformData_ = nullptr;

    // デバッグ描画用
    std::unique_ptr<Model> debugSphereModel_;
    std::unique_ptr<Model> debugBoxModel_;
    static const uint32_t kMaxDebugInstances = 128;
    Microsoft::WRL::ComPtr<ID3D12Resource> debugTransformResources_[kMaxDebugInstances];
    TransformationMatrix* debugTransformData_[kMaxDebugInstances] = { nullptr };
    uint32_t debugTransformIndex_ = 0;

    void DrawSkeleton(const AdvAnim::Skeleton& skeleton, const Matrix4x4& baseWorldMatrix);
};