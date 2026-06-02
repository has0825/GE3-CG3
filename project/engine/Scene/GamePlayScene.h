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
#include <wrl.h>
#include <windows.h>
#include "ParticleManager.h"

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
    kTypeJetExhaust,
    kTypeStardust
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
    std::unique_ptr<Model> enemyModel_;
    std::unique_ptr<Skybox> skybox_;

    std::mt19937 randomEngine_;
    std::vector<Particle> particles_;

    Microsoft::WRL::ComPtr<ID3D12Resource> instancingResource_;
    ParticleForGPU* instancingData_ = nullptr;

    Microsoft::WRL::ComPtr<ID3D12Resource> spriteInstancingResource_;
    ParticleForGPU* spriteInstancingData_ = nullptr;

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
    SceneMode preDebugSceneMode_ = SceneMode::kFighter;
    float modelEnvCoefficient_ = 1.0f;
    float mouseSensitivity_ = 0.005f;
    Vector2 spritePos_ = { 0.0f, 0.0f };

    bool showSimpleSkin_ = false;
    bool showAnimatedCube_ = false;
    bool showParticles_ = true;
    bool showSkybox_ = true;
    int skyboxType_ = 0; // 0: cobblestone, 1: airport
    bool showEnemies_ = true;

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
        kVignette,
        kBoxFilter,
        kOutline,
        kRadialBlur,
        kDissolve,
        kRandom,
    };
    PostProcessType activePostProcess_ = kNone;

    struct VignetteParameter {
        float scale;
        float power;
    };
    Microsoft::WRL::ComPtr<ID3D12Resource> vignetteParamResource_;
    VignetteParameter* vignetteParamData_ = nullptr;

    struct BoxFilterParameter {
        int32_t kernelSize;
    };
    Microsoft::WRL::ComPtr<ID3D12Resource> boxFilterParamResource_;
    BoxFilterParameter* boxFilterParamData_ = nullptr;

    struct RadialBlurParameter {
        Vector2 center;
        float blurWidth;
    };
    Microsoft::WRL::ComPtr<ID3D12Resource> radialBlurParamResource_;
    RadialBlurParameter* radialBlurParamData_ = nullptr;

    struct DissolveParameter {
        float threshold;
        Vector3 edgeColor;
        float edgeRange;
    };
    Microsoft::WRL::ComPtr<ID3D12Resource> dissolveParamResource_;
    DissolveParameter* dissolveParamData_ = nullptr;
    
    struct RandomParameter {
        float time;
        float noiseScale;
        float noiseStrength;
        float isColorNoise;
        float isMultiplyNoise;
        float padding[3];
    };
    Microsoft::WRL::ComPtr<ID3D12Resource> randomParamResource_;
    RandomParameter* randomParamData_ = nullptr;
    float randomEffectTime_ = 0.0f;
    float randomNoiseScale_ = 100.0f;
    float randomNoiseStrength_ = 1.0f;
    float randomSpeed_ = 1.0f;
    bool randomIsColorNoise_ = false;
    int randomNoiseType_ = 0; // 0: TV Static, 1: Multiply
    bool isTransitioning_ = false;
    float transitionThreshold_ = 1.0f;
    uint32_t noise0SrvIndex_ = 0;
    uint32_t noise1SrvIndex_ = 0;
    uint32_t activeNoiseSrvIndex_ = 0; // 現在使用中のノイズ (0=noise0, 1=noise1)
    int selectedNoiseIndex_ = 1;       // ImGui Comboの選択インデックス
    Microsoft::WRL::ComPtr<ID3D12Resource> noise0Resource_;
    Microsoft::WRL::ComPtr<ID3D12Resource> noise0IntermediateResource_;
    Microsoft::WRL::ComPtr<ID3D12Resource> noise1Resource_;
    Microsoft::WRL::ComPtr<ID3D12Resource> noise1IntermediateResource_;

    std::unique_ptr<GpuParticleManager> gpuParticleManager_;
    std::unique_ptr<PostProcess> postProcess_;
    uint32_t depthSrvIndex_ = 0;

    // フォーメーションの種類
    enum class FormationType {
        kVShape,    // V字
        kCircle,    // 円形（丸）
        kLineX,     // 横一列
        kSlant,     // 斜め一列
        kCount
    };

    // 敵小隊（グループ）管理用
    struct EnemyGroup {
        FormationType formation;
        float centerX; // 小隊の中心X
        float centerY; // 小隊の中心Y
        float centerZ; // 小隊の中心Z
    };
    static const int kNumGroups = 3;       // 総グループ数
    static const int kEnemiesPerGroup = 5; // 1グループあたりの敵数

    struct Enemy {
        Vector3 position;
        Vector3 scale;
        Vector3 rotate;
        bool isAlive;
        float radius;

        // フォーメーション管理用メンバ
        int groupIndex;      // 所属グループ (0~2)
        int memberIndex;     // グループ内インデックス (0~4)
        Vector3 localOffset; // グループ中心からの相対位置
    };
    static const int kMaxEnemies = 15; // 5体×3グループ = 計15体に調整
    std::vector<Enemy> enemies_;
    EnemyGroup enemyGroups_[kNumGroups];
    Microsoft::WRL::ComPtr<ID3D12Resource> enemyTransformResources_[kMaxEnemies];
    TransformationMatrix* enemyTransformData_[kMaxEnemies] = { nullptr };

    // フォーメーション関連ヘルパー関数
    void ApplyGroupFormation(int groupIndex);
    void RespawnEnemyGroup(int groupIndex, float playerZ);

    std::unique_ptr<ParticleManager> particleManager_;

    struct Building {
        Vector3 position;
        Vector3 scale;
        Vector3 rotate;
        int floors; // 階数
    };
    static const int kMaxBuildings = 16;
    static const int kMaxBuildingCBs = 120;
    std::vector<Building> buildings_;
    std::unique_ptr<Model> buildingModel_;
    Microsoft::WRL::ComPtr<ID3D12Resource> buildingTransformResources_[kMaxBuildingCBs];
    TransformationMatrix* buildingTransformData_[kMaxBuildingCBs] = { nullptr };

    // 床(Plane)用
    std::unique_ptr<Model> floorModel_;
    static const int kNumFloors = 4;
    Vector3 floorPositions_[kNumFloors];
    Microsoft::WRL::ComPtr<ID3D12Resource> floorTransformResources_[kNumFloors];
    TransformationMatrix* floorTransformData_[kNumFloors] = { nullptr };

    void DrawSkeleton(const AdvAnim::Skeleton& skeleton, const Matrix4x4& baseWorldMatrix);
    
    // プレイヤーの戦闘機用
    float playerRotationRoll_ = 0.0f;
    float playerRotationPitch_ = 0.0f;
    Microsoft::WRL::ComPtr<ID3D12Resource> fighterTransformResource_;
    TransformationMatrix* fighterTransformData_ = nullptr;

    // プレイヤーのワールドZ座標（カメラと分離して独立管理）
    float fighterWorldZ_ = 0.0f;           // プレイヤーのワールドZ座標

    // バレルロール（左Shiftキーで360度ロール）
    bool isBarrelRolling_ = false;          // バレルロール中フラグ
    float barrelRollTimer_ = 0.0f;         // ロール進行タイマー
    static constexpr float kBarrelRollDuration = 0.65f; // ロール完了までの秒数

    // 弾管理
    static const uint32_t kMaxBullets = 60;
    std::vector<Bullet> playerBullets_;
    Microsoft::WRL::ComPtr<ID3D12Resource> bulletTransformResources_[kMaxBullets];
    TransformationMatrix* bulletTransformData_[kMaxBullets] = { nullptr };

    // エイムアシスト用：徐々に補間されるレティクル位置
    Vector3 aimReticlePos_ = { 0.0f, 0.0f, 0.0f };

    // ブーストRadialBlur用
    bool isBoosting_ = false;                  // ブースト動作中フラグ
    float boostBlurWidth_ = 0.0f;              // 現在のブラー強度（0.0〜kBoostBlurMax）
    float boostForwardSpeed_ = 30.0f;          // 現在の前進速度（ブースト中に増加）
    static constexpr float kBoostBlurMax = 0.05f;   // ブースト時の最大ブラー幅
    static constexpr float kBoostBlurFadeIn  = 4.0f; // フェードイン速度
    static constexpr float kBoostBlurFadeOut = 3.0f; // フェードアウト速度
    static constexpr float kBoostSpeedMax  = 480.0f;  // ブースト時最大速度
    static constexpr float kNormalSpeed    = 30.0f;  // 通常速度

