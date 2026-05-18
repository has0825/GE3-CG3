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
#include "GpuParticle.h"
#include <vector>
#include <random>
#include "PostProcess.h"
#include <memory>
#include <vector>
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
    kTypeCylinder,
    kTypeJetExhaust
};

enum class SceneMode {
    kMouse,
    kCamera,
    kFighter
};

struct Bullet {
    Vector3 position;
    Vector3 velocity;
    float lifeTime;
    float currentTime;
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
    GraphicsPipeline* graphicsPipeline_ = nullptr;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> srvDescriptorHeap_;

    std::unique_ptr<Camera> camera_;
    std::unique_ptr<Model> particleModel_;
    std::unique_ptr<Model> ringModel_;
    std::unique_ptr<Model> cylinderModel_;

    std::unique_ptr<Model> playerModel_;
    std::unique_ptr<Model> fighterModel_;
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
    D3D12_GPU_DESCRIPTOR_HANDLE aimingSrvHandleGPU_;
    D3D12_GPU_DESCRIPTOR_HANDLE aimingInstancingSrvHandleGPU_;

    Microsoft::WRL::ComPtr<ID3D12Resource> aimingInstancingResource_;
    ParticleForGPU* aimingInstancingData_ = nullptr;
    
    struct Material {
        Vector4 color;
        Matrix4x4 uvTransform;
    };
    Microsoft::WRL::ComPtr<ID3D12Resource> reticleMaterialResource_;
    Material* reticleMaterialData_ = nullptr;

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

    SceneMode sceneMode_ = SceneMode::kFighter;
    float modelEnvCoefficient_ = 1.0f;
    float mouseSensitivity_ = 0.005f;
    Vector2 spritePos_ = { 0.0f, 0.0f };

    bool showSimpleSkin_ = false;
    bool showAnimatedCube_ = false;
    bool showParticles_ = false;
    bool showSkybox_ = true;

    // simpleSkin
    AdvAnim::AnimatedModel cubeModel_;
    AdvAnim::Animation cubeAnimation_;
    AdvAnim::Skeleton cubeSkeleton_;
    AdvAnim::SkinCluster cubeSkinCluster_;
    float cubeAnimationTime_ = 0.0f;
    std::unique_ptr<Model> cubeRenderModel_;
    Microsoft::WRL::ComPtr<ID3D12Resource> cubeTransformResource_;
    TransformationMatrix* cubeTransformData_ = nullptr;

    // AnimatedCube
    AdvAnim::AnimatedModel animatedCubeModel_;
    AdvAnim::Animation animatedCubeAnimation_;
    AdvAnim::Skeleton animatedCubeSkeleton_;
    float animatedCubeAnimationTime_ = 0.0f;
    std::unique_ptr<Model> animatedCubeRenderModel_;
    Microsoft::WRL::ComPtr<ID3D12Resource> animatedCubeTransformResource_;
    TransformationMatrix* animatedCubeTransformData_ = nullptr;

    // デバッグ描画用
    std::unique_ptr<Model> debugSphereModel_;
    std::unique_ptr<Model> debugBoxModel_;
    static const uint32_t kMaxDebugInstances = 128;
    Microsoft::WRL::ComPtr<ID3D12Resource> debugTransformResources_[kMaxDebugInstances];
    TransformationMatrix* debugTransformData_[kMaxDebugInstances] = { nullptr };
    uint32_t debugTransformIndex_ = 0;
    
    // ポストプロセスの種類
    enum PostProcessType {
        kNone,
        kGrayscale,
        kSepia,
    };
    PostProcessType activePostProcess_ = kNone;

    std::unique_ptr<GpuParticleManager> gpuParticleManager_;
    std::unique_ptr<PostProcess> postProcess_;

    void DrawSkeleton(const AdvAnim::Skeleton& skeleton, const Matrix4x4& baseWorldMatrix);
    
    // プレイヤーの戦闘機用
    float playerRotationRoll_ = 0.0f;
    float playerRotationPitch_ = 0.0f;
    Microsoft::WRL::ComPtr<ID3D12Resource> fighterTransformResource_;
    TransformationMatrix* fighterTransformData_ = nullptr;

    // 弾管理
    static const uint32_t kMaxBullets = 60;
    std::vector<Bullet> playerBullets_;
    Microsoft::WRL::ComPtr<ID3D12Resource> bulletTransformResources_[kMaxBullets];
    TransformationMatrix* bulletTransformData_[kMaxBullets] = { nullptr };
};