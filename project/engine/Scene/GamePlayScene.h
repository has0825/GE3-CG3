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
#include "Sprite.h"
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
    
    // デモシーン用メソッド
    void UpdateDemo(float deltaTime);
    void DrawDemo();
    void EmitHitEffect(const Vector3& pos);
    void ApplyPreset(int presetIndex);

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
    std::unique_ptr<Model> debrisModel_; // 破片専用モデル
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
    static const int kNumGroups = 2;       // 総グループ数
    static const int kEnemiesPerGroup = 5; // 1グループあたりの敵数

    struct Enemy {
        Vector3 position = { 0.0f, 0.0f, 0.0f };
        Vector3 scale = { 1.0f, 1.0f, 1.0f };
        Vector3 rotate = { 0.0f, 0.0f, 0.0f };
        bool isAlive = false;
        float radius = 0.0f;
        float hp = 0.0f;
        float maxHP = 0.0f;

        // フォーメーション管理用メンバ
        int groupIndex = 0;      // 所属グループ (0~2)
        int memberIndex = 0;     // グループ内インデックス (0~4)
        Vector3 localOffset = { 0.0f, 0.0f, 0.0f };

        // ── 追加：敵の自律移動・特攻用のメンバ ──
        enum class State {
            kSideWait, // 横側で待機する
            kAppear,   // 真ん中へ現れる (合流)
            kWander,   // 動きながら下がっていく
            kDive      // プレイヤーへ特攻する
        };
        State state = State::kSideWait;
        float stateTimer = 0.0f;     // 状態切り替え用のタイマー
        Vector3 wanderAnchor = { 0.0f, 0.0f, 0.0f };
        Vector3 diveDirection = { 0.0f, 0.0f, 0.0f };
        float wanderPhase = 0.0f;    // ふわふわした動きを作るための位相値
        float speed = 0.0f;          // 移動速度
        float relativeZ = 120.0f;    // 追加：プレイヤーとの相対Z距離をキープするため
        Vector3 appearStartPos = { 0.0f, 0.0f, 0.0f };      // 追加：出現合流開始時の初期位置を記憶するため
    };
    static const int kMaxEnemies = 10; // 5体×2グループ = 計10体に調整
    std::vector<Enemy> enemies_;
    EnemyGroup enemyGroups_[kNumGroups];
    Microsoft::WRL::ComPtr<ID3D12Resource> enemyTransformResources_[kMaxEnemies];
    TransformationMatrix* enemyTransformData_[kMaxEnemies] = { nullptr };
    int activeGroupIndex_ = 0; // 追加：現在出現中のアクティブなグループインデックス

    // フォーメーション関連ヘルパー関数
    void ApplyGroupFormation(int groupIndex);
    void RespawnEnemyGroup(int groupIndex, float playerZ);

    std::unique_ptr<ParticleManager> particleManager_;

    struct Building {
        Vector3 position = { 0.0f, 0.0f, 0.0f };
        Vector3 scale = { 1.0f, 1.0f, 1.0f };
        Vector3 rotate = { 0.0f, 0.0f, 0.0f };
        int floors = 0; // 階数
        bool isDestroyed = false;          // 破壊中フラグ
        Vector3 velocity = { 0.0f, 0.0f, 0.0f };     // 吹き飛び速度
        Vector3 rotationSpeed = { 0.0f, 0.0f, 0.0f }; // 回転速度
        float destroyTimer = 0.0f;         // 破壊経過タイマー
    };
    static const int kMaxBuildings = 160;
    static const int kMaxBuildingCBs = 1000;
    std::vector<Building> buildings_;
    std::unique_ptr<Model> buildingModel_;
    Microsoft::WRL::ComPtr<ID3D12Resource> buildingTransformResources_[kMaxBuildingCBs];
    TransformationMatrix* buildingTransformData_[kMaxBuildingCBs] = { nullptr };

    // 地面の破片演出用
    struct Debris {
        Vector3 position = { 0.0f, 0.0f, 0.0f };
        Vector3 velocity = { 0.0f, 0.0f, 0.0f };
        Vector3 rotate = { 0.0f, 0.0f, 0.0f };
        Vector3 rotationSpeed = { 0.0f, 0.0f, 0.0f };
        Vector3 scale = { 1.0f, 1.0f, 1.0f };
        float lifeTime = 0.0f;
        float currentTime = 0.0f;
        bool isAlive = false;
    };
    static const int kMaxDebris = 100;
    std::vector<Debris> debris_;
    Microsoft::WRL::ComPtr<ID3D12Resource> debrisTransformResources_[kMaxDebris];
    TransformationMatrix* debrisTransformData_[kMaxDebris] = { nullptr };

    // 床(Plane)用
    std::unique_ptr<Model> floorModel_;
    // 1列分のZ方向タイル数（中央・左・右の3列分バッファを確保）
    static const int kNumFloorColumns = 16;              // Z方向のタイル数
    static const int kNumRoadLanes    = 3;               // 左・中央・右の3列
    static const int kNumFloors       = kNumFloorColumns * kNumRoadLanes; // 合計バッファ数
    Vector3 floorPositions_[kNumFloors];
    Microsoft::WRL::ComPtr<ID3D12Resource> floorTransformResources_[kNumFloors];
    TransformationMatrix* floorTransformData_[kNumFloors] = { nullptr };

    // 床・ビル用の追加定数
    static constexpr float kFloorSizeZ = 200.0f;
    static constexpr float kBuildingInterval = 80.0f;
    static constexpr float kFloorHeight = 10.0f;
    static constexpr float kFloorY = -20.0f;
    // roadScale の回転後の意味: X → Z方向（奥行き）, Y → X方向（幅）
    // plane.objは弦 -1～+1 = 2ユニット幅なので、実際幅 = Yスケール × 2
    static constexpr float kRoadDepthScale  = 100.0f; // Z方向の長さ（タイル間隔に合わせてZファイティングを防止）
    static constexpr float kRoadWidthScale  = 40.0f;  // Yスケール（元の道路幅・テクスチャ割り付けなし）
    // plane.obj幅 = 2ユニットなので、実際ワールド幅 = kRoadWidthScale * 2 = 80m
    // 左右列のXオフセット: 中央からタイル1枚分（=80m）ずらして並べる
    static constexpr float kRoadColumnXOffset = 80.0f; // 列間X方向間隔（実ワールド幅に一致させて隙間をなくす）

    // 背景雲UVスクロール速度定数
    static constexpr float kBgUvScrollSpeedX = 0.016f; // X方向スクロール速度（1秒あたりのUVオフセット）
    static constexpr float kBgUvScrollSpeedY = 0.000f; // Y方向スクロール速度（1秒あたりのUVオフセット）

    // 天球(SkyDome)用定数
    // 半径を大きくするほどカメラから見た各ポリゴンの角度が小さくなり折れ目が目立たなくなる
    static constexpr float kSkydomeScale   = 3000000.0f; // 天球半径（FarClip 5000000 以内に十分大きく）
    static constexpr int   kNumSkyTextures = 2;         // 天球テクスチャ数（Green / Red）

    // 自機オフセットの定数
    static constexpr float kFighterYOffset = -3.0f;

    // 特攻すり抜け衝突判定用の定数
    static constexpr float kDiveCollisionFrameMovementScale = 0.75f;
    static constexpr float kDiveCollisionZBuffer = 3.0f;

    // 天球(SkyDome)用
    std::unique_ptr<Model> skydomeModel_;                          // 天球モデル
    Microsoft::WRL::ComPtr<ID3D12Resource> skydomeTransformRes_;   // 変換行列バッファ
    TransformationMatrix* skydomeTransformData_ = nullptr;         // 変換行列マップポインタ
    int activeBackgroundTex_ = 0;  // 0: Green(haikei/Green.png), 1: Red(haikei/Red.png)
    float bgUvScrollX_ = 0.0f;     // X方向UVスクロール量（天球内側での雲の流れ）
    float bgUvScrollY_ = 0.0f;     // Y方向UVスクロール量

    void DrawSkeleton(const AdvAnim::Skeleton& skeleton, const Matrix4x4& baseWorldMatrix);
    void SpawnDebris(const Vector3& basePos);
    
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
    GamePhase currentPhase_ = GamePhase::kPhase1; // 初期値フェーズ1スタート
    float phaseTimer_ = 0.0f;
    static constexpr float kPhaseDuration = 45.0f; // 1フェーズ45秒に延長して複数編成と戦えるようにする

    // フェーズ表示演出用メンバ変数
    std::unique_ptr<Sprite> phaseIntroSprite_;
    std::unique_ptr<Sprite> numberIntroSprite_;
    float phaseIntroTimer_ = -1.0f;
    int phaseIntroNum_ = 1;
    void StartPhaseIntro(int phaseNum);

    // ボス登場演出と画面シェイクの制御用メンバ変数
    float bossAppearanceTimer_ = -1.0f;
    float cameraShake_ = 0.0f;

    // 蜘蛛ボス用モデル
    std::unique_ptr<Model> bossBodyModel_;
    std::unique_ptr<Model> bossLegModel_;

    // 蜘蛛ボス用定数バッファ
    Microsoft::WRL::ComPtr<ID3D12Resource> bossBodyTransformResource_;
    TransformationMatrix* bossBodyTransformData_ = nullptr;

    Microsoft::WRL::ComPtr<ID3D12Resource> bossLegTransformResources_[8];
    TransformationMatrix* bossLegTransformData_[8] = { nullptr };

    // 蜘蛛ボス調整パラメータ (ImGuiで調整可能)
    float bossScale_ = 4.5f;           // 全体の基本スケール
    float bossBodyScale_ = 58.8f;       // 胴体ボディ(big+Spider.obj)専用の独立スケール
    float bossLegScale_ = 46.36f;        // 足モデル(big+spider+arm.obj)専用の独立スケール
    float bossZOffset_ = 169.0f;       // プレイヤーとの距離
    float bossYOffset_ = 18.0f;       // 接地高さ
    float bossBodyRotY_ = 273.0f;      // ボス胴体のY回転(プレイヤーに向けるため)
    Vector3 bossWebFireOffset_ = { 0.0f, 27.0f, 36.0f }; // 蜘蛛の糸（ビーム）発射位置のオフセット (お尻)
    
    // 左右対称な4対の足の配置パラメータ (ユーザーがImGuiで完全個別調整可能)
    Vector3 bossLegPairPos0_ = { 1.50f, -4.15f, 5.40f };   // 前足 (ペア0)
    float bossLegPairRotY0_ = 180.0f;
    Vector3 bossLegPairPos1_ = { 2.00f, -4.35f, 5.80f };   // 中前足 (ペア1)
    float bossLegPairRotY1_ = 180.0f;
    Vector3 bossLegPairPos2_ = { 2.00f, -4.55f, -5.65f };  // 中後足 (ペア2)
    float bossLegPairRotY2_ = 15.0f;
    Vector3 bossLegPairPos3_ = { 1.55f, -4.45f, -5.60f };  // 後足 (ペア3)
    float bossLegPairRotY3_ = 7.0f;

    // 歩行アニメーション制御用パラメータ
    float bossLegSwingSpeed_ = 5.8f;   // 歩行の速さ
    float bossLegSwingRange_ = 0.30f;   // 前後の振れ幅
    float bossLegLiftRange_ = 0.50f;    // 上下のステップ幅
    float bossTime_ = 0.0f;            // アニメーション用タイマー

    // 蜘蛛ボスの足ピボット位置 (ローカル座標での根本位置)
    // 太もも側（接続ギア）を固定軸にするため、Z軸のプラス側に変更します
    float bossLegPivotY_ = 0.04f;
    float bossLegPivotZ_ = 0.35f;

    // 蜘蛛ボスの胴体の揺れパラメータ
    float bossBodyBounceRange_ = 0.3f;  // 胴体の上下の揺れ幅(メートル)
    float bossBodyRollRange_ = 2.0f;    // 胴体の左右のロール角(度)

    // HP・衝突判定関連のメンバ変数
    float playerHP_ = 100.0f;
    float playerMaxHP_ = 100.0f;
    float bossHP_ = 500.0f;
    float bossMaxHP_ = 500.0f;
    float bossCollisionRadius_ = 18.0f;

    // HPバー（スプライト）描画用
    static const uint32_t kHpBarInstanceCount = 5;
    Microsoft::WRL::ComPtr<ID3D12Resource> hpBarInstancingResource_;
    ParticleForGPU* hpBarInstancingData_ = nullptr;
    D3D12_GPU_DESCRIPTOR_HANDLE hpBarInstancingSrvHandleGPU_{};

    // HPVisual（HPバーの滑らかな減少アニメーション用）
    float playerHPVisual_ = 100.0f;
    float bossHPVisual_ = 500.0f;

    // ボス行動AI
    enum class BossActionState {
        kIdle,          // 待機/移動
        kLegAttack,     // 足で殴る
        kLaserAttack,   // 糸レーザー照射
        kWebAttack,     // 蜘蛛の巣弾
    };
    BossActionState bossActionState_ = BossActionState::kIdle;
    float bossActionTimer_ = 0.0f;
    int bossAttackTargetArea_ = 1;      // ボスが狙うエリア (0: 左, 1: 中央, 2: 右)
    bool isBossAttackTargetLocked_ = false; // ターゲットエリアが確定したかどうか
    float bossLaserTimer_ = 0.0f;

    // プレイヤーの移動速度デバフタイマーおよび画面蜘蛛の巣効果タイマー
    float playerSpeedDebuffTimer_ = 0.0f;
    float screenWebTimer_ = 0.0f;

    // 蜘蛛の巣弾の管理
    struct WebBullet {
        Vector3 position;
        Vector3 velocity;
        float radius;
        bool isAlive;
    };
    static const int kMaxWebBullets = 5;
    std::vector<WebBullet> bossWebBullets_;

    // 蜘蛛の巣弾(3D)用定数バッファ
    Microsoft::WRL::ComPtr<ID3D12Resource> webBulletInstancingResource_;
    ParticleForGPU* webBulletInstancingData_ = nullptr;
    D3D12_GPU_DESCRIPTOR_HANDLE webBulletInstancingSrvHandleGPU_{};

    // 画面蜘蛛の巣(2D)用定数バッファ
    Microsoft::WRL::ComPtr<ID3D12Resource> screenWebTransformResource_;
    ParticleForGPU* screenWebTransformData_ = nullptr;
    D3D12_GPU_DESCRIPTOR_HANDLE screenWebSrvHandleGPU_{};

    // 蜘蛛の巣テクスチャのSRVハンドル
    D3D12_GPU_DESCRIPTOR_HANDLE spiderWebSrvHandleGPU_{};

    // プレイヤーパラメータ
    float playerLimitX_ = 20.0f;
    float playerLimitY_ = 12.0f;
    float playerCollisionRadius_ = 2.0f;
    float playerSpeedX_ = 25.0f;
    float playerSpeedY_ = 20.0f;

    // ── ヒットエフェクト・デモ用メンバ変数 ──
    bool isDemoMode_ = false;                 // デモモードフラグ
    bool autoPlay_ = true;                  // オートデモ再生フラグ
    float autoPlayTimer_ = 0.0f;             // オートデモタイマー
    float autoPlayInterval_ = 1.0f;          // オートデモ間隔 (秒)

    // カメラシェイク
    Vector3 cameraBasePos_ = { 0.0f, 6.0f, -25.0f }; // カメラ基準座標
    Vector3 cameraBaseRot_ = { 0.15f, 0.0f, 0.0f };  // カメラ基準回転
    float cameraShakeIntensity_ = 1.5f;      // シェイク基本強度
    float cameraShakeTimeMax_ = 0.35f;       // シェイク時間 (秒)
    float cameraShakeTimer_ = 0.0f;          // 現在のシェイクタイマー
    Vector3 cameraShakeOffset_ = { 0.0f, 0.0f, 0.0f }; // シェイクオフセット値
    float activeShakeIntensity_ = 0.0f;      // 現在適用されているシェイク強度

    // ヒットストップ
    float hitstopTimeMax_ = 0.08f;           // ヒットストップ時間 (秒)
    float hitstopTimer_ = 0.0f;              // 現在のヒットストップタイマー

    // 画面インパクトフラッシュ (Color Flash)
    bool useImpactFlash_ = true;             // フラッシュ有効フラグ
    float flashAlpha_ = 0.0f;                // フラッシュ不透明度 (0.0〜1.0)
    Vector4 flashColor_ = { 1.0f, 1.0f, 1.0f, 1.0f }; // フラッシュ色

    // ラジアルブラー
    bool useRadialBlur_ = true;              // ラジアルブラー有効フラグ
    float blurIntensity_ = 0.0f;             // 現在のブラー強度
    float maxBlurWidth_ = 0.04f;             // 最大ブラー幅

    // エフェクト調整パラメータ (ImGui)
    int selectedEffectPreset_ = 1;           // 選択中プリセット (0: 通常, 1: 火炎, 2: 雷撃, 3: 斬撃, 4: 重力)
    int effectParticleCount_ = 60;           // パーティクル射出数
    float effectParticleSpeed_ = 15.0f;      // パーティクル初期速度
    float effectGravity_ = 0.0f;             // パーティクル重力影響度
    Vector3 effectBaseColor_ = { 1.0f, 0.6f, 0.1f }; // エフェクトベースカラー

    // 標的（ターゲット）とプレイヤーの位置設定
    Vector3 targetPos_ = { 0.0f, 0.0f, 15.0f }; // 標的座標
    Vector3 playerPos_ = { 0.0f, -3.0f, -5.0f }; // プレイヤー座標

    // デモ用ビーム
    struct ShotBeam {
        Vector3 position;
        Vector3 velocity;
        bool isAlive;
    };
    static const int kMaxShotBeams = 10;
    std::vector<ShotBeam> shotBeams_;
    
    // スプライト描画に必要な定数バッファ (フラッシュ用)
    Microsoft::WRL::ComPtr<ID3D12Resource> flashMaterialResource_;
    Material* flashMaterialData_ = nullptr;
    Microsoft::WRL::ComPtr<ID3D12Resource> flashTransformResource_;
    TransformationMatrix* flashTransformData_ = nullptr;
    D3D12_GPU_DESCRIPTOR_HANDLE flashSrvHandleGPU_{};