public:
    // ゲームフェーズの定義
    enum class GamePhase {
        kPhase1,
        kPhase2,
        kBossFight
    };

private:
    // フェーズ管理用
    GamePhase currentPhase_ = GamePhase::kBossFight; // 初期値ボス戦スタート
    float phaseTimer_ = 0.0f;
    static constexpr float kPhaseDuration = 10.0f; // 1フェーズ10秒

    // 蜘蛛ボス用モデル
    std::unique_ptr<Model> bossBodyModel_;
    std::unique_ptr<Model> bossLegModel_;

    // 蜘蛛ボス用定数バッファ
    Microsoft::WRL::ComPtr<ID3D12Resource> bossBodyTransformResource_;
    TransformationMatrix* bossBodyTransformData_ = nullptr;

    Microsoft::WRL::ComPtr<ID3D12Resource> bossLegTransformResources_[8];
    TransformationMatrix* bossLegTransformData_[8] = { nullptr };

    // 蜘蛛ボス調整パラメータ (ImGuiで調整可能)
    float bossScale_ = 3.0f;           // 全体の基本スケール(3.0f)
    float bossBodyScale_ = 3.0f;       // 胴体ボディ(big+Spider.obj)専用の独立スケール
    float bossLegScale_ = 1.0f;        // 足モデル(big+spider+arm.obj)専用の独立スケール
    float bossZOffset_ = 150.0f;       // プレイヤーとの距離
    float bossYOffset_ = -20.0f;       // 接地高さ
    float bossBodyRotY_ = 180.0f;      // ボス胴体のY回転(プレイヤーに向けるため)
    
    // 左右対称な4対の足の配置パラメータ (ユーザーがImGuiで完全個別調整可能)
    Vector3 bossLegPairPos0_ = { 1.5f, 0.0f, 1.5f };   // 前足 (ペア0)
    float bossLegPairRotY0_ = 135.0f;
    Vector3 bossLegPairPos1_ = { 2.0f, 0.0f, 0.5f };   // 中前足 (ペア1)
    float bossLegPairRotY1_ = 90.0f;
    Vector3 bossLegPairPos2_ = { 2.0f, 0.0f, -0.5f };  // 中後足 (ペア2)
    float bossLegPairRotY2_ = 90.0f;
    Vector3 bossLegPairPos3_ = { 1.5f, 0.0f, -1.5f };  // 後足 (ペア3)
    float bossLegPairRotY3_ = 45.0f;

    // 歩行アニメーション制御用パラメータ
    float bossLegSwingSpeed_ = 5.0f;   // 歩行の速さ
    float bossLegSwingRange_ = 0.3f;   // 前後の振れ幅
    float bossLegLiftRange_ = 0.5f;    // 上下のステップ幅
    float bossTime_ = 0.0f;            // アニメーション用タイマー
};