public:
    enum class AttackMode {
        kShooting, // 射撃（ビーム）
        kMelee     // 近接（打撃・斬撃）
    };
    enum class MeleeState {
        kIdle,
        kDash,     // 標的に向かって超高速突進
        kHit,      // 衝突した瞬間（ヒットストップなど）
        kReturn    // 元の位置に戻る
    };

private:
    AttackMode attackMode_ = AttackMode::kShooting;
    MeleeState meleeState_ = MeleeState::kIdle;
    float meleeTimer_ = 0.0f;
    Vector3 currentFighterPos_ = { 0.0f, -3.0f, -5.0f };
    float digitalGlitchTimer_ = 0.0f;

    // ── Blender連携 (リアルタイム同期 / リプレイ録画 / パラメータ受信) ──
    static int  blenderSyncCounter_;        // game_state.json 書き出し間隔カウンタ
    bool        replayRecording_ = false;   // リプレイ録画中フラグ
    int         replayFrameCounter_ = 0;    // 録画フレーム番号
    int         replaySessionIndex_ = 0;    // リプレイセッションのインデックス
    int         blenderParamReadCounter_ = 0; // blender_params.txt 読み込み間隔カウンタ

    // ── ボス死亡演出用 ──
    bool isBossDefeatedSequence_ = false;
    float bossDefeatTimer_ = 0.0f;
    bool isDefeatBulletActive_ = false;
    Vector3 defeatBulletPos_ = { 0.0f, 0.0f, 0.0f };
    Vector3 defeatBulletVel_ = { 0.0f, 0.0f, 0.0f };
    float defeatBulletSize_ = 0.8f; // トドメの弾サイズ
    Microsoft::WRL::ComPtr<ID3D12Resource> defeatBulletTransformResource_;
    TransformationMatrix* defeatBulletTransformData_ = nullptr;
    bool isBossModelVisible_ = true; // ボス自体の表示フラグ
    bool hasHitBoss_ = false; // ボスにヒットしたか
    float bossDefeatHitTimer_ = 0.0f; // ヒット後の経過時間タイマー
};