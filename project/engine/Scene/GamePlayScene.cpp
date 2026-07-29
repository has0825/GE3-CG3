#include "GamePlayScene.h"
#include "WinApp.h"
#include "SceneManager.h"
#include "D3D12Util.h"
#include "MathUtil.h"
#include "DataTypes.h"
#include "TextureManager.h"
#include "SrvManager.h"
#include "DirectXCommon.h"
#include <algorithm>
#include <fstream>
#include <sstream>
#include <string>
#include <filesystem>
#include <cmath>

#ifdef USE_IMGUI
#include "externals/imgui/imgui.h"
#include "externals/imgui/imgui_impl_dx12.h"
#endif

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif


namespace {
    const std::string kSkyboxTextures[] = {
        "dds/moonless_golf_2k.dds",
        "dds/qwantani_night_puresky_2k.dds"
    };
    const size_t kNumSkyboxTextures = sizeof(kSkyboxTextures) / sizeof(kSkyboxTextures[0]);
    const float kWorldShiftX = 0.0f;
}

// staticメンバの定義
int GamePlayScene::blenderSyncCounter_ = 0;

static bool IsCollidingOBBAndSphere(const Vector3& spherePos, float sphereRadius,
                                     const Vector3& boxPos, const Vector3& boxScale, float boxRotY,
                                     const Vector3& colCenter, const Vector3& colSize) {
    // 1. 球の位置をボックスの位置・Y回転のローカル空間に変換する（スケールは適用しない）
    Vector3 diff = { spherePos.x - boxPos.x, spherePos.y - boxPos.y, spherePos.z - boxPos.z };
    
    // Y軸回転 of 逆回転
    float cosRot = std::cos(-boxRotY);
    float sinRot = std::sin(-boxRotY);
    Vector3 localSpherePos;
    localSpherePos.x = diff.x * cosRot - diff.z * sinRot;
    localSpherePos.y = diff.y;
    localSpherePos.z = diff.x * sinRot + diff.z * cosRot;
    
    // 2. ボックスのスケールを適用したワールド空間でのコライダーAABBの中心と半サイズを計算
    // Blender側では colSize が半幅(ハーフサイズ)として扱われている
    Vector3 halfSize = { colSize.x * boxScale.x, colSize.y * boxScale.y, colSize.z * boxScale.z };
    Vector3 center = { colCenter.x * boxScale.x, colCenter.y * boxScale.y, colCenter.z * boxScale.z };
    
    // 3. ローカル空間でのAABBと球の衝突判定
    Vector3 closestPoint;
    closestPoint.x = (std::max)(center.x - halfSize.x, (std::min)(localSpherePos.x, center.x + halfSize.x));
    closestPoint.y = (std::max)(center.y - halfSize.y, (std::min)(localSpherePos.y, center.y + halfSize.y));
    closestPoint.z = (std::max)(center.z - halfSize.z, (std::min)(localSpherePos.z, center.z + halfSize.z));
    
    float distSq = (localSpherePos.x - closestPoint.x) * (localSpherePos.x - closestPoint.x) +
                   (localSpherePos.y - closestPoint.y) * (localSpherePos.y - closestPoint.y) +
                   (localSpherePos.z - closestPoint.z) * (localSpherePos.z - closestPoint.z);
                   
    return distSq <= (sphereRadius * sphereRadius);
}

static Vector3 Add(const Vector3& v1, const Vector3& v2) {
    return { v1.x + v2.x, v1.y + v2.y, v1.z + v2.z };
}
static Vector3 Subtract(const Vector3& v1, const Vector3& v2) {
    return { v1.x - v2.x, v1.y - v2.y, v1.z - v2.z };
}
static float Length(const Vector3& v) {
    return std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
}
static Vector3 Scale(const Vector3& v, float s) {
    return { v.x * s, v.y * s, v.z * s };
}
static Vector3 TransformNormal(const Vector3& v, const Matrix4x4& m) {
    Vector3 result;
    result.x = v.x * m.m[0][0] + v.y * m.m[1][0] + v.z * m.m[2][0];
    result.y = v.x * m.m[0][1] + v.y * m.m[1][1] + v.z * m.m[2][1];
    result.z = v.x * m.m[0][2] + v.y * m.m[1][2] + v.z * m.m[2][2];
    return result;
}

void GamePlayScene::Initialize() {
    // 既存のリプレイ録画ファイルを全て削除
    try {
        if (std::filesystem::exists("Resources")) {
            for (const auto& entry : std::filesystem::directory_iterator("Resources")) {
                if (entry.is_regular_file()) {
                    std::string filename = entry.path().filename().string();
                    if (filename == "replay_frames.csv" || 
                        (filename.rfind("replay_frames_", 0) == 0 && filename.rfind(".csv") == filename.length() - 4)) {
                        std::filesystem::remove(entry.path());
                    }
                }
            }
        }
    } catch (const std::exception&) {
        // エラー無視
    }
    replaySessionIndex_ = 0;

    // クリアカラーのリセット (デフォルトの青緑系に戻す)
    DirectXCommon::GetInstance()->SetClearColor(0.1f, 0.25f, 0.5f, 1.0f);

    // プレイヤーパラメータをファイルから読み込む
    std::ifstream paramFile("Resources/player_params.txt");
    if (paramFile.is_open()) {
        std::string line;
        while (std::getline(paramFile, line)) {
            if (line.empty() || line[0] == '#') continue;
            size_t pos = line.find('=');
            if (pos == std::string::npos) continue;
            std::string key = line.substr(0, pos);
            std::string val = line.substr(pos + 1);
            if (key == "LIMIT_X") playerLimitX_ = std::stof(val);
            else if (key == "LIMIT_Y") playerLimitY_ = std::stof(val);
            else if (key == "COLLISION_RADIUS") playerCollisionRadius_ = std::stof(val);
            else if (key == "SPEED_X") playerSpeedX_ = std::stof(val);
            else if (key == "SPEED_Y") playerSpeedY_ = std::stof(val);
        }
        paramFile.close();
    } else {
        // ファイルが無ければデフォルト値で作成しておく
        std::ofstream outfile("Resources/player_params.txt");
        if (outfile.is_open()) {
            outfile << "LIMIT_X=36.0\n";
            outfile << "LIMIT_Y=12.0\n";
            outfile << "COLLISION_RADIUS=2.0\n";
            outfile << "SPEED_X=35.0\n";
            outfile << "SPEED_Y=50.0\n";
            outfile.close();
        }
    }

    dxCommon_ = DirectXCommon::GetInstance();
    input_ = Input::GetInstance();
    ID3D12Device* device = dxCommon_->GetDevice();
    ID3D12GraphicsCommandList* commandList = dxCommon_->GetCommandList();

    audio_ = std::make_unique<Audio>();


    audio_->Initialize();

    graphicsPipeline_ = GraphicsPipeline::GetInstance();
    if (graphicsPipeline_) {
        graphicsPipeline_->Initialize(device);
    }

    srvDescriptorHeap_ = SrvManager::GetInstance()->GetDescriptorHeap();
    descriptorSizeSRV_ = SrvManager::GetInstance()->GetDescriptorSize();

    std::random_device seedGenerator;
    randomEngine_.seed(seedGenerator());

    camera_ = std::make_unique<Camera>(WinApp::kClientWidth, WinApp::kClientHeight);
    camera_->SetTranslate({ 0.0f, 0.0f, -15.0f });

    particleModel_ = std::unique_ptr<Model>(Model::CreateParticleModel(device));
    TextureManager::GetInstance()->LoadTexture("isihahen.png");
    ringModel_ = std::unique_ptr<Model>(Model::CreateRingModel(device));
    cylinderModel_ = std::unique_ptr<Model>(Model::CreateCylinderModel(device));

    particleManager_ = std::make_unique<ParticleManager>();
    particleManager_->Initialize(device);

    transformResource_ = CreateBufferResource(device, sizeof(TransformationMatrix));
    transformResource_->Map(0, nullptr, reinterpret_cast<void**>(&transformData_));
    transformData_->WVP = MakeIdentity4x4();
    transformData_->World = MakeIdentity4x4();

    directionalLightResource_ = CreateBufferResource(device, sizeof(DirectionalLight));
    directionalLightResource_->Map(0, nullptr, reinterpret_cast<void**>(&directionalLightData_));
    directionalLightData_->color = { 1.0f, 1.0f, 1.0f, 1.0f };
    directionalLightData_->direction = Normalize({ 0.0f, -1.0f, 1.0f });
    directionalLightData_->intensity = 1.0f;

    cameraResource_ = CreateBufferResource(device, sizeof(CameraDataCB));
    cameraResource_->Map(0, nullptr, reinterpret_cast<void**>(&cameraDataCB_));
    cameraDataCB_->worldPosition = { 0.0f, 0.0f, 0.0f };

    vignetteParamResource_ = CreateBufferResource(device, sizeof(VignetteParameter));
    vignetteParamResource_->Map(0, nullptr, reinterpret_cast<void**>(&vignetteParamData_));
    vignetteParamData_->scale = 16.0f;
    vignetteParamData_->power = 0.8f;

    boxFilterParamResource_ = CreateBufferResource(device, sizeof(BoxFilterParameter));
    boxFilterParamResource_->Map(0, nullptr, reinterpret_cast<void**>(&boxFilterParamData_));
    boxFilterParamData_->kernelSize = 2; // 5x5 default

    radialBlurParamResource_ = CreateBufferResource(device, sizeof(RadialBlurParameter));
    radialBlurParamResource_->Map(0, nullptr, reinterpret_cast<void**>(&radialBlurParamData_));
    radialBlurParamData_->center = { 0.5f, 0.5f };
    radialBlurParamData_->blurWidth = 0.01f;

    spriteInstancingResource_ = CreateBufferResource(device, sizeof(ParticleForGPU) * kSpriteInstanceCount);
    spriteInstancingResource_->Map(0, nullptr, reinterpret_cast<void**>(&spriteInstancingData_));

    DirectX::ScratchImage mipImages = LoadTexture("resources/circle2.png");
    const DirectX::TexMetadata& metadata = mipImages.GetMetadata();
    textureResource_ = CreateTextureResource(device, metadata);
    intermediateResource_ = UploadTextureData(textureResource_.Get(), mipImages, device, commandList);

    DirectX::ScratchImage noiseImages = LoadTexture("resources/noise1.png");
    const DirectX::TexMetadata& noiseMetadata = noiseImages.GetMetadata();
    noise1Resource_ = CreateTextureResource(device, noiseMetadata);
    noise1IntermediateResource_ = UploadTextureData(noise1Resource_.Get(), noiseImages, device, commandList);
    noise1SrvIndex_ = SrvManager::GetInstance()->Allocate();
    SrvManager::GetInstance()->CreateSRVforTexture2D(noise1SrvIndex_, noise1Resource_.Get(), noiseMetadata.format, 1);

    DirectX::ScratchImage noise0Images = LoadTexture("resources/noise0.png");
    const DirectX::TexMetadata& noise0Metadata = noise0Images.GetMetadata();
    noise0Resource_ = CreateTextureResource(device, noise0Metadata);
    noise0IntermediateResource_ = UploadTextureData(noise0Resource_.Get(), noise0Images, device, commandList);
    noise0SrvIndex_ = SrvManager::GetInstance()->Allocate();
    SrvManager::GetInstance()->CreateSRVforTexture2D(noise0SrvIndex_, noise0Resource_.Get(), noise0Metadata.format, 1);

    // 初期は noise1 を使用
    activeNoiseSrvIndex_ = noise1SrvIndex_;
    selectedNoiseIndex_ = 1;

    dissolveParamResource_ = CreateBufferResource(device, sizeof(DissolveParameter));
    dissolveParamResource_->Map(0, nullptr, reinterpret_cast<void**>(&dissolveParamData_));
    dissolveParamData_->threshold = 1.0f;
    dissolveParamData_->edgeColor = { 1.0f, 1.0f, 1.0f };
    dissolveParamData_->edgeRange = 0.03f;

    randomParamResource_ = CreateBufferResource(device, sizeof(RandomParameter));
    randomParamResource_->Map(0, nullptr, reinterpret_cast<void**>(&randomParamData_));
    randomParamData_->time = 0.0f;
    randomParamData_->noiseScale = 100.0f;
    randomParamData_->noiseStrength = 1.0f;
    randomParamData_->isColorNoise = 0.0f;
    randomParamData_->isMultiplyNoise = 0.0f;
    randomEffectTime_ = 0.0f;
    randomNoiseScale_ = 100.0f;
    randomNoiseStrength_ = 1.0f;
    randomSpeed_ = 1.0f;
    randomIsColorNoise_ = false;
    randomNoiseType_ = 0;
    
    isTransitioning_ = true;
    transitionThreshold_ = 1.0f;
    activePostProcess_ = kDissolve;

    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
    srvDesc.Format = metadata.format;
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MipLevels = UINT(metadata.mipLevels);

    uint32_t textureSrvIndex = SrvManager::GetInstance()->Allocate();
    D3D12_CPU_DESCRIPTOR_HANDLE textureSrvHandleCPU = SrvManager::GetInstance()->GetCPUDescriptorHandle(textureSrvIndex);
    textureSrvHandleGPU_ = SrvManager::GetInstance()->GetGPUDescriptorHandle(textureSrvIndex);
    device->CreateShaderResourceView(textureResource_.Get(), &srvDesc, textureSrvHandleCPU);



    DirectX::ScratchImage textMipImages = LoadTexture("resources/text1.png");
    const DirectX::TexMetadata& textMetadata = textMipImages.GetMetadata();
    textTextureResource_ = CreateTextureResource(device, textMetadata);
    textIntermediateResource_ = UploadTextureData(textTextureResource_.Get(), textMipImages, device, commandList);

    D3D12_SHADER_RESOURCE_VIEW_DESC textSrvDesc{};
    textSrvDesc.Format = textMetadata.format;
    textSrvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    textSrvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    textSrvDesc.Texture2D.MipLevels = UINT(textMetadata.mipLevels);

    uint32_t textSrvIndex = SrvManager::GetInstance()->Allocate();
    D3D12_CPU_DESCRIPTOR_HANDLE textSrvHandleCPU = SrvManager::GetInstance()->GetCPUDescriptorHandle(textSrvIndex);
    textSrvHandleGPU_ = SrvManager::GetInstance()->GetGPUDescriptorHandle(textSrvIndex);
    device->CreateShaderResourceView(textTextureResource_.Get(), &textSrvDesc, textSrvHandleCPU);

    D3D12_SHADER_RESOURCE_VIEW_DESC spriteInstancingSrvDesc{};
    spriteInstancingSrvDesc.Format = DXGI_FORMAT_UNKNOWN;
    spriteInstancingSrvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    spriteInstancingSrvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
    spriteInstancingSrvDesc.Buffer.FirstElement = 0;
    spriteInstancingSrvDesc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;
    spriteInstancingSrvDesc.Buffer.NumElements = kSpriteInstanceCount;
    spriteInstancingSrvDesc.Buffer.StructureByteStride = sizeof(ParticleForGPU);

    uint32_t spriteInstancingSrvIndex = SrvManager::GetInstance()->Allocate();
    D3D12_CPU_DESCRIPTOR_HANDLE spriteInstancingSrvHandleCPU = SrvManager::GetInstance()->GetCPUDescriptorHandle(spriteInstancingSrvIndex);
    spriteInstancingSrvHandleGPU_ = SrvManager::GetInstance()->GetGPUDescriptorHandle(spriteInstancingSrvIndex);
    device->CreateShaderResourceView(spriteInstancingResource_.Get(), &spriteInstancingSrvDesc, spriteInstancingSrvHandleCPU);



    // gradationLine.png の読み込みとSRV作成
    DirectX::ScratchImage gradationMipImages = LoadTexture("resources/gradationLine.png");
    const DirectX::TexMetadata& gradationMetadata = gradationMipImages.GetMetadata();
    gradationTextureResource_ = CreateTextureResource(device, gradationMetadata);
    gradationIntermediateResource_ = UploadTextureData(gradationTextureResource_.Get(), gradationMipImages, device, commandList);

    D3D12_SHADER_RESOURCE_VIEW_DESC gradationSrvDesc{};
    gradationSrvDesc.Format = gradationMetadata.format;
    gradationSrvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    gradationSrvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    gradationSrvDesc.Texture2D.MipLevels = UINT(gradationMetadata.mipLevels);

    uint32_t gradationSrvIndex = SrvManager::GetInstance()->Allocate();
    D3D12_CPU_DESCRIPTOR_HANDLE gradationSrvHandleCPU = SrvManager::GetInstance()->GetCPUDescriptorHandle(gradationSrvIndex);
    gradationSrvHandleGPU_ = SrvManager::GetInstance()->GetGPUDescriptorHandle(gradationSrvIndex);
    device->CreateShaderResourceView(gradationTextureResource_.Get(), &gradationSrvDesc, gradationSrvHandleCPU);



    bgmData_ = audio_->LoadAudio("resources/result.mp3");
    jumpSE_ = audio_->LoadAudio("resources/damage.mp3");
    audio_->PlayWave(bgmData_, true, 0.5f);

    TextureManager::GetInstance()->Initialize(device, "Resources/");
    TextureManager::GetInstance()->LoadTexture("test.dds");
    TextureManager::GetInstance()->LoadTexture("circle2.png");
    TextureManager::GetInstance()->LoadTexture("gradationLine.png");
    TextureManager::GetInstance()->LoadTexture("douro.jpg");
    TextureManager::GetInstance()->LoadTexture("human/white.png");
    TextureManager::GetInstance()->LoadTexture("aiming.png");
    TextureManager::GetInstance()->LoadTexture("Player2/Player_basecolor.JPEG");
    TextureManager::GetInstance()->LoadTexture("Player/player.png");
    TextureManager::GetInstance()->LoadTexture("cobblestone_street_night_2k.dds");
    TextureManager::GetInstance()->LoadTexture("rostock_laage_airport_4k.dds");
    TextureManager::GetInstance()->LoadTexture("dds/moonless_golf_2k.dds");
    TextureManager::GetInstance()->LoadTexture("dds/qwantani_night_puresky_2k.dds");
    TextureManager::GetInstance()->LoadTexture("test.dds");
    TextureManager::GetInstance()->LoadTexture("tesuto.dds");

    skybox_ = std::make_unique<Skybox>();
    skybox_->Initialize(device);

    playerModel_ = Model::LoadGLTF("Resources/human/walk.gltf", device);
    fighterModel_ = Model::LoadGLTF("Resources/Player2/Player.obj", device);
    if (fighterModel_) {
        fighterModel_->transform.scale = { 10.0f, 10.0f, 10.0f };
    }
    enemyModel_ = Model::LoadGLTF("Resources/Player/player.obj", device);
    if (enemyModel_) {
        enemyModel_->transform.scale = { 10.0f, 10.0f, 10.0f };
    }
    
    // 戦闘機用トランスフォーム
    fighterTransformResource_ = CreateBufferResource(device, sizeof(TransformationMatrix));
    fighterTransformResource_->Map(0, nullptr, reinterpret_cast<void**>(&fighterTransformData_));
    fighterTransformData_->WVP = MakeIdentity4x4();
    fighterTransformData_->World = MakeIdentity4x4();

    // 弾用トランスフォーム
    playerBullets_.resize(kMaxBullets);
    for (uint32_t i = 0; i < kMaxBullets; ++i) {
        bulletTransformResources_[i] = CreateBufferResource(device, sizeof(TransformationMatrix));
        bulletTransformResources_[i]->Map(0, nullptr, reinterpret_cast<void**>(&bulletTransformData_[i]));
        bulletTransformData_[i]->WVP = MakeIdentity4x4();
        bulletTransformData_[i]->World = MakeIdentity4x4();
    }

    // トドメ巨大弾用トランスフォーム
    defeatBulletTransformResource_ = CreateBufferResource(device, sizeof(TransformationMatrix));
    defeatBulletTransformResource_->Map(0, nullptr, reinterpret_cast<void**>(&defeatBulletTransformData_));
    defeatBulletTransformData_->WVP = MakeIdentity4x4();
    defeatBulletTransformData_->World = MakeIdentity4x4();

    // 死亡演出フラグの初期化
    isBossDefeatedSequence_ = false;
    bossDefeatTimer_ = 0.0f;
    isDefeatBulletActive_ = false;
    isBossModelVisible_ = true;
    hasHitBoss_ = false;
    bossDefeatHitTimer_ = 0.0f;

    // 敵小隊と敵の初期化（オブジェクトプール・無限フォーメーションスポーン式）

    enemies_.clear();
    std::uniform_int_distribution<int> distForm(0, (int)FormationType::kCount - 1);
    std::uniform_real_distribution<float> distX(-10.0f, 10.0f);
    std::uniform_real_distribution<float> distY(-5.0f, 10.0f);

    for (int g = 0; g < kNumGroups; ++g) {
        enemyGroups_[g].formation = (FormationType)distForm(randomEngine_);
        enemyGroups_[g].centerX = distX(randomEngine_);
        enemyGroups_[g].centerY = distY(randomEngine_);
        enemyGroups_[g].centerZ = 250.0f + (float)g * 300.0f; // 250m, 550m, 850m
        enemyGroups_[g].centerRailProgress = enemyGroups_[g].centerZ; // レール空間でも同じ初期値
    }

    for (int i = 0; i < kMaxEnemies; ++i) {
        enemyTransformResources_[i] = CreateBufferResource(device, sizeof(TransformationMatrix));
        enemyTransformResources_[i]->Map(0, nullptr, reinterpret_cast<void**>(&enemyTransformData_[i]));
        enemyTransformData_[i]->WVP = MakeIdentity4x4();
        enemyTransformData_[i]->World = MakeIdentity4x4();

        Enemy enemy;
        enemy.groupIndex = i / kEnemiesPerGroup;
        enemy.memberIndex = i % kEnemiesPerGroup;
        enemy.scale = { 3.8f, 3.8f, 3.8f };
        enemy.rotate = { 0.0f, 0.0f, 0.0f }; // モデルの向きを180度反転して修正
        enemy.isAlive = false;
        enemy.radius = 5.0f;
        enemy.hp = 30.0f;
        enemy.maxHP = 30.0f;
        enemies_.push_back(enemy);
    }

    // 最初はすべての小隊メンバーを生存（Alive）にして初期フォーメーション位置を適用
    // (Z距離がそれぞれ 250m, 550m に分散されます)
    for (int i = 0; i < kMaxEnemies; ++i) {
        enemies_[i].isAlive = true;
    }
    activeGroupIndex_ = 0;
    for (int g = 0; g < kNumGroups; ++g) {
        ApplyGroupFormation(g);
    }

    // ── ビル(Building)の初期化とバッファ生成 ──
    buildingModel_ = std::unique_ptr<Model>(Model::LoadGLTF("Resources/building/building.obj", device));
    TextureManager::GetInstance()->LoadTexture("building/buillding_uv.png");

    // ビル用定数バッファの生成（最大フロア数分の 120 個を確保）
    for (int i = 0; i < kMaxBuildingCBs; ++i) {
        buildingTransformResources_[i] = CreateBufferResource(device, sizeof(TransformationMatrix));
        buildingTransformResources_[i]->Map(0, nullptr, reinterpret_cast<void**>(&buildingTransformData_[i]));
        buildingTransformData_[i]->WVP = MakeIdentity4x4();
        buildingTransformData_[i]->World = MakeIdentity4x4();
    }

    buildings_.clear();
    float kBuildingInterval = 80.0f;
    for (int i = 0; i < kMaxBuildings; ++i) {
        Building building;
        
        // 4列に並べる: 0:内側左, 1:内側右, 2:外側左, 3:外側右
        int columnType = i % 4;
        if (columnType == 0) building.position.x = -45.0f;
        else if (columnType == 1) building.position.x = 45.0f;
        else if (columnType == 2) building.position.x = -85.0f;
        else building.position.x = 85.0f;
        
        // 積み重ねる階数をランダム設定（内側は1〜5階、外側は5〜10階）
        if (columnType == 2 || columnType == 3) {
            building.floors = 5 + (randomEngine_() % 6); // 外側のビルは高め
        } else {
            building.floors = 1 + (randomEngine_() % 5); // 内側のビル
        }
        building.scale = { 10.0f, 10.0f, 10.0f }; // ビル1階分の等倍スケール
        building.rotate = { 0.0f, 0.0f, 0.0f };
        
        int groupIndex = i / 4;
        building.position.z = 50.0f + (float)groupIndex * kBuildingInterval;
        // 積み重ねるため、個々のY座標は Update 内で計算されるため、基準値のみ設定
        building.position.y = kFloorY; 

        // 破壊演出用変数の初期化
        building.isDestroyed = false;
        building.velocity = { 0.0f, 0.0f, 0.0f };
        building.rotationSpeed = { 0.0f, 0.0f, 0.0f };
        building.destroyTimer = 0.0f;

        buildings_.push_back(building);
    }

    // ── 床(Plane)の初期化とバッファ生成 ──
    floorModel_ = std::unique_ptr<Model>(Model::LoadGLTF("Resources/plane.obj", device));
    TextureManager::GetInstance()->LoadTexture("douro.jpg");

    for (int i = 0; i < kNumFloors; ++i) {
        floorTransformResources_[i] = CreateBufferResource(device, sizeof(TransformationMatrix));
        floorTransformResources_[i]->Map(0, nullptr, reinterpret_cast<void**>(&floorTransformData_[i]));
        floorTransformData_[i]->WVP   = MakeIdentity4x4();
        floorTransformData_[i]->World = MakeIdentity4x4();

        floorPositions_[i] = { 0.0f, -10000.0f, 0.0f };
        floorRotations_[i] = { 0.0f, 0.0f, 0.0f };
        floorLanes_[i] = -1;
        floorProgresses_[i] = 999999.0f;
    }

    // 広域地面ベース用定数バッファの生成とマッピング
    groundBaseTransformResource_ = CreateBufferResource(device, sizeof(TransformationMatrix));
    groundBaseTransformResource_->Map(0, nullptr, reinterpret_cast<void**>(&groundBaseTransformData_));
    groundBaseTransformData_->WVP   = MakeIdentity4x4();
    groundBaseTransformData_->World = MakeIdentity4x4();

    // ── 天球(SkyDome)の初期化とバッファ生成 ──
    // 球の内側からテクスチャを見る形式のため、カリングが逆向きのモデルを使用
    TextureManager::GetInstance()->LoadTexture("haikei/Green.png");
    TextureManager::GetInstance()->LoadTexture("haikei/Red.png");
    skydomeModel_ = std::unique_ptr<Model>(Model::LoadGLTF("Resources/skydome/SkyDome.obj", device));
    
    // 天球のライティングを無効化 (Unlit)
    ID3D12Resource* skydomeMaterialRes = skydomeModel_->GetMaterialResource();
    if (skydomeMaterialRes) {
        ::Material* materialData = nullptr;
        skydomeMaterialRes->Map(0, nullptr, reinterpret_cast<void**>(&materialData));
        if (materialData) {
            materialData->enableLighting = 0;
        }
        skydomeMaterialRes->Unmap(0, nullptr);
    }

    skydomeTransformRes_ = CreateBufferResource(device, sizeof(TransformationMatrix));
    skydomeTransformRes_->Map(0, nullptr, reinterpret_cast<void**>(&skydomeTransformData_));
    skydomeTransformData_->WVP   = MakeIdentity4x4();
    skydomeTransformData_->World = MakeIdentity4x4();
    activeBackgroundTex_ = 0;
    bgUvScrollX_ = 0.0f;
    bgUvScrollY_ = 0.0f;

    // レティクル(エイミング)用バッファ
    aimingInstancingResource_ = CreateBufferResource(device, sizeof(ParticleForGPU));
    aimingInstancingResource_->Map(0, nullptr, reinterpret_cast<void**>(&aimingInstancingData_));
    aimingInstancingData_->WVP = MakeIdentity4x4();
    aimingInstancingData_->World = MakeIdentity4x4();
    aimingInstancingData_->uvTransform = MakeIdentity4x4();
    aimingInstancingData_->color = { 1.0f, 1.0f, 1.0f, 1.0f };
    
    D3D12_SHADER_RESOURCE_VIEW_DESC aimingInstancingSrvDesc{};
    aimingInstancingSrvDesc.Format = DXGI_FORMAT_UNKNOWN;
    aimingInstancingSrvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    aimingInstancingSrvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
    aimingInstancingSrvDesc.Buffer.FirstElement = 0;
    aimingInstancingSrvDesc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;
    aimingInstancingSrvDesc.Buffer.NumElements = 1;
    aimingInstancingSrvDesc.Buffer.StructureByteStride = sizeof(ParticleForGPU);

    uint32_t aimingInstancingSrvIndex = SrvManager::GetInstance()->Allocate();
    D3D12_CPU_DESCRIPTOR_HANDLE aimingInstancingSrvHandleCPU = SrvManager::GetInstance()->GetCPUDescriptorHandle(aimingInstancingSrvIndex);
    aimingInstancingSrvHandleGPU_ = SrvManager::GetInstance()->GetGPUDescriptorHandle(aimingInstancingSrvIndex);
    device->CreateShaderResourceView(aimingInstancingResource_.Get(), &aimingInstancingSrvDesc, aimingInstancingSrvHandleCPU);
    
    reticleMaterialResource_ = CreateBufferResource(device, sizeof(Material));
    reticleMaterialResource_->Map(0, nullptr, reinterpret_cast<void**>(&reticleMaterialData_));
    reticleMaterialData_->color = {1.0f, 1.0f, 1.0f, 1.0f};
    reticleMaterialData_->uvTransform = MakeIdentity4x4();

    // HPバー用のバッファ生成とマッピング
    hpBarInstancingResource_ = CreateBufferResource(device, sizeof(ParticleForGPU) * kHpBarInstanceCount);
    hpBarInstancingResource_->Map(0, nullptr, reinterpret_cast<void**>(&hpBarInstancingData_));
    for (uint32_t i = 0; i < kHpBarInstanceCount; ++i) {
        hpBarInstancingData_[i].WVP = MakeIdentity4x4();
        hpBarInstancingData_[i].World = MakeIdentity4x4();
        hpBarInstancingData_[i].color = { 1.0f, 1.0f, 1.0f, 1.0f };
        hpBarInstancingData_[i].uvTransform = MakeIdentity4x4();
    }

    D3D12_SHADER_RESOURCE_VIEW_DESC hpBarInstancingSrvDesc{};
    hpBarInstancingSrvDesc.Format = DXGI_FORMAT_UNKNOWN;
    hpBarInstancingSrvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    hpBarInstancingSrvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
    hpBarInstancingSrvDesc.Buffer.FirstElement = 0;
    hpBarInstancingSrvDesc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;
    hpBarInstancingSrvDesc.Buffer.NumElements = kHpBarInstanceCount;
    hpBarInstancingSrvDesc.Buffer.StructureByteStride = sizeof(ParticleForGPU);

    uint32_t hpBarInstancingSrvIndex = SrvManager::GetInstance()->Allocate();
    D3D12_CPU_DESCRIPTOR_HANDLE hpBarInstancingSrvHandleCPU = SrvManager::GetInstance()->GetCPUDescriptorHandle(hpBarInstancingSrvIndex);
    hpBarInstancingSrvHandleGPU_ = SrvManager::GetInstance()->GetGPUDescriptorHandle(hpBarInstancingSrvIndex);
    device->CreateShaderResourceView(hpBarInstancingResource_.Get(), &hpBarInstancingSrvDesc, hpBarInstancingSrvHandleCPU);



    // HP関連変数の初期化
    playerHP_ = 100.0f;
    playerMaxHP_ = 100.0f;
    bossHP_ = 500.0f;
    bossMaxHP_ = 500.0f;
    bossCollisionRadius_ = 27.0f;
    bossWebFireOffset_ = { 0.0f, 27.0f, 36.0f }; // 糸レーザーをお尻（上・奥）から発射するための初期オフセット

    // 【重要】モデルのスケール・座標設定
    // ※もし画面に見えない場合は、ここ(scale)を 10.0f や 100.0f など大きくしてみてください。
    playerModel_->transform.scale = { 10.0f, 10.0f, 10.0f };
    playerModel_->transform.translate = { 0.0f, 0.0f, 0.0f };
    playerModel_->transform.rotate = { 0.0f, 0.0f, 0.0f };
    playerModel_->SetEnvironmentCoefficient(modelEnvCoefficient_);

    // simpleSkinの初期化
    cubeModel_ = AdvAnim::LoadModelFile("Resources/simpleSkin", "simpleSkin.gltf");
    cubeAnimation_ = AdvAnim::LoadAnimationFile("Resources/simpleSkin", "simpleSkin.gltf");
    cubeSkeleton_ = AdvAnim::CreateSkeleton(cubeModel_.rootNode);
    cubeSkinCluster_ = AdvAnim::CreateSkinCluster(device, cubeSkeleton_, cubeModel_.modelData, TextureManager::GetInstance()->GetSrvHeap(), dxCommon_->GetDescriptorSizeSRV());

    cubeRenderModel_ = std::make_unique<Model>();
    cubeRenderModel_->Initialize(cubeModel_.modelData, device);
    cubeRenderModel_->bones_ = cubeModel_.bones;
    
    // 定数バッファの生成
    cubeTransformResource_ = CreateBufferResource(device, sizeof(TransformationMatrix));
    cubeTransformResource_->Map(0, nullptr, reinterpret_cast<void**>(&cubeTransformData_));
    cubeTransformData_->WVP = MakeIdentity4x4();
    cubeTransformData_->World = MakeIdentity4x4();

    // AnimatedCubeの初期化
    animatedCubeModel_ = AdvAnim::LoadModelFile("Resources/AnimatedCube", "AnimatedCube.gltf");
    animatedCubeAnimation_ = AdvAnim::LoadAnimationFile("Resources/AnimatedCube", "AnimatedCube.gltf");
    animatedCubeSkeleton_ = AdvAnim::CreateSkeleton(animatedCubeModel_.rootNode);

    animatedCubeRenderModel_ = std::make_unique<Model>();
    animatedCubeRenderModel_->Initialize(animatedCubeModel_.modelData, device);
    animatedCubeRenderModel_->bones_ = animatedCubeModel_.bones;
    
    animatedCubeTransformResource_ = CreateBufferResource(device, sizeof(TransformationMatrix));
    animatedCubeTransformResource_->Map(0, nullptr, reinterpret_cast<void**>(&animatedCubeTransformData_));
    animatedCubeTransformData_->WVP = MakeIdentity4x4();
    animatedCubeTransformData_->World = MakeIdentity4x4();

    // テクスチャの読み込み
    TextureManager::GetInstance()->LoadTexture("AnimatedCube/AnimatedCube_BaseColor.png");

    // デバッグ描画用の初期化
    debugSphereModel_ = Model::CreateSphereModel(device);
    debugSphereModel_->SetColor({ 0.0f, 1.0f, 1.0f, 1.0f }); // ネオンシアン (高視認性)
    debugBoxModel_ = Model::CreateBoxModel(device);
    debugBoxModel_->SetColor({ 0.0f, 0.0f, 1.0f, 1.0f }); // 青色
    for (uint32_t i = 0; i < kMaxDebugInstances; ++i) {
        debugTransformResources_[i] = CreateBufferResource(device, sizeof(TransformationMatrix));
        debugTransformResources_[i]->Map(0, nullptr, reinterpret_cast<void**>(&debugTransformData_[i]));
        debugTransformData_[i]->WVP = MakeIdentity4x4();
        debugTransformData_[i]->World = MakeIdentity4x4();
    }

    // removed isCameraMode_ = false;

    gpuParticleManager_ = std::make_unique<GpuParticleManager>();
    gpuParticleManager_->Initialize(device);

    postProcess_ = std::make_unique<PostProcess>();
    postProcess_->Initialize(dxCommon_, WinApp::kClientWidth, WinApp::kClientHeight);

    // 深度バッファ用のSRVを生成
    depthSrvIndex_ = SrvManager::GetInstance()->Allocate();
    SrvManager::GetInstance()->CreateSRVforTexture2D(
        depthSrvIndex_, 
        dxCommon_->GetDepthStencilResource(), 
        DXGI_FORMAT_R24_UNORM_X8_TYPELESS, // 深度値のサンプリング用フォーマット
        1
    );

    gpuParticleManager_->SetTranslate({ -10.0f, 0.0f, 0.0f });

    // プレイヤーのワールドZ座標を初期化（カメラ初期Z=-15より65ユニット前に配置）
    fighterWorldZ_ = camera_->GetTransform().translate.z + 65.0f;

    // ── 蜘蛛ボス（Big Spider）の初期化 ──
    // テクスチャのロード
    TextureManager::GetInstance()->LoadTexture("big Spider/big+Spider_basecolor.jpg");
    TextureManager::GetInstance()->LoadTexture("big Spider/big+spider+arm_basecolor.jpg");

    // ボスモデルのロード (元の高画質オリジナルモデルをロード)
    bossBodyModel_ = Model::LoadGLTF("Resources/big Spider/big+Spider.obj", device);
    bossLegModel_ = Model::LoadGLTF("Resources/big Spider/big+spider+arm.obj", device);

    // 胴体用の定数バッファ生成とマップ
    bossBodyTransformResource_ = CreateBufferResource(device, sizeof(TransformationMatrix));
    bossBodyTransformResource_->Map(0, nullptr, reinterpret_cast<void**>(&bossBodyTransformData_));
    bossBodyTransformData_->WVP = MakeIdentity4x4();
    bossBodyTransformData_->World = MakeIdentity4x4();

    // 8本の足用の定数バッファ生成とマップ
    for (int i = 0; i < 8; ++i) {
        bossLegTransformResources_[i] = CreateBufferResource(device, sizeof(TransformationMatrix));
        bossLegTransformResources_[i]->Map(0, nullptr, reinterpret_cast<void**>(&bossLegTransformData_[i]));
        bossLegTransformData_[i]->WVP = MakeIdentity4x4();
        bossLegTransformData_[i]->World = MakeIdentity4x4();
    }

    // 蜘蛛の巣テクスチャのロードとSRV取得
    TextureManager::GetInstance()->LoadTexture("spider web.png");
    spiderWebSrvHandleGPU_ = TextureManager::GetInstance()->GetSrvHandleGPU("spider web.png");

    // 画面蜘蛛の巣用バッファ生成とマッピング
    screenWebTransformResource_ = CreateBufferResource(device, sizeof(ParticleForGPU));
    screenWebTransformResource_->Map(0, nullptr, reinterpret_cast<void**>(&screenWebTransformData_));
    screenWebTransformData_->WVP = MakeIdentity4x4();
    screenWebTransformData_->World = MakeIdentity4x4();
    screenWebTransformData_->color = { 1.0f, 1.0f, 1.0f, 0.0f }; // 初期は完全透明
    screenWebTransformData_->uvTransform = MakeIdentity4x4();

    // 画面蜘蛛の巣用のSRV作成
    D3D12_SHADER_RESOURCE_VIEW_DESC screenWebSrvDesc{};
    screenWebSrvDesc.Format = DXGI_FORMAT_UNKNOWN;
    screenWebSrvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    screenWebSrvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
    screenWebSrvDesc.Buffer.FirstElement = 0;
    screenWebSrvDesc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;
    screenWebSrvDesc.Buffer.NumElements = 1;
    screenWebSrvDesc.Buffer.StructureByteStride = sizeof(ParticleForGPU);

    uint32_t screenWebSrvIndex = SrvManager::GetInstance()->Allocate();
    D3D12_CPU_DESCRIPTOR_HANDLE screenWebSrvHandleCPU = SrvManager::GetInstance()->GetCPUDescriptorHandle(screenWebSrvIndex);
    screenWebSrvHandleGPU_ = SrvManager::GetInstance()->GetGPUDescriptorHandle(screenWebSrvIndex);
    device->CreateShaderResourceView(screenWebTransformResource_.Get(), &screenWebSrvDesc, screenWebSrvHandleCPU);

    // 蜘蛛の巣弾（3D）用バッファ生成とマッピング
    webBulletInstancingResource_ = CreateBufferResource(device, sizeof(ParticleForGPU) * kMaxWebBullets);
    webBulletInstancingResource_->Map(0, nullptr, reinterpret_cast<void**>(&webBulletInstancingData_));
    for (int i = 0; i < kMaxWebBullets; ++i) {
        webBulletInstancingData_[i].WVP = MakeIdentity4x4();
        webBulletInstancingData_[i].World = MakeIdentity4x4();
        webBulletInstancingData_[i].color = { 1.0f, 1.0f, 1.0f, 1.0f };
        webBulletInstancingData_[i].uvTransform = MakeIdentity4x4();
    }

    // 蜘蛛の巣弾用のSRV作成
    D3D12_SHADER_RESOURCE_VIEW_DESC webBulletSrvDesc{};
    webBulletSrvDesc.Format = DXGI_FORMAT_UNKNOWN;
    webBulletSrvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    webBulletSrvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
    webBulletSrvDesc.Buffer.FirstElement = 0;
    webBulletSrvDesc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;
    webBulletSrvDesc.Buffer.NumElements = kMaxWebBullets;
    webBulletSrvDesc.Buffer.StructureByteStride = sizeof(ParticleForGPU);

    uint32_t webBulletSrvIndex = SrvManager::GetInstance()->Allocate();
    D3D12_CPU_DESCRIPTOR_HANDLE webBulletSrvHandleCPU = SrvManager::GetInstance()->GetCPUDescriptorHandle(webBulletSrvIndex);
    webBulletInstancingSrvHandleGPU_ = SrvManager::GetInstance()->GetGPUDescriptorHandle(webBulletSrvIndex);
    device->CreateShaderResourceView(webBulletInstancingResource_.Get(), &webBulletSrvDesc, webBulletSrvHandleCPU);

    // 蜘蛛の巣弾オブジェクトプール初期化
    bossWebBullets_.resize(kMaxWebBullets);
    for (int i = 0; i < kMaxWebBullets; ++i) {
        bossWebBullets_[i].isAlive = false;
        bossWebBullets_[i].radius = 8.0f; // 巨大な蜘蛛の巣
    }

    // HP表示補間用初期化
    playerHPVisual_ = playerHP_;
    bossHPVisual_ = bossHP_;

    // 初期フェーズはフェーズ1スタート
    currentPhase_ = GamePhase::kPhase1;
    phaseTimer_ = 0.0f;
    bossTime_ = 0.0f;
    StartPhaseIntro(1);

    /*
    // 現在の配置をBlender用にファイル出力する
    std::ofstream layoutFile("Resources/scene_layout.txt");
    if (layoutFile.is_open()) {
        float kFloorHeight = 10.0f;
        for (int i = 0; i < kMaxBuildings; ++i) {
            for (int f = 0; f < buildings_[i].floors; ++f) {
                Vector3 floorPos = buildings_[i].position;
                floorPos.y = -20.0f + (float)f * kFloorHeight + kFloorHeight * 0.5f;
                layoutFile << "BUILDING," 
                           << floorPos.x << "," << floorPos.y << "," << floorPos.z << ","
                           << buildings_[i].scale.x << "," << buildings_[i].scale.y << "," << buildings_[i].scale.z << ","
                           << buildings_[i].rotate.x << "," << buildings_[i].rotate.y << "," << buildings_[i].rotate.z << "\n";
            }
        }
        for (int i = 0; i < kNumFloors; ++i) {
            layoutFile << "FLOOR," 
                       << floorPositions_[i].x << "," << floorPositions_[i].y << "," << floorPositions_[i].z << ","
                       << 300.0f << "," << 1.0f << "," << kFloorSizeZ << "\n";
        }
        // 自機初期位置の出力
        layoutFile << "PLAYER," 
                   << 0.0f << "," << kFighterYOffset << "," << fighterWorldZ_ << ","
                   << 10.0f << "," << 10.0f << "," << 10.0f << ","
                   << 0.0f << "," << 0.0f << "," << 0.0f << "\n";
        // 敵の初期位置の出力
        for (int i = 0; i < kMaxEnemies; ++i) {
            if (enemies_[i].isAlive) {
                layoutFile << "ENEMY," 
                           << enemies_[i].position.x << "," << enemies_[i].position.y << "," << enemies_[i].position.z << ","
                           << enemies_[i].scale.x << "," << enemies_[i].scale.y << "," << enemies_[i].scale.z << ","
                           << enemies_[i].rotate.x << "," << enemies_[i].rotate.y << "," << enemies_[i].rotate.z << "\n";
            }
        }
        layoutFile.close();
    }
    */

    // ── scene_layout.txt のインポート処理 ──
    int floorCount = 0;
    std::ifstream layoutInFile("Resources/scene_layout.txt");
    if (layoutInFile.is_open()) {
        // すべてのビルを一旦クリア (floors = 0 にして描画を無効化)
        for (int i = 0; i < kMaxBuildings; ++i) {
            buildings_[i].floors = 0;
            buildings_[i].isDestroyed = false;
        }

        // すべての敵を一旦非アクティブ化
        for (int i = 0; i < kMaxEnemies; ++i) {
            enemies_[i].isAlive = false;
        }

        // すべての床を一旦画面外へ（描画非表示用）および回転クリア
        for (int i = 0; i < kNumFloors; ++i) {
            floorPositions_[i] = { 0.0f, -10000.0f, 0.0f };
            floorRotations_[i] = { 0.0f, 0.0f, 0.0f };
            floorLanes_[i] = -1;
            floorProgresses_[i] = 999999.0f;
        }

        waypoints_.clear();

        std::string line;
        int buildingCount = 0;
        int enemyCount = 0;

        while (std::getline(layoutInFile, line)) {
            if (line.empty()) continue;
            std::stringstream ss(line);
            std::string type;
            std::getline(ss, type, ',');

            if (type == "BUILDING") {
                std::string s_gx, s_gy, s_gz, s_sx, s_sy, s_sz, s_rx, s_ry, s_rz;
                std::getline(ss, s_gx, ',');
                std::getline(ss, s_gy, ',');
                std::getline(ss, s_gz, ',');
                std::getline(ss, s_sx, ',');
                std::getline(ss, s_sy, ',');
                std::getline(ss, s_sz, ',');
                std::getline(ss, s_rx, ',');
                std::getline(ss, s_ry, ',');
                std::getline(ss, s_rz, ',');

                float gx = std::stof(s_gx);
                float gy = std::stof(s_gy);
                float gz = std::stof(s_gz);
                float sx = std::stof(s_sx);
                float sz = std::stof(s_sz);
                float rx = s_rx.empty() ? 0.0f : std::stof(s_rx);
                float ry = s_ry.empty() ? 0.0f : std::stof(s_ry);
                float rz = s_rz.empty() ? 0.0f : std::stof(s_rz);

                // コライダー情報があれば読み取る
                std::string s_col_type, s_cx, s_cy, s_cz, s_csx, s_csy, s_csz;
                bool hasCollider = false;
                Vector3 colCenter = { 0.0f, 0.0f, 0.0f };
                Vector3 colSize = { 2.0f, 2.0f, 2.0f };
                
                // 無効フラグ
                bool isDisabled = false;
                std::string s_disabled;
                
                if (std::getline(ss, s_col_type, ',')) {
                    if (s_col_type == "BOX" &&
                        std::getline(ss, s_cx, ',') &&
                        std::getline(ss, s_cy, ',') &&
                        std::getline(ss, s_cz, ',') &&
                        std::getline(ss, s_csx, ',') &&
                        std::getline(ss, s_csy, ',') &&
                        std::getline(ss, s_csz, ',')) {
                        
                        hasCollider = true;
                        colCenter = { std::stof(s_cx), std::stof(s_cz), std::stof(s_cy) };
                        colSize = { std::stof(s_csx), std::stof(s_csz), std::stof(s_csy) };
                    } else if (s_col_type == "NONE") {
                        std::string dummy;
                        for (int d = 0; d < 6; ++d) {
                            std::getline(ss, dummy, ',');
                        }
                    }
                    
                    if (std::getline(ss, s_disabled, ',')) {
                        if (!s_disabled.empty() && std::stoi(s_disabled) == 1) {
                            isDisabled = true;
                        }
                    }
                }
                
                if (isDisabled) {
                    continue;
                }

                // 既にずれている座標と比較するため、読み込んだ gx にも kWorldShiftX を加算
                float shiftedGx = gx + kWorldShiftX;
                int foundIdx = -1;
                for (int i = 0; i < buildingCount; ++i) {
                    if (std::abs(buildings_[i].position.x - shiftedGx) < 1.0f &&
                        std::abs(buildings_[i].position.z - gz) < 1.0f) {
                        foundIdx = i;
                        break;
                    }
                }

                if (foundIdx != -1) {
                    buildings_[foundIdx].floors++;
                    buildings_[foundIdx].originalFloors = buildings_[foundIdx].floors;
                    if (hasCollider) {
                        buildings_[foundIdx].collider.hasCollider = true;
                        buildings_[foundIdx].collider.center = colCenter;
                        buildings_[foundIdx].collider.size = colSize;
                    }
                } else if (buildingCount < kMaxBuildings) {
                    Building b;
                    b.position = { shiftedGx, -20.0f, gz }; // 底面を基準高さ -20.0f
                    b.scale = { sx, 10.0f, sz };     
                    b.rotate = { rx, ry, rz };
                    b.floors = 1;
                    b.originalX = shiftedGx;
                    b.originalY = -20.0f;
                    b.originalFloors = 1;
                    b.isDestroyed = false;
                    b.velocity = { 0.0f, 0.0f, 0.0f };
                    b.rotationSpeed = { 0.0f, 0.0f, 0.0f };
                    b.destroyTimer = 0.0f;
                    b.collider.hasCollider = hasCollider;
                    b.collider.center = colCenter;
                    b.collider.size = colSize;

                    buildings_[buildingCount] = b;
                    buildingCount++;
                }
            } else if (type == "ENEMY") {
                std::string s_gx, s_gy, s_gz;
                std::getline(ss, s_gx, ',');
                std::getline(ss, s_gy, ',');
                std::getline(ss, s_gz, ',');

                float gx = std::stof(s_gx);
                float gy = std::stof(s_gy);
                float gz = std::stof(s_gz);

                // 敵のBoxコライダー用の読み込み (3.8,3.8,3.8,0.0,0.0,0.0 の6列をスキップ)
                std::string dummy;
                for (int d = 0; d < 6; ++d) {
                    std::getline(ss, dummy, ',');
                }
                
                std::string s_col_type, s_cx, s_cy, s_cz, s_csx, s_csy, s_csz;
                bool hasCollider = false;
                Vector3 colCenter = { 0.0f, 0.0f, 0.0f };
                Vector3 colSize = { 2.0f, 2.0f, 2.0f };
                
                bool isDisabled = false;
                std::string s_disabled;
                
                if (std::getline(ss, s_col_type, ',')) {
                    if (s_col_type == "BOX" &&
                        std::getline(ss, s_cx, ',') &&
                        std::getline(ss, s_cy, ',') &&
                        std::getline(ss, s_cz, ',') &&
                        std::getline(ss, s_csx, ',') &&
                        std::getline(ss, s_csy, ',') &&
                        std::getline(ss, s_csz, ',')) {
                        
                        hasCollider = true;
                        colCenter = { std::stof(s_cx), std::stof(s_cz), std::stof(s_cy) };
                        colSize = { std::stof(s_csx), std::stof(s_csz), std::stof(s_csy) };
                    } else if (s_col_type == "NONE") {
                        std::string dummy_col;
                        for (int d = 0; d < 6; ++d) {
                            std::getline(ss, dummy_col, ',');
                        }
                    }
                    
                    if (std::getline(ss, s_disabled, ',')) {
                        if (!s_disabled.empty() && std::stoi(s_disabled) == 1) {
                            isDisabled = true;
                        }
                    }
                }

                if (isDisabled) {
                    continue;
                }

                if (enemyCount < kMaxEnemies) {
                    enemies_[enemyCount].position = { gx + kWorldShiftX, gy, gz };
                    enemies_[enemyCount].isAlive = true;
                    enemies_[enemyCount].hp = 3.0f;
                    enemies_[enemyCount].radius = 3.8f;
                    enemies_[enemyCount].collider.hasCollider = hasCollider;
                    enemies_[enemyCount].collider.center = colCenter;
                    enemies_[enemyCount].collider.size = colSize;
                    enemyCount++;
                }
            } else if (type == "FLOOR") {
                std::string s_gx, s_gy, s_gz;
                std::getline(ss, s_gx, ',');
                std::getline(ss, s_gy, ',');
                std::getline(ss, s_gz, ',');

                // 不要なスケールやダミー回転を読み飛ばす (300.0, 1.0, 200.0, 0.0, 0.0, 0.0)
                std::string dummy;
                for (int d = 0; d < 6; ++d) {
                    std::getline(ss, dummy, ',');
                }

                std::string s_rx, s_ry, s_rz;
                std::getline(ss, s_rx, ',');
                std::getline(ss, s_ry, ',');
                std::getline(ss, s_rz, ',');

                float gx = std::stof(s_gx);
                float gy = std::stof(s_gy);
                float gz = std::stof(s_gz);
                float rx = s_rx.empty() ? 0.0f : std::stof(s_rx);
                float ry = s_ry.empty() ? 0.0f : std::stof(s_ry);
                float rz = s_rz.empty() ? 0.0f : std::stof(s_rz);

                if (floorCount < kNumFloors) {
                    floorPositions_[floorCount] = { gx + kWorldShiftX, kFloorY, gz };
                    floorRotations_[floorCount] = { rx, ry, rz };
                    floorCount++;
                }
            } else if (type == "WAYPOINT") {
                std::string s_gx, s_gy, s_gz;
                std::getline(ss, s_gx, ',');
                std::getline(ss, s_gy, ',');
                std::getline(ss, s_gz, ',');

                float gx = std::stof(s_gx);
                float gy = std::stof(s_gy);
                float gz = std::stof(s_gz);

                waypoints_.push_back({ gx + kWorldShiftX, gy, gz });
            }
        }
        layoutInFile.close();
    }
    numLoadedFloors_ = floorCount;

    if (waypoints_.empty()) {
        for (float z = 0.0f; z <= 10000.0f; z += 10.0f) {
            waypoints_.push_back({ kWorldShiftX, -20.0f, z });
        }
    }

    // ── 累積距離（アークレングス）の事前計算 ──
    waypointDistances_.clear();
    if (!waypoints_.empty()) {
        waypointDistances_.resize(waypoints_.size());
        waypointDistances_[0] = 0.0f;
        for (size_t i = 1; i < waypoints_.size(); ++i) {
            Vector3 diff = Subtract(waypoints_[i], waypoints_[i - 1]);
            float dist = std::sqrt(diff.x * diff.x + diff.y * diff.y + diff.z * diff.z);
            waypointDistances_[i] = waypointDistances_[i - 1] + dist;
        }

        // ── プレイヤー進行ルート（コース中央脇）へのビル増量・高密度配置 ──
        if (!waypointDistances_.empty() && waypointDistances_.back() > 0.0f) {
            float totalDist = waypointDistances_.back();
            float buildingPitch = 40.0f; // 40m間隔で進行方向の左右にビルを高密度補填

            for (float s = 20.0f; s < totalDist; s += buildingPitch) {
                Vector3 railPos = GetRailPosition(s);
                Vector3 railDir = GetRailDirection(s);
                Vector3 railRight = CalculateRailRight(railDir);

                // コース中央脇の左右4レーン (±45m, ±85m)
                const float sideOffsets[] = { -45.0f, +45.0f, -85.0f, +85.0f };
                for (int k = 0; k < 4; ++k) {
                    float offset = sideOffsets[k];
                    Vector3 bPos = Add(railPos, Scale(railRight, offset));
                    bPos.y = -20.0f; // 地面基準高さ

                    // 既存のビルとの重なりチェック (20m以内なら配置スキップ)
                    bool isOverlap = false;
                    for (size_t bIdx = 0; bIdx < buildings_.size(); ++bIdx) {
                        if (buildings_[bIdx].floors <= 0) continue;
                        float dx = buildings_[bIdx].position.x - bPos.x;
                        float dz = buildings_[bIdx].position.z - bPos.z;
                        if (dx * dx + dz * dz < 20.0f * 20.0f) {
                            isOverlap = true;
                            break;
                        }
                    }

                    if (!isOverlap && buildings_.size() < kMaxBuildings) {
                        Building b;
                        b.position = bPos;
                        b.scale = { 10.0f, 10.0f, 10.0f };
                        b.isNearCourseColumn = true; // 中央＆すぐ隣の列フラグ（詳細描画用）

                        // 近列(±45m)は中層(3~8階)、外列(±85m)は高層(6~12階)
                        if (std::abs(offset) < 60.0f) {
                            b.floors = 3 + (randomEngine_() % 6);
                        } else {
                            b.floors = 6 + (randomEngine_() % 7);
                        }

                        float rotY = std::atan2(railDir.x, railDir.z);
                        b.rotate = { 0.0f, rotY, 0.0f };

                        b.originalX = bPos.x;
                        b.originalY = -20.0f;
                        b.originalFloors = b.floors;
                        b.isDestroyed = false;
                        b.velocity = { 0.0f, 0.0f, 0.0f };
                        b.rotationSpeed = { 0.0f, 0.0f, 0.0f };
                        b.destroyTimer = 0.0f;

                        buildings_.push_back(b);
                    }
                }
            }

            // ── 曲がり角（カーブ手前の直線および直進延長）での高密度背景ビル補填 ──
            float stepCheck = 40.0f; // 40mごとにチェック
            for (float s = 40.0f; s < totalDist - 40.0f; s += stepCheck) {
                float prevS = (s - 30.0f < 0.0f) ? 0.0f : (s - 30.0f);
                float nextS = (s + 30.0f > totalDist) ? totalDist : (s + 30.0f);
                Vector3 dirPrev = GetRailDirection(prevS);
                Vector3 dirNext = GetRailDirection(nextS);

                // 進行方向の変化量（カーブ判定）
                float dotVal = dirPrev.x * dirNext.x + dirPrev.z * dirNext.z;
                if (dotVal < 0.985f) { // カーブしているポイント
                    Vector3 dirStraight = dirPrev;
                    Vector3 rightStraight = CalculateRailRight(dirStraight);
                    Vector3 pStart = GetRailPosition(s);

                    // 直進方向（ダミー道路が伸びている方向）へ 15 ステップ（各 80m）高密度に伸ばす
                    const float dummyCols[] = {
                        -45.0f, 45.0f, -85.0f, 85.0f,
                        -125.0f, 125.0f, -165.0f, 165.0f, -205.0f, 205.0f,
                        -245.0f, 245.0f, -285.0f, 285.0f, -325.0f, 325.0f, -365.0f, 365.0f
                    };
                    for (int d = 1; d <= 15; ++d) {
                        float dDist = d * 80.0f;
                        Vector3 pDummy = Add(pStart, Scale(dirStraight, dDist));

                        for (int k = 0; k < 18; ++k) {
                            float offset = dummyCols[k];
                            Vector3 bPos = Add(pDummy, Scale(rightStraight, offset));
                            bPos.y = -20.0f;

                            // 既存ビルとの重なりチェック (20m以内)
                            bool isOverlap = false;
                            for (size_t bIdx = 0; bIdx < buildings_.size(); ++bIdx) {
                                if (buildings_[bIdx].floors <= 0) continue;
                                float dx = buildings_[bIdx].position.x - bPos.x;
                                float dz = buildings_[bIdx].position.z - bPos.z;
                                if (dx * dx + dz * dz < 20.0f * 20.0f) {
                                    isOverlap = true;
                                    break;
                                }
                            }

                            if (!isOverlap && buildings_.size() < kMaxBuildings) {
                                Building b;
                                b.position = bPos;
                                b.scale = { 10.0f, 10.0f, 10.0f };
                                b.floors = 3 + (randomEngine_() % 8);
                                b.isNearCourseColumn = (std::abs(offset) < 90.0f); // 中央＆隣接列のみ詳細描画
                                float rotY = std::atan2(dirStraight.x, dirStraight.z);
                                b.rotate = { 0.0f, rotY, 0.0f };
                                b.originalX = bPos.x;
                                b.originalY = -20.0f;
                                b.originalFloors = b.floors;
                                b.isDestroyed = false;
                                b.velocity = { 0.0f, 0.0f, 0.0f };
                                b.rotationSpeed = { 0.0f, 0.0f, 0.0f };
                                b.destroyTimer = 0.0f;

                                buildings_.push_back(b);
                            }
                        }
                    }
                }
            }
        }
    }

    // ── プレイヤーが通る中央タイル/レールと重なっているビルを自動削除 ──
    if (!waypoints_.empty() && !waypointDistances_.empty() && waypointDistances_.back() > 0.0f) {
        float totalDist = waypointDistances_.back();
        float checkStep = 10.0f; // 10m間隔で進行ライン上の判定を網羅
        const float kCenterClearRadius = 38.0f; // 中央レール中心から38m以内（道路中央通行域）のビルは削除対象

        for (size_t i = 0; i < buildings_.size(); ++i) {
            if (buildings_[i].floors <= 0 || buildings_[i].isDestroyed) continue;

            bool isOverlappingWithCenter = false;
            for (float s = 0.0f; s <= totalDist; s += checkStep) {
                Vector3 railPos = GetRailPosition(s);
                float dx = buildings_[i].position.x - railPos.x;
                float dz = buildings_[i].position.z - railPos.z;
                if (dx * dx + dz * dz < kCenterClearRadius * kCenterClearRadius) {
                    isOverlappingWithCenter = true;
                    break;
                }
            }

            if (isOverlappingWithCenter) {
                // 中央タイルと重なっているビルを削除 (floors = 0)
                buildings_[i].floors = 0;
                buildings_[i].originalFloors = 0;
            }
        }

        // ── プレイヤーが通るルート（コース/レール）の近くにあるビルの自動判定 ──
        float routeCheckStep = 10.0f; // 10m間隔でルート上の点を判定
        const float kNearRouteDist = 130.0f; // ルート中心から130m以内（コース両脇および直近ビル列）
        const float kNearRouteDistSq = kNearRouteDist * kNearRouteDist;

        for (size_t i = 0; i < buildings_.size(); ++i) {
            if (buildings_[i].floors <= 0) continue;

            float minSqDist = 1e9f;
            for (float s = 0.0f; s <= totalDist; s += routeCheckStep) {
                Vector3 railPos = GetRailPosition(s);
                float dx = buildings_[i].position.x - railPos.x;
                float dz = buildings_[i].position.z - railPos.z;
                float sqDist = dx * dx + dz * dz;
                if (sqDist < minSqDist) {
                    minSqDist = sqDist;
                }
            }

            // ルートの近くにあるビル、または直進延長上のビルは isNearCourseColumn = true （高詳細描画対象）
            // 元の isNearCourseColumn（曲がり角延長上の高密度補テンフラグ等）を保持しつつ判定
            buildings_[i].isNearCourseColumn = buildings_[i].isNearCourseColumn || (minSqDist <= kNearRouteDistSq);
        }
    }

    // ── デモ用データの初期化 ──
    shotBeams_.resize(kMaxShotBeams);
    for (int i = 0; i < kMaxShotBeams; ++i) {
        shotBeams_[i].isAlive = false;
    }

    // フラッシュ演出用の定数バッファ生成とマッピング
    flashMaterialResource_ = CreateBufferResource(device, sizeof(Material));
    flashMaterialResource_->Map(0, nullptr, reinterpret_cast<void**>(&flashMaterialData_));
    flashMaterialData_->color = { 1.0f, 1.0f, 1.0f, 0.0f }; // 初期は完全透明
    flashMaterialData_->uvTransform = MakeIdentity4x4();

    flashTransformResource_ = CreateBufferResource(device, sizeof(TransformationMatrix));
    flashTransformResource_->Map(0, nullptr, reinterpret_cast<void**>(&flashTransformData_));
    flashTransformData_->WVP = MakeIdentity4x4();
    flashTransformData_->World = MakeIdentity4x4();

    // デモ用テクスチャのロード（フラッシュやヒットエフェクトで使うテクスチャを予め確保）
    TextureManager::GetInstance()->LoadTexture("aiming.png");
    flashSrvHandleGPU_ = TextureManager::GetInstance()->GetSrvHandleGPU("aiming.png");
    
    // 近接突撃用メンバの初期化
    currentFighterPos_ = playerPos_;
    attackMode_ = AttackMode::kShooting;
    meleeState_ = MeleeState::kIdle;
    digitalGlitchTimer_ = 0.0f;
}

void GamePlayScene::StartPhaseIntro(int phaseNum) {
    phaseIntroNum_ = phaseNum;
    phaseIntroTimer_ = 0.0f;

    // Asepriteで作られたフェーズ画像と数字画像をロードしてスプライト生成
    phaseIntroSprite_ = Sprite::Create("phase.png", { 0.0f, 0.0f });
    std::string numberTex = "number/big" + std::to_string(phaseNum) + ".png";
    numberIntroSprite_ = Sprite::Create(numberTex, { 0.0f, 0.0f });

    // 効果音を鳴らす (少し高めの音量で)
    if (audio_) {
        audio_->PlayWave(jumpSE_, false, 1.2f);
    }
}

void GamePlayScene::Finalize() {
    if (audio_) {
        audio_->Finalize();
    }
}

void GamePlayScene::Update() {
    float kDeltaTime = 1.0f / 60.0f;

    if (isDemoMode_) {
        UpdateDemo(kDeltaTime);
        return;
    }

    // 0. 数字キーによるプリセット切り替え（通常プレイ用）
    if (input_->IsKeyTriggered(DIK_1)) ApplyPreset(0);
    else if (input_->IsKeyTriggered(DIK_2)) ApplyPreset(1);
    else if (input_->IsKeyTriggered(DIK_3)) ApplyPreset(2);
    else if (input_->IsKeyTriggered(DIK_4)) ApplyPreset(3);
    else if (input_->IsKeyTriggered(DIK_5)) ApplyPreset(4);
    else if (input_->IsKeyTriggered(DIK_6)) ApplyPreset(5);
    else if (input_->IsKeyTriggered(DIK_7)) ApplyPreset(6);
    else if (input_->IsKeyTriggered(DIK_8)) ApplyPreset(7);
    else if (input_->IsKeyTriggered(DIK_9)) ApplyPreset(8);
    else if (input_->IsKeyTriggered(DIK_0)) ApplyPreset(9);

    // 0.1. Tキーによる背景（スカイボックス）の切り替え
    if (input_->IsKeyTriggered(DIK_T)) {
        skyboxType_ = (skyboxType_ + 1) % static_cast<int>(kNumSkyboxTextures);
    }

    // 0.5. 各種演出タイマーの更新と時間スケール（ヒットストップ）処理
    if (hitstopTimer_ > 0.0f) {
        hitstopTimer_ -= kDeltaTime;
        kDeltaTime = kDeltaTime * 0.05f; // スローモーション化
    }

    if (cameraShakeTimer_ > 0.0f) {
        cameraShakeTimer_ -= kDeltaTime;
        if (cameraShakeTimer_ < 0.0f) cameraShakeTimer_ = 0.0f;
        
        float progress = cameraShakeTimer_ / (cameraShakeTimeMax_ > 0.0f ? cameraShakeTimeMax_ : 1.0f);
        progress = std::clamp(progress, 0.0f, 1.0f);
        float currentIntensity = activeShakeIntensity_ * (progress * progress);
        
        std::uniform_real_distribution<float> distShake(-1.0f, 1.0f);
        if (selectedEffectPreset_ == 7) {
            cameraShakeOffset_ = {
                distShake(randomEngine_) * currentIntensity,
                0.0f,
                0.0f
            };
        } else {
            cameraShakeOffset_ = {
                distShake(randomEngine_) * currentIntensity,
                distShake(randomEngine_) * currentIntensity,
                distShake(randomEngine_) * currentIntensity
            };
        }
    } else {
        cameraShakeOffset_ = { 0.0f, 0.0f, 0.0f };
    }

    if (flashAlpha_ > 0.0f) {
        float fadeSpeed = (selectedEffectPreset_ == 8) ? 1.5f : 3.5f;
        flashAlpha_ -= kDeltaTime * fadeSpeed;
        if (flashAlpha_ < 0.0f) flashAlpha_ = 0.0f;
    }

    if (useRadialBlur_ && blurIntensity_ > 0.0f) {
        blurIntensity_ -= kDeltaTime * (maxBlurWidth_ / 0.25f);
        if (blurIntensity_ < 0.0f) blurIntensity_ = 0.0f;
        
        activePostProcess_ = kRadialBlur;
        if (radialBlurParamData_) {
            radialBlurParamData_->center = { 0.5f, 0.5f };
            radialBlurParamData_->blurWidth = -blurIntensity_;
        }
    } else {
        if (activePostProcess_ == kRadialBlur) {
            activePostProcess_ = kNone;
        }
    }

    if (digitalGlitchTimer_ > 0.0f) {
        digitalGlitchTimer_ -= kDeltaTime;
        if (digitalGlitchTimer_ < 0.0f) digitalGlitchTimer_ = 0.0f;
        
        activePostProcess_ = kRandom;
        if (randomParamResource_ && randomParamData_) {
            randomParamData_->time = randomEffectTime_;
            randomParamData_->noiseScale = 100.0f;
            randomParamData_->noiseStrength = 0.85f;
            randomParamData_->isColorNoise = false;
            randomParamData_->isMultiplyNoise = false;
        }
    } else {
        if (activePostProcess_ == kRandom) {
            activePostProcess_ = kNone;
        }
    }

    // ゲームクリア/ゲームオーバー遷移判定
    if (bossHP_ <= 0.0f && !isBossDefeatedSequence_) {
        isBossDefeatedSequence_ = true;
        bossDefeatTimer_ = 0.0f;
        hasHitBoss_ = false;
        isBossModelVisible_ = true;
        isDefeatBulletActive_ = false;
        bossDefeatHitTimer_ = 0.0f;
        
        // パーティクルを一旦すべてクリア
        if (particleManager_) {
            particleManager_->Clear();
        }
        
        // プレイヤーの速度を通常に戻し、バレルロールやブーストを強制解除
        isBarrelRolling_ = false;
        isBoosting_ = false;
        boostBlurWidth_ = 0.0f;
        boostForwardSpeed_ = kNormalSpeed;
        
        // プレイヤーの弾をすべて消去（寿命切れにする）
        for (auto& b : playerBullets_) {
            b.currentTime = b.lifeTime;
        }
        // ボスの蜘蛛の巣弾などをクリア
        bossWebBullets_.clear();
    }

    if (isBossDefeatedSequence_) {
        bossDefeatTimer_ += kDeltaTime;
        
        // カメラトランスフォームの参照を取得
        EulerTransform& camTrans = camera_->GetTransform();
        
        // 1. 自機を画面中央（X=0, Y=0）に滑らかに補間移動
        if (fighterModel_) {
            fighterModel_->transform.translate.x = std::lerp(fighterModel_->transform.translate.x, 0.0f, 0.1f);
            fighterModel_->transform.translate.y = std::lerp(fighterModel_->transform.translate.y, 0.0f, 0.1f);
            fighterModel_->transform.rotate.x = std::lerp(fighterModel_->transform.rotate.x, 0.0f, 0.1f);
            fighterModel_->transform.rotate.y = std::lerp(fighterModel_->transform.rotate.y, 0.0f, 0.1f);
            fighterModel_->transform.rotate.z = std::lerp(fighterModel_->transform.rotate.z, 0.0f, 0.1f);
            
            // プレイヤーのロール・ピッチもリセット
            playerRotationRoll_ = std::lerp(playerRotationRoll_, 0.0f, 0.1f);
            playerRotationPitch_ = std::lerp(playerRotationPitch_, 0.0f, 0.1f);
        }
        
        // 自機のZ前進は継続（ボスの位置との整合性を保つため）
        fighterWorldZ_ += boostForwardSpeed_ * kDeltaTime;
        
        // 自機のワールド位置
        Vector3 fighterWorldPos = {
            camTrans.translate.x + (fighterModel_ ? fighterModel_->transform.translate.x : 0.0f),
            camTrans.translate.y - 3.0f + (fighterModel_ ? fighterModel_->transform.translate.y : 0.0f),
            fighterWorldZ_
        };
        
        // ボスのワールド位置
        float bodyBounce = 0.0f;
        if (bossLegSwingSpeed_ > 0.0f) {
            bodyBounce = std::sin(bossTime_ * 2.0f) * bossBodyBounceRange_;
        }
        Vector3 bossPos = GetBossPosition(bodyBounce);
        
        // ==========================================
        // 【カメラワーク】背後65m、高さ26m、ボス注視アングル（常に維持）
        // ==========================================
        float camProgress = fighterWorldZ_ - 65.0f;
        Vector3 camRailPos = GetRailPosition(camProgress);
        Vector3 camRailDir = GetRailDirection(camProgress);
        Vector3 camRailRight = CalculateRailRight(camRailDir);
        Vector3 camRailUp = CalculateRailUp(camRailDir, camRailRight);
        Vector3 targetCamPos = Add(camRailPos, Scale(camRailUp, 26.0f)); // 通常時 Yオフセット 26.0f
        
        // 開始フレーム（最初のフレーム）は即座にワープ、それ以外はスムーズに補間
        bool isStartFrame = (bossDefeatTimer_ <= kDeltaTime);
        camTrans.translate = isStartFrame ? targetCamPos : Lerp(camTrans.translate, targetCamPos, 0.08f);
        
        // カメラからボスへの方向ベクトルから回転角を計算して注視させる
        Vector3 lookDir = Subtract(bossPos, camTrans.translate);
        float lookDist = Length(lookDir);
        if (lookDist > 0.001f) {
            lookDir = Scale(lookDir, 1.0f / lookDist);
        } else {
            lookDir = camRailDir;
        }
        
        float targetRotY = -std::atan2(lookDir.x, lookDir.z);
        float targetRotX = std::atan2(-lookDir.y, std::sqrt(lookDir.x * lookDir.x + lookDir.z * lookDir.z));
        
        float rotLerpFactor = isStartFrame ? 1.0f : 0.08f;
        
        float diffY = targetRotY - camTrans.rotate.y;
        while (diffY < -static_cast<float>(M_PI)) diffY += 2.0f * static_cast<float>(M_PI);
        while (diffY > static_cast<float>(M_PI)) diffY -= 2.0f * static_cast<float>(M_PI);
        camTrans.rotate.y += diffY * rotLerpFactor;

        float diffX = targetRotX - camTrans.rotate.x;
        while (diffX < -static_cast<float>(M_PI)) diffX += 2.0f * static_cast<float>(M_PI);
        while (diffX > static_cast<float>(M_PI)) diffX -= 2.0f * static_cast<float>(M_PI);
        camTrans.rotate.x += diffX * rotLerpFactor;
        
        camTrans.rotate.z = isStartFrame ? 0.0f : std::lerp(camTrans.rotate.z, 0.0f, rotLerpFactor);
        
        // ==========================================
        // 【演出エフェクト】チャージ期 / 弾丸飛行期 / 爆発期
        // ==========================================
        if (bossDefeatTimer_ < 0.8f) {
            // --- チャージエフェクトの発生 ---
            Vector3 leftWing = Add(fighterWorldPos, Add(Scale(camRailRight, -3.5f), Scale(camRailUp, 0.8f)));
            Vector3 rightWing = Add(fighterWorldPos, Add(Scale(camRailRight, 3.5f), Scale(camRailUp, 0.8f)));
            Vector3 chargeColor = { 0.4f, 0.7f, 1.0f }; // 神聖な青白
            
            if (particleManager_) {
                particleManager_->EmitLSystemLightning(leftWing, bossPos, 3, -0.6f, chargeColor);
                particleManager_->EmitLSystemLightning(rightWing, bossPos, 3, -0.6f, chargeColor);
                particleManager_->EmitHolyLight(fighterWorldPos, 8.0f, 3, chargeColor);
            }
            
            // チャージによる空間の震え（画面シェイク）
            cameraShakeTimer_ = 0.05f;
            cameraShakeIntensity_ = 1.2f;
            activeShakeIntensity_ = 1.2f;
            cameraShakeTimeMax_ = 0.05f;
        }
        else if (isDefeatBulletActive_) {
            // 巨大弾のトレイルエフェクト
            if (particleManager_) {
                Vector3 bulletTrailColor = { 0.8f, 0.9f, 1.0f };
                particleManager_->EmitHolyLight(defeatBulletPos_, 4.0f, 4, bulletTrailColor);
                
                // 弾の周囲に激しくうねる電撃
                float offsetVal = 6.0f;
                Vector3 randomOffset = { 
                    (float)(rand() % 100 - 50) / 100.0f * offsetVal, 
                    (float)(rand() % 100 - 50) / 100.0f * offsetVal, 
                    (float)(rand() % 100 - 50) / 100.0f * offsetVal 
                };
                particleManager_->EmitLSystemLightning(defeatBulletPos_, Add(defeatBulletPos_, randomOffset), 2, -0.4f, bulletTrailColor);
            }
        }
        else if (hasHitBoss_) {
            // --- 連鎖大爆発エフェクトの発生 ---
            if (particleManager_) {
                // 0.15秒おきに計5回、ボスの周囲に花火大爆発を連続発生
                static float lastFireworkTime = -1.0f;
                static int fireworkCount = 0;
                
                if (bossDefeatHitTimer_ - lastFireworkTime >= 0.15f && fireworkCount < 5) {
                    lastFireworkTime = bossDefeatHitTimer_;
                    fireworkCount++;
                    
                    std::uniform_real_distribution<float> offsetDist(-20.0f, 20.0f);
                    Vector3 fPos = {
                        bossPos.x + offsetDist(randomEngine_),
                        bossPos.y + offsetDist(randomEngine_),
                        bossPos.z + offsetDist(randomEngine_)
                    };
                    
                    Vector3 fwColor = {
                        (float)(rand() % 100) / 100.0f,
                        (float)(rand() % 100) / 100.0f,
                        (float)(rand() % 100) / 100.0f
                    };
                    if (Length(fwColor) < 0.5f) fwColor = { 1.0f, 0.5f, 0.2f };
                    
                    particleManager_->EmitFirework(fPos, fwColor);
                    particleManager_->EmitMegaRing(fPos, fwColor);
                    
                    // 爆発の度に追加爆音SE
                    if (audio_) {
                        audio_->PlayWave(jumpSE_, false, 1.4f);
                    }
                }
                
                // 毎フレーム空間に降り注ぐスパークと光の粒子
                particleManager_->EmitCustomSparks(bossPos, 35.0f, 4, { 1.0f, 0.8f, 0.3f }, 0.8f);
                particleManager_->EmitHolyLight(bossPos, 15.0f, 3, { 1.0f, 1.0f, 1.0f });
            }
        }

        // 2. トドメの巨大弾の自動発射 (0.8秒時点)
        if (bossDefeatTimer_ >= 0.8f && !isDefeatBulletActive_ && !hasHitBoss_) {
            isDefeatBulletActive_ = true;
            
            defeatBulletPos_ = fighterWorldPos;
            
            // 弾の速度ベクトル (ボスへ向かう方向)
            Vector3 dir = Normalize(Subtract(bossPos, defeatBulletPos_));
            float bulletSpeed = 380.0f; // より高速に射撃
            defeatBulletVel_ = Scale(dir, bulletSpeed);
            
            // トドメの弾サイズをさらに巨大化（3.5f）
            defeatBulletSize_ = 3.5f;
            
            // 発射瞬間のマズル衝撃波リング＆シリンダー＆大爆発
            if (particleManager_) {
                particleManager_->EmitMegaRing(fighterWorldPos, { 0.4f, 0.8f, 1.0f });
                particleManager_->EmitMegaCylinder(fighterWorldPos, { 0.4f, 0.8f, 1.0f });
                particleManager_->EmitFlame(fighterWorldPos, 30.0f, 40, { 0.4f, 0.8f, 1.0f });
            }
            
            // 発射SE
            if (audio_) {
                audio_->PlayWave(jumpSE_, false, 1.5f);
            }
        }
        
        // 3. トドメの巨大弾の更新とヒット判定
        if (isDefeatBulletActive_) {
            defeatBulletPos_ = Add(defeatBulletPos_, Scale(defeatBulletVel_, kDeltaTime));
            
            // 衝突判定 (ボスの衝突半径または弾がボスを追い抜いたか)
            Vector3 diff = Subtract(defeatBulletPos_, bossPos);
            float dist = Length(diff);
            if (dist <= bossCollisionRadius_ || defeatBulletPos_.z >= bossPos.z) {
                isDefeatBulletActive_ = false;
                hasHitBoss_ = true;
                isBossModelVisible_ = false; // ボスを消滅させる
                bossDefeatHitTimer_ = 0.0f;  // ヒットした瞬間からタイマーを開始
                
                // ★★★ スローモーション（ヒットストップ）の発動 ★★★
                hitstopTimer_ = 0.65f; // 0.65秒間超スローモーション化
                
                // 大きく白い十字Particleエフェクトを発生 (ボスのレール空間を考慮)
                float bossProgress = fighterWorldZ_ + bossZOffset_;
                Vector3 bRailDir = GetRailDirection(bossProgress);
                Vector3 bRailRight = CalculateRailRight(bRailDir);
                Vector3 bRailUp = CalculateRailUp(bRailDir, bRailRight);
                particleManager_->EmitWhiteCross(bossPos, bRailRight, bRailUp);
                
                // 白い超巨大多重衝撃波リングと巨大シリンダーエフェクトを追加してかっこよく演出
                particleManager_->EmitMegaRing(bossPos, {1.0f, 1.0f, 1.0f});
                particleManager_->EmitMegaCylinder(bossPos, {1.0f, 1.0f, 1.0f});
                
                // 周囲に飛び散る純白の激しいスパークエフェクトを追加
                particleManager_->EmitFlame(bossPos, 20.0f, 30, {1.0f, 1.0f, 1.0f});
                
                // ボスから地面・天に向けて四方八方に拡散する巨大稲妻を8本落とす
                Vector3 lightningColor = { 1.0f, 1.0f, 1.0f };
                for (int i = 0; i < 8; ++i) {
                    float theta = (i / 8.0f) * 2.0f * static_cast<float>(M_PI);
                    Vector3 strikeEnd = {
                        bossPos.x + std::cos(theta) * 60.0f,
                        bossPos.y - 40.0f, // 地面付近
                        bossPos.z + std::sin(theta) * 60.0f
                    };
                    particleManager_->EmitLSystemLightning(bossPos, strikeEnd, 4, 1.2f, lightningColor);
                }
                
                // 大爆発SE
                if (audio_) {
                    audio_->PlayWave(jumpSE_, false, 2.0f);
                }
                
                // 画面インパクトフラッシュ (真っ白に)
                flashAlpha_ = 1.0f;
                flashColor_ = { 1.0f, 1.0f, 1.0f, 1.0f };
                
                // 強烈なラジアルブラーによる爆発の衝撃波画面の歪み表現
                useRadialBlur_ = true;
                blurIntensity_ = 0.12f;
                maxBlurWidth_ = 0.12f;
                
                // 画面シェイクを2倍に強化して重厚な爆発振動を表現
                cameraShakeTimer_ = 0.80f;
                cameraShakeIntensity_ = 12.0f;
                activeShakeIntensity_ = 12.0f;
                cameraShakeTimeMax_ = 0.80f;
            }
        }
        
        // 被弾後タイマーのカウントを進める
        if (hasHitBoss_) {
            bossDefeatHitTimer_ += kDeltaTime;
        }
        
        // フラッシュの減衰速度をトドメ演出中はさらにゆっくりにする（かつ最初は最大で維持）
        if (flashAlpha_ > 0.0f) {
            float originalFade = kDeltaTime * ((selectedEffectPreset_ == 8) ? 1.5f : 3.5f);
            
            // 命中後0.4秒間はフラッシュを完全に1.0f（最大値）にロックして輝かせる
            if (bossDefeatHitTimer_ < 0.4f) {
                flashAlpha_ = 1.0f;
            } else {
                // その後、非常にゆっくりフェードアウトさせる（減衰速度 0.6f）
                float desiredFade = kDeltaTime * 0.6f;
                flashAlpha_ += (originalFade - desiredFade);
                flashAlpha_ = (std::min)(flashAlpha_, 1.0f);
            }
        }

        // 4. クリアシーンへの遷移判定 (撃破ヒット後、さらに2.0秒演出を持たせる)
        if (hasHitBoss_ && bossDefeatHitTimer_ >= 2.0f) {
            SceneManager::GetInstance()->ChangeScene("CLEAR");
            return;
        }
    }
    
    if (playerHP_ <= 0.0f) {
        SceneManager::GetInstance()->ChangeScene("GAMEOVER");
        return;
    }

    // HPVisualの補間更新（滑らかなHPバー減少用）
    playerHPVisual_ = std::lerp(playerHPVisual_, playerHP_, 0.1f);
    bossHPVisual_ = std::lerp(bossHPVisual_, bossHP_, 0.1f);

    // デバフおよび画面効果タイマーの更新
    if (playerSpeedDebuffTimer_ > 0.0f) {
        playerSpeedDebuffTimer_ -= kDeltaTime;
    }
    if (screenWebTimer_ > 0.0f) {
        screenWebTimer_ -= kDeltaTime;
    }

    // ── 「PHASE」表示演出タイマーの更新 ──
    if (phaseIntroTimer_ >= 0.0f) {
        phaseIntroTimer_ += kDeltaTime;
        
        float t = phaseIntroTimer_;
        float s = 1.0f; // スケール倍率
        float a = 1.0f; // アルファ値
        
        if (t < 0.5f) {
            // 1. フェードイン・縮小バウンド (0.0s 〜 0.5s)
            float rate = t / 0.5f;
            const float c1 = 1.70158f;
            const float c3 = c1 + 1.0f;
            float e = 1.0f + c3 * std::pow(rate - 1.0f, 3.0f) + c1 * std::pow(rate - 1.0f, 2.0f);
            s = 4.0f - 3.0f * e; // 4倍 ➔ 1倍 (EaseOutBack)
            a = rate; // 0.0 ➔ 1.0
        } else if (t < 1.5f) {
            // 2. 静止・ゆっくりズーム (0.5s 〜 1.5s)
            float rate = (t - 0.5f) / 1.0f;
            s = 1.0f + 0.1f * rate; // 1.0倍 ➔ 1.1倍
            a = 1.0f;
        } else if (t < 2.0f) {
            // 3. フェードアウト・拡大 (1.5s 〜 2.0s)
            float rate = (t - 1.5f) / 0.5f;
            s = 1.1f + 0.5f * rate * rate; // 1.1倍 ➔ 1.6倍 (EaseInQuad)
            a = 1.0f - rate; // 1.0 ➔ 0.0
        } else {
            // 演出終了
            phaseIntroTimer_ = -1.0f;
            phaseIntroSprite_.reset();
            numberIntroSprite_.reset();
        }

        // スプライトの位置とサイズを更新
        if (phaseIntroSprite_ && numberIntroSprite_) {
            float w1 = (float)phaseIntroSprite_->GetMetadata().width;
            float h1 = (float)phaseIntroSprite_->GetMetadata().height;
            float w2 = (float)numberIntroSprite_->GetMetadata().width;
            float h2 = (float)numberIntroSprite_->GetMetadata().height;
            
            float gap = 20.0f; // 文字と数字の間隔
            float totalW = (w1 + gap + w2) * s;
            
            float cx = 0.0f; // 平行投影(正射影)の中心は (0, 0)
            float cy = 0.0f;
            
            // PHASE文字の配置
            float x1 = cx - totalW / 2.0f;
            float y1 = (h1 * s) / 2.0f; // Y軸は上がプラス。Yスケール反転により、開始点を上端に設定
            phaseIntroSprite_->transform.translate = { x1, y1, 0.0f };
            phaseIntroSprite_->transform.scale = { w1 * s, -h1 * s, 1.0f }; // Yスケールを負にして上下反転を修正
            phaseIntroSprite_->SetColor({ 1.0f, 1.0f, 1.0f, a });
            
            // 数字の配置
            float x2 = x1 + (w1 + gap) * s;
            float y2 = (h2 * s) / 2.0f;
            numberIntroSprite_->transform.translate = { x2, y2, 0.0f };
            numberIntroSprite_->transform.scale = { w2 * s, -h2 * s, 1.0f }; // Yスケールを負にして上下反転を修正
            numberIntroSprite_->SetColor({ 1.0f, 1.0f, 1.0f, a });
        }
    }

    // ── フェーズ自動遷移 ──
    if (currentPhase_ != GamePhase::kBossFight) {
        phaseTimer_ += kDeltaTime;
        if (phaseTimer_ >= kPhaseDuration) {
            phaseTimer_ = 0.0f;
            if (currentPhase_ == GamePhase::kPhase1) {
                currentPhase_ = GamePhase::kPhase2;
                StartPhaseIntro(2); // PHASE 2の表示演出
            } else if (currentPhase_ == GamePhase::kPhase2) {
                currentPhase_ = GamePhase::kBossFight;
                bossAppearanceTimer_ = 5.0f; // ボス登場演出開始（5.0秒）
            }
        }
    }

    // ── ボス登場落下演出タイマーの更新 ──
    if (bossAppearanceTimer_ > 0.0f) {
        bossAppearanceTimer_ -= kDeltaTime;
        float t = 5.0f - bossAppearanceTimer_; // 0.0f から 5.0f

        // ① 1.8秒時点で着地 (1回限り)
        if (t >= 1.8f && !isBossLanded_) {
            isBossLanded_ = true;
            // 着地！画面シェイクと土煙・SE
            cameraShakeTimer_ = 0.50f;
            cameraShakeIntensity_ = 3.5f;
            activeShakeIntensity_ = 3.5f;
            cameraShakeTimeMax_ = 0.50f;
            if (audio_) {
                audio_->PlayWave(jumpSE_, false, 2.0f); // 大音量で着地SE
            }
            // 土煙パーティクル
            Vector3 landPos = GetBossPosition(0.0f);
            for (int k = 0; k < 12; ++k) {
                particleManager_->EmitHit(landPos);
            }
            particleManager_->EmitRing(landPos);
            particleManager_->EmitCylinder(landPos);
        }

        // ② 4.0秒時点で咆哮・ビル吹き飛ばし (1回限り)
        if (t >= 4.0f && !isBossRoared_) {
            isBossRoared_ = true;
            // 咆哮の大シェイク！ (シェイクの激しさを向上)
            cameraShakeTimer_ = 1.20f;
            cameraShakeIntensity_ = 10.0f;
            activeShakeIntensity_ = 10.0f;
            cameraShakeTimeMax_ = 1.20f;
            
            // 画面全体を赤いフラッシュで染める (フェードアウトは自動実行)
            flashColor_ = { 1.0f, 0.15f, 0.15f };
            flashAlpha_ = 0.95f;

            if (audio_) {
                audio_->PlayWave(jumpSE_, false, 3.0f); // 大音量の咆哮SE
            }
            
            // ボス位置から赤い高威力エネルギー衝撃波群を大放出
            Vector3 bossPos = GetBossPosition(0.0f);
            
            // 巨大衝撃波リング＆シリンダー
            particleManager_->EmitMegaRing(bossPos, {1.0f, 0.1f, 0.1f});
            particleManager_->EmitMegaCylinder(bossPos, {1.0f, 0.1f, 0.1f});
            
            // 空間を走る赤い雷撃
            particleManager_->EmitLightning(bossPos, 65.0f, 30, { 1.0f, 0.2f, 0.2f });
            
            // 重力渦（エネルギー凝縮）
            particleManager_->EmitGravityVortex(bossPos, 35.0f, 40, { 0.8f, 0.1f, 0.1f });
            
            // 暗黒紫のカオスボイド
            particleManager_->EmitChaosVoid(bossPos, 45.0f, 30, { 0.6f, 0.0f, 0.9f });
            
            // 激しい火花
            particleManager_->EmitCustomSparks(bossPos, 90.0f, 50, { 1.0f, 0.4f, 0.1f }, 1.5f);

            // 周囲のビルを衝撃波で吹き飛ばす（3Dのワールド直線距離で判定し、曲がり角での一斉消滅を防止）
            Vector3 roarPos = GetBossPosition(0.0f, 0.0f, 0.0f);
            for (int i = 0; i < kMaxBuildings; ++i) {
                if (!buildings_[i].isDestroyed) {
                    Vector3 diff = Subtract(buildings_[i].position, roarPos);
                    float distSq = diff.x * diff.x + diff.y * diff.y + diff.z * diff.z;
                    if (distSq < 180.0f * 180.0f) { // 180m 以内のビルを吹き飛ばす
                        buildings_[i].isDestroyed = true;
                        buildings_[i].destroyTimer = 0.0f;
                        
                        // 咆哮の風圧で外側へ激しく吹き飛ばす
                        float signX = (buildings_[i].position.x >= 0.0f) ? 1.0f : -1.0f;
                        buildings_[i].velocity = { signX * 85.0f, 70.0f, -20.0f + (float)(randomEngine_() % 30) };
                        buildings_[i].rotationSpeed = {
                            ((float)(randomEngine_() % 200) / 100.0f) - 1.0f,
                            ((float)(randomEngine_() % 200) / 100.0f) - 1.0f,
                            -signX * 4.0f
                        };
                    }
                }
            }
        }

        // ③ 咆哮継続中のエネルギー連続噴出演出 ＆ 空気の振動ブラー(集中線) ＆ FOVズーム (4.0s ～ 5.0s)
        if (t >= 4.0f && t < 5.0f) {
            float roarProgress = (t - 4.0f) / 1.0f;
            float easeRoar = std::sin(roarProgress * static_cast<float>(M_PI)); // 0 -> 1 -> 0

            // ① 空気の振動ブラー (ラジアルブラーによる集中線)
            activePostProcess_ = kRadialBlur;
            if (radialBlurParamData_) {
                radialBlurParamData_->center = { 0.5f, 0.5f };
                radialBlurParamData_->blurWidth = -0.06f * easeRoar; // 最大 -0.06 の強烈なブラー
            }

            // ② カメラのFOVズームイン＆アウト (急激に寄って、衝撃で吹き飛ぶように引く)
            float fov = 0.45f;
            if (roarProgress < 0.25f) {
                // 最初の0.25秒で一気にズームイン (FOV 0.45 -> 0.28)
                float zoomT = roarProgress / 0.25f;
                fov = std::lerp(0.45f, 0.28f, zoomT * zoomT);
            } else {
                // 残りの0.75秒で吹き飛ばされるようにズームアウトしてから通常に戻す (FOV 0.28 -> 0.52 -> 0.45)
                float zoomT = (roarProgress - 0.25f) / 0.75f;
                if (zoomT < 0.5f) {
                    float backT = zoomT / 0.5f;
                    fov = std::lerp(0.28f, 0.52f, backT);
                } else {
                    float backT = (zoomT - 0.5f) / 0.5f;
                    fov = std::lerp(0.52f, 0.45f, backT);
                }
            }
            camera_->SetFov(fov);

            // ③ 継続的な口元からの火花・炎
            static int emitCounter = 0;
            emitCounter++;
            if (emitCounter % 3 == 0) {
                Vector3 bossPos = GetBossPosition(0.0f);
                float bossProgress = fighterWorldZ_ + bossZOffset_;
                Vector3 bossRailDir = GetRailDirection(bossProgress);
                Vector3 bossRailUp = Cross(bossRailDir, Normalize(Vector3{ -bossRailDir.z, 0.0f, bossRailDir.x }));
                Vector3 mouthPos = Add(bossPos, Add(Scale(bossRailUp, 6.0f), Scale(bossRailDir, -22.0f)));
                particleManager_->EmitFlame(mouthPos, 70.0f, 6, { 1.0f, 0.1f, 0.1f });
                particleManager_->EmitCustomSparks(mouthPos, 80.0f, 10, { 1.0f, 0.5f, 0.1f }, 1.0f);
            }
        }

        // ④ 演出終了時のリセットとクリーンアップ
        if (bossAppearanceTimer_ <= 0.0f) {
            bossAppearanceTimer_ = -1.0f;
            camera_->ClearTarget(); // 演出終了時に確実に注視ターゲットを解除
            camera_->SetFov(0.45f); // FOVを確実に初期値に戻す
            activePostProcess_ = kNone; // ラジアルブラーを解除
        }
    }

    // ボスの足アニメーション時間更新
    if (currentPhase_ == GamePhase::kBossFight) {
        bossTime_ += kDeltaTime * bossLegSwingSpeed_;
    }

    // ボス戦時は雑魚敵をすべて非生存にする
    if (currentPhase_ == GamePhase::kBossFight) {
        for (auto& enemy : enemies_) {
            enemy.isAlive = false;
        }
    }

    randomEffectTime_ += kDeltaTime * randomSpeed_;
    if (randomParamData_) {
        randomParamData_->time = randomEffectTime_;
        randomParamData_->noiseScale = randomNoiseScale_;
        randomParamData_->noiseStrength = randomNoiseStrength_;
        randomParamData_->isColorNoise = randomIsColorNoise_ ? 1.0f : 0.0f;
        randomParamData_->isMultiplyNoise = (randomNoiseType_ == 1) ? 1.0f : 0.0f;
    }

    if (isTransitioning_) {
        transitionThreshold_ -= kDeltaTime * 0.5f; // Fade in over 2 seconds
        if (transitionThreshold_ <= 0.0f) {
            transitionThreshold_ = 0.0f;
            isTransitioning_ = false;
            activePostProcess_ = kNone;
            // 前のシーンのメモリを完全に解放
            SceneManager::GetInstance()->ClearPreviousScene();
        }
        dissolveParamData_->threshold = transitionThreshold_;
    }

    // ── プレイヤーのワールド位置の共通計算 ──
    EulerTransform& camTransForPos = camera_->GetTransform();
    Vector3 fighterWorldPos = {
        camTransForPos.translate.x + fighterModel_->transform.translate.x,
        camTransForPos.translate.y - 3.0f + fighterModel_->transform.translate.y,
        fighterWorldZ_
    };

#ifdef USE_IMGUI
    // ── ボス足攻撃のデバッグ用オーバーレイ表示 ──
    ImGui::SetNextWindowPos(ImVec2(20.0f, 20.0f), ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.55f); // 半透明背景
    ImGui::Begin("Boss Leg Debug", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_AlwaysAutoResize);
    ImGui::SetWindowFontScale(2.0f); // 文字サイズを大きく
    
    int currentAttackLeg = 5;
    
    if (bossActionState_ == BossActionState::kLegAttack) {
        ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "LEG ATTACK ACTIVE!");
        ImGui::Text("Active Leg Index: %d", currentAttackLeg);
        ImGui::Text("Timer: %.2fs", bossActionTimer_);
    } else {
        ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.3f, 1.0f), "Boss State: Normal");
        ImGui::Text("Target Leg if attack starts: %d", currentAttackLeg);
    }
    ImGui::End();

    ImGui::SetNextWindowSize(ImVec2(500, 600), ImGuiCond_FirstUseEver);
    ImGui::Begin("GamePlay Control");

    // ── ゲームフェーズ制御 ──
    ImGui::Separator();
    ImGui::Text("Game Phase Control");
    const char* phaseNames[] = { "1 Phase (Enemies)", "2 Phase (Enemies)", "Boss Fight (Big Spider)" };
    int currentPhaseInt = static_cast<int>(currentPhase_);
    if (ImGui::Combo("Game Phase", &currentPhaseInt, phaseNames, IM_ARRAYSIZE(phaseNames))) {
        currentPhase_ = static_cast<GamePhase>(currentPhaseInt);
        phaseTimer_ = 0.0f; // 切り替え時にタイマーをリセット
    }
    if (currentPhase_ != GamePhase::kBossFight) {
        ImGui::ProgressBar(phaseTimer_ / kPhaseDuration, ImVec2(0.0f, 0.0f));
        ImGui::Text("Time remaining: %.1fs", kPhaseDuration - phaseTimer_);
    } else {
        ImGui::TextColored(ImVec4(1.0f, 0.2f, 0.2f, 1.0f), "BOSS FIGHT ACTIVE");
    }

    // ── 背景切り替えとパラメータ調整 ──
    ImGui::Separator();
    ImGui::Text("SkyDome Control");
    const char* bgNames[] = { "Green.png (Green)", "Red.png (Red)" };
    ImGui::Combo("Sky Texture", &activeBackgroundTex_, bgNames, IM_ARRAYSIZE(bgNames));
    if (ImGui::Button("Switch Sky Texture (T key)")) {
        activeBackgroundTex_ = (activeBackgroundTex_ + 1) % kNumSkyTextures;
    }
    ImGui::DragFloat("Sky UV Scroll X", &bgUvScrollX_, 0.001f, 0.0f, 1.0f, "%.3f");
    ImGui::DragFloat("Sky UV Scroll Y", &bgUvScrollY_, 0.001f, 0.0f, 1.0f, "%.3f");

    // ── プレイヤーHP調整 ──
    ImGui::Separator();
    ImGui::Text("Player HP Status");
    ImGui::SliderFloat("Player HP", &playerHP_, 0.0f, playerMaxHP_, "%.1f");

    // ── 蜘蛛ボス調整項目 ──
    if (currentPhase_ == GamePhase::kBossFight) {
        ImGui::Separator();
        ImGui::DragFloat("Boss Collision Radius", &bossCollisionRadius_, 0.1f, 1.0f, 100.0f, "%.1f");
        ImGui::DragFloat("Boss Body Scale (big+Spider)", &bossBodyScale_, 0.1f, 0.01f, 100.0f, "%.2f");
        ImGui::DragFloat("Boss Leg Scale (big+spider+arm)", &bossLegScale_, 0.1f, 0.01f, 100.0f, "%.2f");
        ImGui::DragFloat("Boss Y Height", &bossYOffset_, 0.5f, -200.0f, 100.0f, "%.1f");
        ImGui::DragFloat("Boss Z Offset", &bossZOffset_, 1.0f, 10.0f, 1000.0f, "%.1f");
        ImGui::DragFloat("Boss Body RotY", &bossBodyRotY_, 1.0f, 0.0f, 360.0f, "%.1f");
        ImGui::DragFloat("Body Bounce Range", &bossBodyBounceRange_, 0.01f, 0.0f, 5.0f, "%.2f");
        ImGui::DragFloat("Body Roll Range (deg)", &bossBodyRollRange_, 0.05f, 0.0f, 15.0f, "%.1f");
        ImGui::DragFloat3("Boss Web Fire Offset (Ass)", &bossWebFireOffset_.x, 0.1f, -100.0f, 100.0f, "%.2f");
        
        ImGui::Text("--- Symmetrical Leg Pairs (X, Y, Z Offsets) ---");
        ImGui::DragFloat3("Pair 0 (Front) Offset", &bossLegPairPos0_.x, 0.05f, -20.0f, 20.0f, "%.2f");
        ImGui::DragFloat("Pair 0 (Front) RotY", &bossLegPairRotY0_, 1.0f, -180.0f, 180.0f, "%.1f");
        
        ImGui::DragFloat3("Pair 1 (Mid-Front) Offset", &bossLegPairPos1_.x, 0.05f, -20.0f, 20.0f, "%.2f");
        ImGui::DragFloat("Pair 1 (Mid-Front) RotY", &bossLegPairRotY1_, 1.0f, -180.0f, 180.0f, "%.1f");
        
        ImGui::DragFloat3("Pair 2 (Mid-Back) Offset", &bossLegPairPos2_.x, 0.05f, -20.0f, 20.0f, "%.2f");
        ImGui::DragFloat("Pair 2 (Mid-Back) RotY", &bossLegPairRotY2_, 1.0f, -180.0f, 180.0f, "%.1f");
        
        ImGui::DragFloat3("Pair 3 (Back) Offset", &bossLegPairPos3_.x, 0.05f, -20.0f, 20.0f, "%.2f");
        ImGui::DragFloat("Pair 3 (Back) RotY", &bossLegPairRotY3_, 1.0f, -180.0f, 180.0f, "%.1f");

        ImGui::Text("--- Leg Local Pivot Offset ---");
        ImGui::DragFloat("Pivot Y (Up/Down)", &bossLegPivotY_, 0.01f, -1.0f, 1.0f, "%.2f");
        ImGui::DragFloat("Pivot Z (Front/Back)", &bossLegPivotZ_, 0.01f, -1.0f, 1.0f, "%.2f");

        ImGui::Text("--- Walk Animation Motion ---");
        ImGui::DragFloat("Walk Speed", &bossLegSwingSpeed_, 0.1f, 0.0f, 20.0f, "%.1f");
        ImGui::DragFloat("Walk Swing Range", &bossLegSwingRange_, 0.01f, 0.0f, 2.0f, "%.2f");
        ImGui::DragFloat("Walk Step Lift Range", &bossLegLiftRange_, 0.05f, 0.0f, 10.0f, "%.2f");
    }

    ImGui::Separator();
    ImGui::Checkbox("Show SimpleSkin", &showSimpleSkin_);
    ImGui::Checkbox("Show AnimatedCube", &showAnimatedCube_);
    ImGui::Checkbox("Show Particles", &showParticles_);
    ImGui::Checkbox("Show Skybox", &showSkybox_);
    if (showSkybox_) {
        const char* skyboxItems[] = {
            "Moonless Golf (Night)",
            "Qwantani Night Puresky"
        };
        ImGui::Combo("Skybox Type", &skyboxType_, skyboxItems, IM_ARRAYSIZE(skyboxItems));
    }
    ImGui::Checkbox("Show Enemies", &showEnemies_);
    ImGui::DragFloat2("Sprite Position", &spritePos_.x, 1.0f, -2000.0f, 2000.0f, "%.1f");
    if (ImGui::SliderFloat("Model Reflection", &modelEnvCoefficient_, 0.0f, 1.0f)) {
        if (playerModel_) playerModel_->SetEnvironmentCoefficient(modelEnvCoefficient_);
    }


    if (fighterModel_) {
        if (ImGui::DragFloat("Fighter Model Scale", &fighterModel_->transform.scale.x, 0.1f, 0.1f, 100.0f, "%.1f")) {
            fighterModel_->transform.scale.y = fighterModel_->transform.scale.x;
            fighterModel_->transform.scale.z = fighterModel_->transform.scale.x;
        }
    }
    
    ImGui::Text("F1 Key: Toggle Debug Camera (Free Camera)");
    if (sceneMode_ == SceneMode::kCamera) {
        ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), "  [WASDQE] Move Camera (LShift: Turbo)");
        ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), "  [ZC] Rotate Camera Left/Right (Yaw)");
        ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), "  [Right-Click Drag] Look Around");
        ImGui::TextColored(ImVec4(0.5f, 1.0f, 0.5f, 1.0f), "  (Mouse cursor is active for ImGui!)");
    }
    ImGui::Text("TAB Key: Switch Scene Mode");
    ImGui::Text("Current Mode: %s", (sceneMode_ == SceneMode::kMouse ? "Mouse" : (sceneMode_ == SceneMode::kCamera ? "Camera" : "Fighter")));

    // ── Blender連携パネル ──
    ImGui::Separator();
    ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.6f, 1.0f), "[Blender Sync]");
    ImGui::Text("game_state.json: Updating every 3 frames");
    if (replayRecording_) {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.8f, 0.1f, 0.1f, 1.0f));
        if (ImGui::Button("[REC] Stop Replay (R key)")) {
            replayRecording_ = false;
        }
        ImGui::PopStyleColor();
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "Frame: %d", replayFrameCounter_);
    } else {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.1f, 0.6f, 0.2f, 1.0f));
        if (ImGui::Button("[REC] Start Replay (R key)")) {
            replayRecording_ = true;
            replayFrameCounter_ = 0;
            replaySessionIndex_++;
            std::string filename = "Resources/replay_frames_" + std::to_string(replaySessionIndex_) + ".csv";
            std::ofstream repFileHdr(filename, std::ios::trunc);
            if (repFileHdr.is_open()) {
                repFileHdr << "frame,px,py,pz,proll,ppitch";
                for (int i = 0; i < kMaxEnemies; ++i) {
                    repFileHdr << ",e" << i << "_alive,e" << i << "_x,e" << i << "_y,e" << i << "_z,e" << i << "_rz";
                }
                for (int i = 0; i < 20; ++i) {
                    repFileHdr << ",b" << i << "_alive,b" << i << "_x,b" << i << "_y,b" << i << "_z";
                }
                repFileHdr << ",boss_active,boss_x,boss_y,boss_z,boss_ry\n";
                repFileHdr.close();
            }
        }
        ImGui::PopStyleColor();
    }
    ImGui::Text("Blender RT : blender_realtime_viewer.py");
    ImGui::Text("Replay     : blender_replay_viewer.py");
    ImGui::Text("LevelEditor: blender_level_editor.py");
    ImGui::Text("ParamEdit  : blender_param_editor.py");
    ImGui::End();
#endif

    // マウスホイールによる環境マップ反射強度の調整
    int32_t wheel = input_->GetWheelDelta();
    if (wheel != 0) {
        modelEnvCoefficient_ += static_cast<float>(wheel) / 12000.0f; // 感度調整 (120ごとに0.01動く程度)
        modelEnvCoefficient_ = std::clamp(modelEnvCoefficient_, 0.0f, 1.0f);
        if (playerModel_) playerModel_->SetEnvironmentCoefficient(modelEnvCoefficient_);
    }

    if (input_->IsKeyPressed(DIK_1)) currentEffect_ = kTypeExplosion;
    if (input_->IsKeyPressed(DIK_2)) currentEffect_ = kTypeFountain;
    if (input_->IsKeyPressed(DIK_3)) currentEffect_ = kTypeSpiral;
    if (input_->IsKeyPressed(DIK_4)) currentEffect_ = kTypeRain;
    
    // キー5を押した瞬間のみ、ヒットエフェクトとリングエフェクトを発生させる
    if (input_->IsKeyTriggered(DIK_5)) {
        particleManager_->EmitHit(emitterPos_);
        particleManager_->EmitRing(emitterPos_);
    }

    if (input_->IsKeyTriggered(DIK_6)) {
        particleManager_->EmitCylinder(emitterPos_);
    }

    if (input_->IsKeyTriggered(DIK_G)) useGravity_ = !useGravity_;

    // SPACEキーを押した瞬間にSEを再生（戦闘機モード以外のみ。戦闘機モードはブーストSE対応予定）
    if (input_->IsKeyTriggered(DIK_SPACE) && sceneMode_ != SceneMode::kFighter) {
        audio_->PlayWave(jumpSE_, false, 1.0f);
    }
    EulerTransform& camTrans = camera_->GetTransform();
    HWND hwnd = WinApp::GetInstance()->GetHwnd();

    // F1キーでデバッグカメラ（第三者視点フリーカメラ）を即時切り替え
    if (input_->IsKeyTriggered(DIK_F1)) {
        if (sceneMode_ == SceneMode::kCamera) {
            // 現在デバッグカメラ（Camera）モードなら、元のモードに戻す
            sceneMode_ = preDebugSceneMode_;

            // 元の操作に戻る際は、ImGuiなどを快適に操作できるよう必ずマウスカーソルを確実に再表示する
            // ShowCursorの内部カウンタが負に累積して消えたままになるのを防ぐため、表示(>=0)されるまで呼び出す
            while (ShowCursor(TRUE) < 0);

            // ── 元の操作に戻る際、カメラの回転・位置を一瞬で戦闘機背後に完全リセット！ ──
            if (sceneMode_ == SceneMode::kFighter) {
                // 1. 回転を正面(0,0,0)にリセット（デバッグ時の首振り角を引き継がない）
                camTrans.rotate = { 0.0f, 0.0f, 0.0f };

                // 2. 位置（X, Y, Z）を戦闘機追従のジャストな初期背後位置に一瞬でワープ
                float targetCamX = 0.0f;
                float targetCamY = 0.0f;
                if (fighterModel_) {
                    targetCamX = fighterModel_->transform.translate.x * 0.5f;
                    targetCamY = fighterModel_->transform.translate.y * 0.5f;
                }
                camTrans.translate.x = targetCamX;
                camTrans.translate.y = targetCamY;
                camTrans.translate.z = fighterWorldZ_ - 65.0f;
            }
        } else {
            // それ以外なら、現在のモードを保存してデバッグカメラ（Camera）に切り替える
            preDebugSceneMode_ = sceneMode_;
            sceneMode_ = SceneMode::kCamera;

            // デバッグカメラに入る際は最初はImGuiをいじるため、マウスカーソルを確実に表示状態にする
            while (ShowCursor(TRUE) < 0);

            // カメラの回転を正面にリセット（切り替え直後に迷子になるのを防止）
            camTrans.rotate = { 0.0f, 0.0f, 0.0f };
        }
    }

    // TABキーでモード切替
    if (input_->IsKeyTriggered(DIK_TAB)) {
        if (sceneMode_ == SceneMode::kMouse) {
            sceneMode_ = SceneMode::kCamera;
            ShowCursor(FALSE);
        } else if (sceneMode_ == SceneMode::kCamera) {
            sceneMode_ = SceneMode::kFighter;
            ShowCursor(FALSE);
        } else {
            sceneMode_ = SceneMode::kMouse;
            ShowCursor(TRUE);
        }
    }

    // Bキーでボスシーン（kBossFightフェーズ）へ即座にスキップ
    if (input_->IsKeyTriggered(DIK_B)) {
        if (currentPhase_ != GamePhase::kBossFight) {
            currentPhase_ = GamePhase::kBossFight;
            bossAppearanceTimer_ = 5.0f; // ボス登場演出（落下）開始（5.0秒）
            phaseTimer_ = 0.0f;          // 通常フェーズのタイマーをリセット
            bossZOffset_ = 169.0f;       // ボスZオフセットを初期位置にリセット
            bossTime_ = 0.0f;            // ボスタイマーのリセット
            isBossLanded_ = false;       // 着地フラグのリセット
            isBossRoared_ = false;       // 咆哮フラグのリセット
            bossActionState_ = BossActionState::kIdle; // アクションステートのリセット
            bossActionTimer_ = 0.0f;     // アクションタイマーのリセット
        }
    }

    if (sceneMode_ == SceneMode::kCamera) {
        // 右クリックが押されている間（ドラッグ中）だけカメラを回転させる（カーソルロック）
        if (input_->IsMousePressed(1)) {
            // カメラ回転中はマウスカーソルを確実に非表示にする
            while (ShowCursor(FALSE) >= 0);

            RECT rect;
            GetWindowRect(hwnd, &rect);
            int centerX = rect.left + (rect.right - rect.left) / 2;
            int centerY = rect.top + (rect.bottom - rect.top) / 2;
            POINT currentPos;
            GetCursorPos(&currentPos);

            float deltaX = static_cast<float>(currentPos.x - centerX);
            float deltaY = static_cast<float>(currentPos.y - centerY);

            SetCursorPos(centerX, centerY);

            camTrans.rotate.y += deltaX * mouseSensitivity_;
            camTrans.rotate.x += deltaY * mouseSensitivity_;

            const float pitchLimit = static_cast<float>(M_PI / 2.0 - 0.01);
            camTrans.rotate.x = std::clamp(camTrans.rotate.x, -pitchLimit, pitchLimit);
        } else {
            // 右クリックを離している間は、ImGuiの操作ができるようにマウスカーソルを確実に表示する
            while (ShowCursor(TRUE) < 0);
        }
        
        // Z / C キーによるカメラの左右旋回（横回転）
        float keyboardRotSpeed = 1.5f; // 秒間の回転速度(ラジアン)
        if (input_->IsKeyPressed(DIK_Z)) {
            camTrans.rotate.y -= keyboardRotSpeed * kDeltaTime;
        }
        if (input_->IsKeyPressed(DIK_C)) {
            camTrans.rotate.y += keyboardRotSpeed * kDeltaTime;
        }
        
        Vector3 moveDir = { 0.0f, 0.0f, 0.0f };
        if (input_->IsKeyPressed(DIK_W)) moveDir.z += 1.0f;
        if (input_->IsKeyPressed(DIK_S)) moveDir.z -= 1.0f;
        if (input_->IsKeyPressed(DIK_D)) moveDir.x += 1.0f;
        if (input_->IsKeyPressed(DIK_A)) moveDir.x -= 1.0f;
        if (input_->IsKeyPressed(DIK_E)) moveDir.y += 1.0f;
        if (input_->IsKeyPressed(DIK_Q)) moveDir.y -= 1.0f;
        
        if (moveDir.x != 0.0f || moveDir.y != 0.0f || moveDir.z != 0.0f) {
            // フリーカメラの基本移動速度を大幅に向上（元の 5.0f は遅すぎたため 80.0f に設定）
            // さらに左Shiftまたは右Shiftを押している間は、高速ターボ移動（300.0f）できるように拡張
            float cameraSpeed = 80.0f;
            if (input_->IsKeyPressed(DIK_LSHIFT) || input_->IsKeyPressed(DIK_RSHIFT)) {
                cameraSpeed = 300.0f;
            }
            Matrix4x4 cameraRotY = MakeRotateYMatrix(camTrans.rotate.y);
            Vector3 rotatedMoveDir = TransformNormal(moveDir, cameraRotY);
            rotatedMoveDir = Normalize(rotatedMoveDir);
            rotatedMoveDir = Scale(rotatedMoveDir, cameraSpeed * kDeltaTime);
            camTrans.translate = Add(camTrans.translate, rotatedMoveDir);
        }
    } else if (sceneMode_ == SceneMode::kFighter) {
        // --- 戦闘機（レールシューター）モード ---

        // ── ブースト処理（LSHIFTで発動、バレルロール終了と同時に自動で戻る） ────────────────
        if (input_->IsKeyTriggered(DIK_LSHIFT) && !(phaseIntroTimer_ >= 0.0f) && !isBossDefeatedSequence_) {
            if (!isBarrelRolling_) {
                isBoosting_ = true;
                isBarrelRolling_ = true;
                barrelRollTimer_ = 0.0f;
            }
        }

        if (isBoosting_) {
            boostBlurWidth_ += kBoostBlurFadeIn * kDeltaTime * kBoostBlurMax;
            boostBlurWidth_ = (std::min)(boostBlurWidth_, kBoostBlurMax);
            boostForwardSpeed_ += (kBoostSpeedMax - boostForwardSpeed_) * 0.05f;
        } else {
            boostBlurWidth_ -= kBoostBlurFadeOut * kDeltaTime * kBoostBlurMax;
            boostBlurWidth_ = (std::max)(boostBlurWidth_, 0.0f);
            boostForwardSpeed_ += (kNormalSpeed - boostForwardSpeed_) * 0.05f;
        }

        // RadialBlur の適用制御
        if (boostBlurWidth_ > 0.0001f) {
            if (!isTransitioning_) activePostProcess_ = kRadialBlur;
            if (radialBlurParamData_) {
                radialBlurParamData_->center = { 0.5f, 0.5f };
                radialBlurParamData_->blurWidth = -boostBlurWidth_;
            }
        } else {
            if (activePostProcess_ == kRadialBlur && !isTransitioning_) {
                activePostProcess_ = kNone;
            }
        }

        // ── 1. プレイヤーのワールドZ座標を自律前進 ──────────────────
        if (!(phaseIntroTimer_ >= 0.0f)) {
            fighterWorldZ_ += boostForwardSpeed_ * kDeltaTime;
        }

        // ── 2. カメラは等速追従（直接代入）をベースにしつつ、ブースト時はG遅延（最大120m）を適用 ──
        float currentDist = 65.0f;
        if (isBoosting_) {
            // バレルロール進行度を正弦波（0〜1〜0）にマップしてカメラを後方に引き離す
            float t = barrelRollTimer_ / kBarrelRollDuration;
            float wave = std::sin(t * static_cast<float>(M_PI));

            // ブースト時カメラ引き離しG演出（通常65m ➔ 最大120m引き離す）
            currentDist = 65.0f + wave * 55.0f;
        }

        float camProgress = fighterWorldZ_ - currentDist;
        Vector3 camRailPos = GetRailPosition(camProgress);
        Vector3 camRailDir = GetRailDirection(camProgress);
        Vector3 camRailRight = CalculateRailRight(camRailDir);
        Vector3 camRailUp = CalculateRailUp(camRailDir, camRailRight);

        // ── 3. 自機の横/縦移動（画面内相対）─────────────────────────
        if (fighterModel_) {
            Vector3 inputDir = {0,0,0};
            if (!(phaseIntroTimer_ >= 0.0f) && !isBossDefeatedSequence_) {
                if (input_->IsKeyPressed(DIK_W)) inputDir.y += 1.0f;
                if (input_->IsKeyPressed(DIK_S)) inputDir.y -= 1.0f;
                if (input_->IsKeyPressed(DIK_A)) inputDir.x -= 1.0f;
                if (input_->IsKeyPressed(DIK_D)) inputDir.x += 1.0f;
            }

            if (inputDir.x != 0 || inputDir.y != 0) {
                inputDir = Normalize(inputDir);
                float speedFactor = (playerSpeedDebuffTimer_ > 0.0f) ? 0.5f : 1.0f;
                
                // 横移動：傾き（ロール）に応じて移動量を決定し、動き出しをワンテンポ遅らせる
                float rollFactor = 0.0f;
                if (std::abs(playerRotationRoll_) > 0.001f) {
                    rollFactor = -playerRotationRoll_ / kMaxRollAngle;
                }
                rollFactor = std::clamp(rollFactor, -1.0f, 1.0f);
                
                fighterModel_->transform.translate.x += rollFactor * playerSpeedX_ * speedFactor * kDeltaTime;
                fighterModel_->transform.translate.y += inputDir.y * playerSpeedY_ * speedFactor * kDeltaTime;
            }

            fighterModel_->transform.translate.x = std::clamp(fighterModel_->transform.translate.x, -playerLimitX_, playerLimitX_);
            fighterModel_->transform.translate.y = std::clamp(fighterModel_->transform.translate.y, -playerLimitY_, playerLimitY_);

            // ── プレイヤーのワールド座標（Z はfighterWorldZ_を直接使う）──
            Vector3 playerRailPos = GetRailPosition(fighterWorldZ_);
            Vector3 playerRailDir = GetRailDirection(fighterWorldZ_);
            Vector3 playerRailRight = CalculateRailRight(playerRailDir);
            Vector3 playerRailUp = CalculateRailUp(playerRailDir, playerRailRight);

            // 基準高さは路面 -20.0f からプレイヤーは通常 -3.0f (つまり相対 +17.0f)
            float playerOffsetUp = fighterModel_->transform.translate.y + 17.0f;
            Vector3 fighterWorldPos = Add(playerRailPos, Add(Scale(playerRailRight, fighterModel_->transform.translate.x), Scale(playerRailUp, playerOffsetUp)));

            if (!isBossDefeatedSequence_) {
                // スターフォックス風のカメラX/Y追従
                float cameraLag = 0.08f;
                float targetCamX = fighterModel_->transform.translate.x * 0.5f;
                float targetCamY = fighterModel_->transform.translate.y * 0.5f;
                camRelativeX_ = std::lerp(camRelativeX_, targetCamX, cameraLag);
                camRelativeY_ = std::lerp(camRelativeY_, targetCamY, cameraLag);

                // カメラの目標位置を計算（プレイヤーモデルの真後ろから追従）
                // 基準高さは路面 -20.0f から通常 +6.0f (つまり相対 +26.0f)
                float camOffsetUp = camRelativeY_ + 26.0f;
                Vector3 camBackVector = Scale(playerRailDir, -currentDist);
                Vector3 targetCamPos = Add(playerRailPos, Add(camBackVector, Add(Scale(playerRailRight, camRelativeX_), Scale(playerRailUp, camOffsetUp))));

                // カメラ位置のスムーズ追従 (Smooth Follow)
                float posLerpFactor = 0.12f;
                Vector3 camDiff = Subtract(targetCamPos, camTrans.translate);
                float camDistSq = camDiff.x * camDiff.x + camDiff.y * camDiff.y + camDiff.z * camDiff.z;
                if (camDistSq > 10000.0f) { // 100m以上離れている場合は即座にワープ
                    camTrans.translate = targetCamPos;
                } else {
                    camTrans.translate = Lerp(camTrans.translate, targetCamPos, posLerpFactor);
                }

                // ── カメラの注視ターゲット（LookAhead & LookAt）の計算 ──
                // プレイヤーの少し先（20.0m 先）を注視点とする
                float lookAheadDist = 20.0f;
                Vector3 lookAheadTarget = Add(fighterWorldPos, Scale(playerRailDir, lookAheadDist));
                // プレイヤーの少し上（5.0m 上）を狙う
                Vector3 cameraTarget = Add(lookAheadTarget, Scale(playerRailUp, 5.0f));

                // カメラの現在位置から注視ターゲットへの方向ベクトル
                Vector3 lookDir = Subtract(cameraTarget, camTrans.translate);
                float lookDist = std::sqrt(lookDir.x * lookDir.x + lookDir.y * lookDir.y + lookDir.z * lookDir.z);
                if (lookDist > 0.001f) {
                    lookDir = Scale(lookDir, 1.0f / lookDist);
                } else {
                    lookDir = camRailDir; // フォールバック
                }

                // 方向ベクトルからオイラー角を計算
                float baseCamRotY = -std::atan2(lookDir.x, lookDir.z);
                float baseCamRotX = std::atan2(-lookDir.y, std::sqrt(lookDir.x * lookDir.x + lookDir.z * lookDir.z));

                float targetCamRotateY = fighterModel_->transform.translate.x * 0.003f;
                float targetCamRotateX = -fighterModel_->transform.translate.y * 0.003f;

                float targetRotY = baseCamRotY + targetCamRotateY;
                float targetRotX = baseCamRotX + targetCamRotateX;

                // カメラの回転角のスムーズ首振り（最短角度差による補間）
                float rotLerpFactor = 0.15f;

                float diffY = targetRotY - camTrans.rotate.y;
                while (diffY < -static_cast<float>(M_PI)) diffY += 2.0f * static_cast<float>(M_PI);
                while (diffY > static_cast<float>(M_PI)) diffY -= 2.0f * static_cast<float>(M_PI);
                camTrans.rotate.y += diffY * rotLerpFactor;

                float diffX = targetRotX - camTrans.rotate.x;
                while (diffX < -static_cast<float>(M_PI)) diffX += 2.0f * static_cast<float>(M_PI);
                while (diffX > static_cast<float>(M_PI)) diffX -= 2.0f * static_cast<float>(M_PI);
                camTrans.rotate.x += diffX * rotLerpFactor;

                // ロール回転は入力とバレルロールに追従
                float targetRotZ = -inputDir.x * 0.03f;
                if (isBarrelRolling_) {
                    float rollT = barrelRollTimer_ / kBarrelRollDuration;
                    targetRotZ += std::sin(rollT * static_cast<float>(M_PI)) * 0.15f;
                }
                camTrans.rotate.z = std::lerp(camTrans.rotate.z, targetRotZ, rotLerpFactor);
            }

            // ── 4. バレルロール開始（LSHIFTのブースト切り替え時に連動） ──

            // ── 5. 機体の傾き（通常ロール/ピッチ + バレルロール合成） ──
            float baseRoll  = inputDir.x * -kMaxRollAngle;
            float basePitch = inputDir.y * 0.4f;
            playerRotationRoll_  = std::lerp(playerRotationRoll_,  baseRoll,  0.1f);
            playerRotationPitch_ = std::lerp(playerRotationPitch_, basePitch, 0.1f);

            float rollAngle = playerRotationRoll_;

            if (isBarrelRolling_) {
                if (!(phaseIntroTimer_ >= 0.0f)) {
                    barrelRollTimer_ += kDeltaTime;
                }
                float t = barrelRollTimer_ / kBarrelRollDuration; // 0.0 → 1.0
                if (t >= 1.0f) {
                    t = 1.0f;
                    isBarrelRolling_ = false;
                    barrelRollTimer_ = 0.0f;
                    isBoosting_ = false; // バレルロール終了と同時にブーストも自動解除
                }
                // easeInOut: 0.5 - 0.5 * cos(π*t) で0から1へスムーズに変化
                float easedT = 0.5f - 0.5f * std::cos(static_cast<float>(M_PI) * t);
                rollAngle += easedT * 2.0f * static_cast<float>(M_PI); // ← ベース回転に上乗せ（360度一回転）
            }

            fighterModel_->transform.rotate.z = rollAngle;

            // 機体自体の向きをレールの進行方向に滑らかに合わせる
            float basePlayerRotY = std::atan2(playerRailDir.x, playerRailDir.z);
            float basePlayerRotX = std::atan2(-playerRailDir.y, std::sqrt(playerRailDir.x * playerRailDir.x + playerRailDir.z * playerRailDir.z));

            // 進行方向に向かせるための回転角（符号を反転して180度オフセット）
            float targetPlayerRotY = -basePlayerRotY + static_cast<float>(M_PI);
            float diffPlayerY = targetPlayerRotY - fighterModel_->transform.rotate.y;
            while (diffPlayerY < -static_cast<float>(M_PI)) diffPlayerY += 2.0f * static_cast<float>(M_PI);
            while (diffPlayerY > static_cast<float>(M_PI)) diffPlayerY -= 2.0f * static_cast<float>(M_PI);
            fighterModel_->transform.rotate.y += diffPlayerY * 0.15f;

            // ピッチはレールの傾きを除いた入力分のみ
            float targetPlayerRotX = playerRotationPitch_;
            float diffPlayerX = targetPlayerRotX - fighterModel_->transform.rotate.x;
            while (diffPlayerX < -static_cast<float>(M_PI)) diffPlayerX += 2.0f * static_cast<float>(M_PI);
            while (diffPlayerX > static_cast<float>(M_PI)) diffPlayerX -= 2.0f * static_cast<float>(M_PI);
            fighterModel_->transform.rotate.x += diffPlayerX * 0.15f;

            // レールに沿った回転行列を構築 (r = playerRailRight, u = playerRailUp, d = playerRailDir)
            Matrix4x4 R_rail = MakeIdentity4x4();
            R_rail.m[0][0] = playerRailRight.x; R_rail.m[0][1] = playerRailRight.y; R_rail.m[0][2] = playerRailRight.z;
            R_rail.m[1][0] = playerRailUp.x;    R_rail.m[1][1] = playerRailUp.y;    R_rail.m[1][2] = playerRailUp.z;
            R_rail.m[2][0] = playerRailDir.x;   R_rail.m[2][1] = playerRailDir.y;   R_rail.m[2][2] = playerRailDir.z;

            // モデルのローカル回転 (Z:ロール -> X:ピッチ -> Y:180度反転)
            Matrix4x4 rotateY = MakeRotateYMatrix(static_cast<float>(M_PI));
            Matrix4x4 rotateX = MakeRotateXMatrix(playerRotationPitch_);
            Matrix4x4 rotateZ = MakeRotateZMatrix(rollAngle);
            Matrix4x4 R_local = Multiply(Multiply(rotateZ, rotateX), rotateY);

            // 最終姿勢行列の算出とワールド行列の組み立て
            Matrix4x4 R_final = Multiply(R_local, R_rail);
            Matrix4x4 scaleMatrix = MakeScaleMatrix(fighterModel_->transform.scale);
            Matrix4x4 translateMatrix = MakeTranslateMatrix(fighterWorldPos);
            fighterTransformData_->World = Multiply(Multiply(scaleMatrix, R_final), translateMatrix);


            // ── ボス登場演出中のカメラワーク上書き ──
            if (!isBossDefeatedSequence_ && currentPhase_ == GamePhase::kBossFight && bossAppearanceTimer_ > 0.0f) {
                float t = 5.0f - bossAppearanceTimer_; // 0.0f から 5.0f
                
                // ボスの落下オフセット（1.8秒で着地）
                float dropOffset = 0.0f;
                if (t < 1.8f) {
                    float appRate = t / 1.8f;
                    dropOffset = 120.0f * (1.0f - appRate) * (1.0f - appRate);
                }
                Vector3 bossDropPos = GetBossPosition(0.0f, dropOffset);

                // カメラ演出用のレール姿勢の事前計算
                Vector3 playerRailPos = GetRailPosition(fighterWorldZ_);
                Vector3 playerRailDir = GetRailDirection(fighterWorldZ_);
                Vector3 playerRailRight = CalculateRailRight(playerRailDir);
                Vector3 playerRailUp = CalculateRailUp(playerRailDir, playerRailRight);

                Vector3 camPos{};
                Vector3 camRot{};

                // 通常のカメラ開始位置・目標位置
                float targetCamX = fighterModel_ ? fighterModel_->transform.translate.x * 0.5f : 0.0f;
                float targetCamY = fighterModel_ ? fighterModel_->transform.translate.y * 0.5f : 0.0f;
                float targetY = fighterModel_ ? fighterModel_->transform.translate.y * 0.5f : 0.0f;

                float dist = 140.0f; // 旋回半径

                if (t < 1.8f) {
                    // --- フェーズ1: 落下見上げ (t: 0.0s ～ 1.8s) ---
                    float t_phase1 = t / 1.8f;
                    float easedT = t_phase1 * t_phase1 * (3.0f - 2.0f * t_phase1); // Smoothstep

                    // 開始位置（戦闘機背後、レール追従）
                    Vector3 startPos = Add(playerRailPos, Add(Scale(playerRailRight, targetCamX), Add(Scale(playerRailUp, targetCamY + 26.0f), Scale(playerRailDir, -65.0f))));
                    // 目標位置（地面近く、レール追従）
                    Vector3 targetPos = Add(playerRailPos, Add(Scale(playerRailUp, 4.0f), Scale(playerRailDir, -15.0f)));

                    camPos = {
                        std::lerp(startPos.x, targetPos.x, easedT),
                        std::lerp(startPos.y, targetPos.y, easedT),
                        std::lerp(startPos.z, targetPos.z, easedT)
                    };

                    camera_->SetTarget(&bossDropPos); // ボスをロックオン
                }
                else if (t < 4.0f) {
                    // --- フェーズ2: ボスを中心とした上空からの360度公転旋回 (t: 1.8s ～ 4.0s) ---
                    float t_spin = (t - 1.8f) / 2.2f;
                    float easedT = t_spin * t_spin * (3.0f - 2.0f * t_spin); // Smoothstep

                    // 旋回角度（ヨー）: 時計回りに360度公転 (2*PI から 0.0 へ)
                    float yawAngle = (1.0f - easedT) * 2.0f * static_cast<float>(M_PI);

                    // Y高さ: ボスの上空 (+30.0f) から、らせんの終端高さ (+45.0f) へ徐々に上昇させる
                    float camY = bossDropPos.y + 30.0f + (15.0f * easedT);

                    // レールのローカル軸を取得して、らせん公転位置をレール系で計算
                    float bossProgress = fighterWorldZ_ + bossZOffset_;
                    Vector3 bossRailDir = GetRailDirection(bossProgress);
                    Vector3 bossRailRight = Normalize(Vector3{ bossRailDir.z, 0.0f, -bossRailDir.x });
                    Vector3 bossRailUp = Cross(bossRailDir, bossRailRight);

                    // カメラの本来の公転位置
                    Vector3 orbitPos = Add(bossDropPos, Add(
                        Scale(bossRailRight, -std::sin(yawAngle) * dist),
                        Add(Scale(bossRailUp, 30.0f + (15.0f * easedT)), Scale(bossRailDir, -std::cos(yawAngle) * dist))
                    ));

                    // 1.8s切り替え時のワープを防ぐため、フェーズ1の終了位置からのスムーズなブレンド接続 (最初の0.8秒)
                    float blendDuration = 0.8f;
                    float elapsedInPhase2 = t - 1.8f;
                    if (elapsedInPhase2 < blendDuration) {
                        float blendRate = elapsedInPhase2 / blendDuration;
                        float easedBlend = blendRate * blendRate * (3.0f - 2.0f * blendRate); // Smoothstep

                        // フェーズ1の終了位置（地面近く、レール追従）
                        Vector3 lastPos = Add(playerRailPos, Add(Scale(playerRailUp, 4.0f), Scale(playerRailDir, -15.0f)));
                        camPos = {
                            std::lerp(lastPos.x, orbitPos.x, easedBlend),
                            std::lerp(lastPos.y, orbitPos.y, easedBlend),
                            std::lerp(lastPos.z, orbitPos.z, easedBlend)
                        };
                    } else {
                        camPos = orbitPos;
                    }

                    camera_->SetTarget(&bossDropPos); // ボスをロックオン
                }
                else {
                    // --- フェーズ3: 旋回完了位置から通常アングルへのスムーズな復帰 + 咆哮微振動 (t: 4.0s ～ 5.0s) ---
                    camera_->ClearTarget(); // ターゲットを解除して通常アングルの首振り角度（オイラー角）補間に戻す

                    float t_phase3 = (t - 4.0f) / 1.0f;
                    float easedT = t_phase3 * t_phase3 * (3.0f - 2.0f * t_phase3); // Smoothstep

                    // 4.0s時点のボス位置（着地状態のでdropOffset=0）
                    Vector3 bossDropPos35 = GetBossPosition(0.0f);
                    
                    float bossProgress = fighterWorldZ_ + bossZOffset_;
                    Vector3 bRailDir = GetRailDirection(bossProgress);
                    Vector3 bRailRight = CalculateRailRight(bRailDir);
                    Vector3 bRailUp = CalculateRailUp(bRailDir, bRailRight);

                    // 旋回完了時の位置（ボスの後方上空、らせんの終端高さ 45.0f に合わせる、レール追従）
                    Vector3 startPos = Add(bossDropPos35, Add(Scale(bRailDir, -dist), Scale(bRailUp, 45.0f)));
                    // 通常位置（戦闘機背後、レール追従）
                    Vector3 targetPos = Add(playerRailPos, Add(Scale(playerRailRight, targetCamX), Add(Scale(playerRailUp, targetY + 26.0f), Scale(playerRailDir, -65.0f))));

                    camPos = {
                        std::lerp(startPos.x, targetPos.x, easedT),
                        std::lerp(startPos.y, targetPos.y, easedT),
                        std::lerp(startPos.z, targetPos.z, easedT)
                    };

                    // 咆哮時の激しいカメラ微振動（最初の0.5秒ほどが最大で徐々に収まる）
                    float shakeIntensity = 2.5f * (1.0f - t_phase3);
                    std::uniform_real_distribution<float> distShake(-1.0f, 1.0f);
                    camPos.x += distShake(randomEngine_) * shakeIntensity;
                    camPos.y += distShake(randomEngine_) * shakeIntensity;
                    camPos.z += distShake(randomEngine_) * shakeIntensity;

                    // 角度計算：ボスのLookAtから通常カメラの首振り角度へ補間
                    Vector3 dir = Subtract(bossDropPos, camPos);
                    Vector3 dirNorm = Normalize(dir);
                    float lookRotY = -std::atan2(dirNorm.x, dirNorm.z); // 符号をマイナスに修正
                    float lookRotX = std::atan2(-dirNorm.y, std::sqrt(dirNorm.x * dirNorm.x + dirNorm.z * dirNorm.z));

                    // レール進行方向のベース角度
                    float baseCamRotY = -std::atan2(playerRailDir.x, playerRailDir.z); // 符号をマイナスに修正
                    float baseCamRotX = std::atan2(-playerRailDir.y, std::sqrt(playerRailDir.x * playerRailDir.x + playerRailDir.z * playerRailDir.z));

                    float targetCamRotateY = baseCamRotY + (fighterModel_ ? fighterModel_->transform.translate.x * 0.003f : 0.0f);
                    float targetCamRotateX = baseCamRotX + (fighterModel_ ? -fighterModel_->transform.translate.y * 0.003f : 0.0f);

                    float blendT = easedT * easedT; // 最後に向けて急激に通常カメラ角度に戻す

                    // ヨー角 (Y) の差分を [-PI, PI] にクランプして最短経路で補間
                    float diffRotY = targetCamRotateY - lookRotY;
                    while (diffRotY < -static_cast<float>(M_PI)) diffRotY += 2.0f * static_cast<float>(M_PI);
                    while (diffRotY > static_cast<float>(M_PI)) diffRotY -= 2.0f * static_cast<float>(M_PI);
                    camRot.y = lookRotY + diffRotY * blendT;

                    // ピッチ角 (X) の差分を [-PI, PI] にクランプして最短経路で補間
                    float diffRotX = targetCamRotateX - lookRotX;
                    while (diffRotX < -static_cast<float>(M_PI)) diffRotX += 2.0f * static_cast<float>(M_PI);
                    while (diffRotX > static_cast<float>(M_PI)) diffRotX -= 2.0f * static_cast<float>(M_PI);
                    camRot.x = lookRotX + diffRotX * blendT;

                    camRot.z = 0.0f;
                }

                // カメラに適用
                camTrans.translate = camPos;
                if (t >= 4.0f) {
                    camTrans.rotate = camRot;
                }
            }

            // ── 背景テクスチャの切り替え（Tキー）──
            if (input_->IsKeyTriggered(DIK_T)) {
                activeBackgroundTex_ = (activeBackgroundTex_ + 1) % kNumSkyTextures;
            }



            // ── Blender同期: game_state.jsonの出力（3フレームに1回）──
            if (++blenderSyncCounter_ % 3 == 0) {
                std::ostringstream oss;
                oss << "{\n";
                // フェーズ情報
                oss << "  \"phase\": " << static_cast<int>(currentPhase_) << ",\n";
                // プレイヤー情報
                oss << "  \"player\": {";
                oss << "\"x\": " << fighterWorldPos.x << ", ";
                oss << "\"y\": " << fighterWorldPos.y << ", ";
                oss << "\"z\": " << fighterWorldPos.z << ", ";
                oss << "\"roll\": " << (fighterModel_ ? fighterModel_->transform.rotate.z : 0.0f) << ", ";
                oss << "\"pitch\": " << (fighterModel_ ? fighterModel_->transform.rotate.x : 0.0f) << ", ";
                oss << "\"hp\": " << playerHP_ << ", ";
                oss << "\"maxhp\": " << playerMaxHP_ << ", ";
                oss << "\"boosting\": " << (isBoosting_ ? "true" : "false");
                oss << "},\n";
                // 敵情報 (10体分)
                oss << "  \"enemies\": [\n";
                for (int i = 0; i < kMaxEnemies; ++i) {
                    oss << "    {";
                    oss << "\"alive\": " << (enemies_[i].isAlive ? "true" : "false") << ", ";
                    oss << "\"x\": " << enemies_[i].position.x << ", ";
                    oss << "\"y\": " << enemies_[i].position.y << ", ";
                    oss << "\"z\": " << enemies_[i].position.z << ", ";
                    oss << "\"rx\": " << enemies_[i].rotate.x << ", ";
                    oss << "\"ry\": " << enemies_[i].rotate.y << ", ";
                    oss << "\"rz\": " << enemies_[i].rotate.z << ", ";
                    oss << "\"hp\": " << enemies_[i].hp << ", ";
                    oss << "\"state\": " << static_cast<int>(enemies_[i].state);
                    oss << "}";
                    if (i < kMaxEnemies - 1) oss << ",";
                    oss << "\n";
                }
                oss << "  ],\n";
                // ボス情報
                float bodyBounceForJson = 0.0f;
                if (bossLegSwingSpeed_ > 0.0f) {
                    bodyBounceForJson = std::sin(bossTime_ * 2.0f) * bossBodyBounceRange_;
                }
                float dropOffsetForJson = 0.0f;
                if (bossAppearanceTimer_ > 0.0f) {
                    float appRate = (3.0f - bossAppearanceTimer_) / 3.0f;
                    dropOffsetForJson = 120.0f * (1.0f - appRate) * (1.0f - appRate);
                }
                oss << "  \"boss\": {";
                oss << "\"active\": " << (currentPhase_ == GamePhase::kBossFight ? "true" : "false") << ", ";
                oss << "\"visible\": " << (isBossModelVisible_ ? "true" : "false") << ", ";
                oss << "\"x\": 0.0, ";
                oss << "\"y\": " << (bossYOffset_ + bodyBounceForJson + dropOffsetForJson) << ", ";
                oss << "\"z\": " << (fighterWorldZ_ + bossZOffset_) << ", ";
                oss << "\"rot_y\": " << bossBodyRotY_ << ", ";
                oss << "\"hp\": " << bossHP_ << ", ";
                oss << "\"maxhp\": " << bossMaxHP_ << ", ";
                oss << "\"action\": " << static_cast<int>(bossActionState_);
                oss << "},\n";
                // 弾情報 (全60発・固定インデックス・aliveフラグ付き)
                // ※インデックスを固定することでBlender側が同じ弾を追跡できる
                static const int kMaxDisplayBullets = 20; // 先頭20発のみ出力(JSON軽量化)
                oss << "  \"bullets\": [\n";
                for (int i = 0; i < kMaxDisplayBullets; ++i) {
                    bool alive = (playerBullets_[i].currentTime < playerBullets_[i].lifeTime);
                    oss << "    {\"alive\": " << (alive ? "true" : "false") << ", ";
                    oss << "\"x\": " << playerBullets_[i].position.x << ", ";
                    oss << "\"y\": " << playerBullets_[i].position.y << ", ";
                    oss << "\"z\": " << playerBullets_[i].position.z << "}";
                    if (i < kMaxDisplayBullets - 1) oss << ",";
                    oss << "\n";
                }
                oss << "  ],\n";
                // 追加情報
                oss << "  \"recording\": " << (replayRecording_ ? "true" : "false") << "\n";
                oss << "}\n";

                std::ofstream posFile("Resources/game_state.json", std::ios::trunc);
                if (posFile.is_open()) {
                    posFile << oss.str();
                    posFile.close();
                }
            }

            // ── リプレイ録画 (RキーでON/OFF, 録画中は毎フレームCSVに追記) ──
            if (input_->IsKeyTriggered(DIK_R) && !(phaseIntroTimer_ >= 0.0f)) {
                replayRecording_ = !replayRecording_;
                if (replayRecording_) {
                    replayFrameCounter_ = 0;
                    replaySessionIndex_++;
                    std::string filename = "Resources/replay_frames_" + std::to_string(replaySessionIndex_) + ".csv";
                    // ヘッダー行を一度書いて初期化
                    std::ofstream repFile(filename, std::ios::trunc);
                    if (repFile.is_open()) {
                        repFile << "frame,px,py,pz,proll,ppitch";
                        for (int i = 0; i < kMaxEnemies; ++i) {
                            repFile << ",e" << i << "_alive,e" << i << "_x,e" << i << "_y,e" << i << "_z,e" << i << "_rz";
                        }
                        for (int i = 0; i < 20; ++i) {
                            repFile << ",b" << i << "_alive,b" << i << "_x,b" << i << "_y,b" << i << "_z";
                        }
                        repFile << ",boss_active,boss_x,boss_y,boss_z,boss_ry\n";
                        repFile.close();
                    }
                }
            }

            if (replayRecording_) {
                std::string filename = "Resources/replay_frames_" + std::to_string(replaySessionIndex_) + ".csv";
                std::ofstream repFile(filename, std::ios::app);
                if (repFile.is_open()) {
                    float bodyBounceRec = 0.0f;
                    if (bossLegSwingSpeed_ > 0.0f) {
                        bodyBounceRec = std::sin(bossTime_ * 2.0f) * bossBodyBounceRange_;
                    }
                    float dropOffsetRec = 0.0f;
                    if (bossAppearanceTimer_ > 0.0f) {
                        float appRate = (3.0f - bossAppearanceTimer_) / 3.0f;
                        dropOffsetRec = 120.0f * (1.0f - appRate) * (1.0f - appRate);
                    }
                    repFile << replayFrameCounter_++
                            << "," << fighterWorldPos.x
                            << "," << fighterWorldPos.y
                            << "," << fighterWorldPos.z
                            << "," << (fighterModel_ ? fighterModel_->transform.rotate.z : 0.0f)
                            << "," << (fighterModel_ ? fighterModel_->transform.rotate.x : 0.0f);
                    for (int i = 0; i < kMaxEnemies; ++i) {
                        repFile << "," << (enemies_[i].isAlive ? 1 : 0)
                                << "," << enemies_[i].position.x
                                << "," << enemies_[i].position.y
                                << "," << enemies_[i].position.z
                                << "," << enemies_[i].rotate.z;
                    }
                    // 弾の情報 (最大20発)
                    for (int i = 0; i < 20; ++i) {
                        bool bulletAlive = (playerBullets_[i].currentTime < playerBullets_[i].lifeTime);
                        repFile << "," << (bulletAlive ? 1 : 0)
                                << "," << playerBullets_[i].position.x
                                << "," << playerBullets_[i].position.y
                                << "," << playerBullets_[i].position.z;
                    }
                    bool bossActiveRec = (currentPhase_ == GamePhase::kBossFight);
                    repFile << "," << (bossActiveRec ? 1 : 0)
                            << ",0.0"
                            << "," << (bossYOffset_ + bodyBounceRec + dropOffsetRec)
                            << "," << (fighterWorldZ_ + bossZOffset_)
                            << "," << bossBodyRotY_ << "\n";
                    repFile.close();
                }
            }

            // ── Blenderパラメータ読み込み (60フレームに1回) ──
            if (++blenderParamReadCounter_ >= 60) {
                blenderParamReadCounter_ = 0;
                std::ifstream paramIn("Resources/blender_params.txt");
                if (paramIn.is_open()) {
                    std::string line;
                    while (std::getline(paramIn, line)) {
                        if (line.empty() || line[0] == '#') continue;
                        size_t pos = line.find('=');
                        if (pos == std::string::npos) continue;
                        std::string key = line.substr(0, pos);
                        std::string val = line.substr(pos + 1);
                        try {
                            float v = std::stof(val);
                            if (key == "boss_z_offset")       bossZOffset_      = v;
                            else if (key == "boss_y_offset") bossYOffset_      = v;
                            else if (key == "boss_scale")    bossScale_        = v;
                            else if (key == "player_speed_x")playerSpeedX_     = v;
                            else if (key == "player_speed_y")playerSpeedY_     = v;
                            else if (key == "player_limit_x")playerLimitX_     = v;
                            else if (key == "player_limit_y")playerLimitY_     = v;
                            else if (key == "boss_hp")        bossHP_           = v;
                            else if (key == "player_hp")      playerHP_         = v;
                        } catch (...) {}
                    }
                    paramIn.close();
                }
            }

            // ── 弾の発射（LCtrl） ────────────────────────────────────
            // デフォルトのレティクル位置をレールの進行方向120m前に設定
            Vector3 defaultReticlePos = Add(fighterWorldPos, Scale(playerRailDir, 120.0f));

            Vector3 targetReticlePos = defaultReticlePos;
            if (currentPhase_ == GamePhase::kBossFight) {
                float dropOffset = 0.0f;
                if (bossAppearanceTimer_ > 0.0f) {
                    float appRate = (3.0f - bossAppearanceTimer_) / 3.0f;
                    dropOffset = 120.0f * (1.0f - appRate) * (1.0f - appRate);
                }
                float bodyBounce = 0.0f;
                if (bossLegSwingSpeed_ > 0.0f) {
                    bodyBounce = std::sin(bossTime_ * 2.0f) * bossBodyBounceRange_;
                }
                targetReticlePos = GetBossPosition(bodyBounce, dropOffset);
            } else {
                float bestDist2D = 30.0f;
                Enemy* lockedEnemy = nullptr;
                for (auto& enemy : enemies_) {
                    if (!enemy.isAlive) continue;
                    // レールの進行方向に対して「前方」にいる敵のみロックオン対象
                    Vector3 toEnemy = Subtract(enemy.position, fighterWorldPos);
                    float forwardDot = toEnemy.x * playerRailDir.x + toEnemy.y * playerRailDir.y + toEnemy.z * playerRailDir.z;
                    if (forwardDot > 0.0f) {
                        // プレイヤーのローカル空間（右・上）に投影して、画面上での2D距離を計算
                        float rx = toEnemy.x * playerRailRight.x + toEnemy.y * playerRailRight.y + toEnemy.z * playerRailRight.z;
                        float ry = toEnemy.x * playerRailUp.x    + toEnemy.y * playerRailUp.y    + toEnemy.z * playerRailUp.z;
                        float dist2D = std::sqrt(rx * rx + ry * ry);
                        if (dist2D < bestDist2D) {
                            bestDist2D = dist2D;
                            lockedEnemy = &enemy;
                        }
                    }
                }
                if (lockedEnemy) {
                    targetReticlePos = lockedEnemy->position;
                }
            }
            float aimLerpSpeed = 0.05f;
            aimReticlePos_.x = std::lerp(aimReticlePos_.x, targetReticlePos.x, aimLerpSpeed);
            aimReticlePos_.y = std::lerp(aimReticlePos_.y, targetReticlePos.y, aimLerpSpeed);
            aimReticlePos_.z = std::lerp(aimReticlePos_.z, targetReticlePos.z, aimLerpSpeed);
            Vector3 reticlePos = aimReticlePos_;

            // ボス登場演出中は射撃を禁止
            bool isBossIntro = (currentPhase_ == GamePhase::kBossFight && bossAppearanceTimer_ > 0.0f);
            if (input_->IsKeyTriggered(DIK_SPACE) && !(phaseIntroTimer_ >= 0.0f) && !isBossDefeatedSequence_ && !isBossIntro) {
                // 翼の発射位置をレールのright/up方向で計算
                Vector3 leftWing  = Add(fighterWorldPos, Add(Scale(playerRailRight, -2.5f), Scale(playerRailUp, 0.8f)));
                Vector3 rightWing = Add(fighterWorldPos, Add(Scale(playerRailRight,  2.5f), Scale(playerRailUp, 0.8f)));

                Vector3 dirLeft  = Normalize({ reticlePos.x - leftWing.x,  reticlePos.y - leftWing.y,  reticlePos.z - leftWing.z });
                Vector3 dirRight = Normalize({ reticlePos.x - rightWing.x, reticlePos.y - rightWing.y, reticlePos.z - rightWing.z });
                float bulletSpeed = 150.0f + boostForwardSpeed_;

                for (auto& b : playerBullets_) {
                    if (b.currentTime >= b.lifeTime) {
                        b.position = leftWing; b.velocity = Scale(dirLeft, bulletSpeed);
                        b.lifeTime = 2.0f; b.currentTime = 0.0f; break;
                    }
                }
                for (auto& b : playerBullets_) {
                    if (b.currentTime >= b.lifeTime) {
                        b.position = rightWing; b.velocity = Scale(dirRight, bulletSpeed);
                        b.lifeTime = 2.0f; b.currentTime = 0.0f; break;
                    }
                }
                audio_->PlayWave(jumpSE_, false, 1.0f);
            }

            // エイミング(レティクル)の更新
            Matrix4x4 reticleWorld = MakeAffineMatrix(Vector3{8.0f, 8.0f, 8.0f}, Vector3{0.0f, 0.0f, 0.0f}, reticlePos);
            aimingInstancingData_->World = reticleWorld;

            // ── 自機と敵・ボスの衝突判定 ────────────────────────
            // ボス登場演出中は衝突判定を行わない
            if (currentPhase_ == GamePhase::kBossFight && !(bossAppearanceTimer_ > 0.0f)) {
                // 落下登場演出時のオフセット
                float dropOffset = 0.0f;
                if (bossAppearanceTimer_ > 0.0f) {
                    float appRate = (3.0f - bossAppearanceTimer_) / 3.0f;
                    dropOffset = 120.0f * (1.0f - appRate) * (1.0f - appRate);
                }

                // ボスとの衝突
                float bodyBounce = 0.0f;
                if (bossLegSwingSpeed_ > 0.0f) {
                    bodyBounce = std::sin(bossTime_ * 2.0f) * bossBodyBounceRange_;
                }
                Vector3 bossPos = GetBossPosition(bodyBounce, dropOffset);
                Vector3 diff = Subtract(fighterWorldPos, bossPos);
                float dist = Length(diff);
                if (dist <= (bossCollisionRadius_ + playerCollisionRadius_)) { // 自機半径
                    // 接触中は毎フレーム少しずつダメージを受ける
                    playerHP_ -= 0.5f;
                    if (playerHP_ < 0.0f) playerHP_ = 0.0f;

                    // 一定頻度で被弾エフェクトとSEを発生
                    static uint32_t hitTimer = 0;
                    if (++hitTimer % 15 == 0) {
                        particleManager_->EmitHit(fighterWorldPos);
                        audio_->PlayWave(jumpSE_, false, 0.8f);
                    }
                }


            } else {
                // 自機とビルの衝突判定（Boxコライダー対応）
                for (size_t i = 0; i < buildings_.size(); ++i) {
                    auto& b = buildings_[i];
                    if (b.floors <= 0 || b.isDestroyed) continue;
                    if (!b.collider.hasCollider) continue;
                    
                    Vector3 boxScale = { b.scale.x, b.scale.y * b.floors, b.scale.z };
                    if (IsCollidingOBBAndSphere(fighterWorldPos, playerCollisionRadius_, b.position, boxScale, b.rotate.y, b.collider.center, b.collider.size)) {
                        playerHP_ -= 0.5f; // 毎フレームダメージを受ける
                        if (playerHP_ < 0.0f) playerHP_ = 0.0f;
                        
                        static uint32_t bHitTimer = 0;
                        if (++bHitTimer % 15 == 0) {
                            particleManager_->EmitHit(fighterWorldPos);
                            audio_->PlayWave(jumpSE_, false, 0.8f);
                        }
                    }
                }

                // 雑魚敵との衝突
                for (auto& enemy : enemies_) {
                    if (!enemy.isAlive) continue;

                    bool hit = false;
                    if (enemy.collider.hasCollider) {
                        hit = IsCollidingOBBAndSphere(fighterWorldPos, playerCollisionRadius_, enemy.position, enemy.scale, enemy.rotate.y, enemy.collider.center, enemy.collider.size);
                    } else {
                        Vector3 diff = Subtract(fighterWorldPos, enemy.position);
                        float dist = Length(diff);
                        
                        if (dist <= (enemy.radius + playerCollisionRadius_)) {
                            hit = true;
                        }
                        // 特攻状態におけるすり抜け防止判定
                        else if (enemy.state == Enemy::State::kDive) {
                            float frameMovement = enemy.speed * kDeltaTime;
                            float zDiff = enemy.position.z - fighterWorldPos.z;
                            if (std::abs(zDiff) < (frameMovement * 0.75f + 3.0f)) {
                                float distXY = std::sqrt(diff.x * diff.x + diff.y * diff.y);
                                if (distXY <= (enemy.radius + playerCollisionRadius_)) {
                                    hit = true;
                                }
                            }
                        }
                    }

                    if (hit) {
                        // 敵を撃破
                        enemy.isAlive = false;
                        playerHP_ -= 10.0f;
                        if (playerHP_ < 0.0f) playerHP_ = 0.0f;

                        // 被弾エフェクトとSE
                        EmitHitEffect(enemy.position);
                        audio_->PlayWave(jumpSE_, false, 1.2f);
                    }
                }
            }
        }
    }

    // ── 弾の更新（全モード共通）と敵・ボスとの衝突判定 ──
    if (!(phaseIntroTimer_ >= 0.0f)) {
        for (int i = 0; i < kMaxBullets; ++i) {
            if (playerBullets_[i].currentTime < playerBullets_[i].lifeTime) {
                playerBullets_[i].position = Add(playerBullets_[i].position, Scale(playerBullets_[i].velocity, kDeltaTime));
                playerBullets_[i].currentTime += kDeltaTime;

                // ボス登場演出中は弾との衝突判定を行わない
                if (currentPhase_ == GamePhase::kBossFight && !(bossAppearanceTimer_ > 0.0f)) {
                // 落下登場演出時のオフセット
                float dropOffset = 0.0f;
                if (bossAppearanceTimer_ > 0.0f) {
                    float appRate = (3.0f - bossAppearanceTimer_) / 3.0f;
                    dropOffset = 120.0f * (1.0f - appRate) * (1.0f - appRate);
                }

                float bodyBounce = 0.0f;
                if (bossLegSwingSpeed_ > 0.0f) {
                    bodyBounce = std::sin(bossTime_ * 2.0f) * bossBodyBounceRange_;
                }
                Vector3 bossPos = GetBossPosition(bodyBounce, dropOffset);

                Vector3 diff = Subtract(playerBullets_[i].position, bossPos);
                float dist = Length(diff);
                if (dist <= (bossCollisionRadius_ + 2.5f)) { // 弾の半径2.5f
                    // 弾を消去
                    playerBullets_[i].currentTime = playerBullets_[i].lifeTime;

                    // ボスにダメージ
                    bossHP_ -= 10.0f;
                    if (bossHP_ < 0.0f) bossHP_ = 0.0f;

                    // 被弾エフェクトとSE
                    EmitHitEffect(playerBullets_[i].position);
                    audio_->PlayWave(jumpSE_, false, 1.2f);
                }
            }
            // 非ボスフェーズ時の敵との当たり判定 (球衝突判定)
            else {
                for (auto& enemy : enemies_) {
                    if (!enemy.isAlive) continue;

                    bool hit = false;
                    if (enemy.collider.hasCollider) {
                        hit = IsCollidingOBBAndSphere(playerBullets_[i].position, 2.5f, enemy.position, enemy.scale, enemy.rotate.y, enemy.collider.center, enemy.collider.size);
                    } else {
                        Vector3 diff = Subtract(playerBullets_[i].position, enemy.position);
                        float dist = Length(diff);
                        // 弾の当たり判定半径を広げて（0.5f -> 2.5f）、すり抜けや近距離で当たらない現象を緩和
                        if (dist <= (enemy.radius + 2.5f)) {
                            hit = true;
                        }
                    }

                    if (hit) {
                        // 弾を消去
                        playerBullets_[i].currentTime = playerBullets_[i].lifeTime;

                        // 敵のHPを減算
                        enemy.hp -= 10.0f;
                        if (enemy.hp <= 0.0f) {
                            // 敵を撃破
                            enemy.isAlive = false;
                            Vector3 deathPos = enemy.position; // 爆発エフェクト発生位置を記録

                            // 全滅リポップはUpdateの最後で一括判定するため、ここではリポップ処理を行わない

                            // ★★★ 超ド派手爆破エフェクト！ ★★★
                            EmitHitEffect(deathPos);
                            // プリセット5(氷結)・8(神聖)以外のときは属性色のシリンダーとリングを重ね合わせて撃破を強調
                            if (selectedEffectPreset_ != 5 && selectedEffectPreset_ != 8) {
                                particleManager_->EmitCylinder(deathPos, effectBaseColor_);
                                particleManager_->EmitRing(deathPos, effectBaseColor_);
                            }

                            // 爆破音を再生
                            audio_->PlayWave(jumpSE_, false, 1.5f);
                        } else {
                            // 生存時は小規模な被弾エフェクトとSE
                            EmitHitEffect(playerBullets_[i].position);
                            audio_->PlayWave(jumpSE_, false, 0.6f);
                        }
                        break;
                    }
                }
            }
        }
    }
}

    // ── 蜘蛛ボス（Big Spider）の攻撃AIと攻撃更新 ──
    if (currentPhase_ == GamePhase::kBossFight && !isBossDefeatedSequence_ && bossAppearanceTimer_ <= 0.0f) {
        bossActionTimer_ += kDeltaTime;

        // 攻撃周期：2.5秒ごとに攻撃を切り替える（高速化）
        if (bossActionState_ == BossActionState::kIdle && bossActionTimer_ >= 2.5f) {
            bossActionTimer_ = 0.0f;
            // デバッグ確認用：常に近接攻撃（100%確率）を行う
            bossActionState_ = BossActionState::kLegAttack;
        }

        // 各攻撃パターンに応じたボスの物理的な移動やアニメーション設定
        if (bossActionState_ == BossActionState::kLegAttack) {
            // 足殴り：ボスが一時的にプレイヤーに急接近する（6.8秒で往復：5.0秒接近・振りかぶり溜め、0.3秒叩きつけ、1.5秒後退に調整）
            static bool hasShaken = false;
            Vector3 lightningColor = { 0.68f, 0.05f, 1.0f }; // カッコいいネオンパープル（紫色の雷）
            
            float bodyBounce = std::sin(bossTime_ * 2.0f) * bossBodyBounceRange_;
            Vector3 currentBossPos = { 0.0f, bossYOffset_ + bodyBounce, fighterWorldZ_ + bossZOffset_ };

            if (bossActionTimer_ <= 5.0f) {
                // 1. 接近・振りかぶり溜め (0.0〜5.0秒)
                float t = bossActionTimer_ / 5.0f;
                bossZOffset_ = std::lerp(169.0f, 65.0f, t);
                hasShaken = false; // 振りかぶりフェーズではシェイクフラグをリセット
                
                // 振りかぶり中にプレイヤーの位置を追従し、ターゲットエリアを決定（レールローカル横位置で判定するように修正）
                float playerLocalX = fighterModel_ ? fighterModel_->transform.translate.x : 0.0f;
                if (playerLocalX < -15.0f) {
                    bossAttackTargetArea_ = 0; // 左
                } else if (playerLocalX > 15.0f) {
                    bossAttackTargetArea_ = 2; // 右
                } else {
                    bossAttackTargetArea_ = 1; // 中央
                }
                isBossAttackTargetLocked_ = false;

                // ★★★ 紫の雷の溜めエフェクト ★★★
                // ボス本体の周囲に雷オーラを発生
                particleManager_->EmitLightning(currentBossPos, 8.0f, 2, lightningColor);
                
                // 振りかぶる右前脚（i=5）自体に強烈な紫の電撃オーラを纏わせる
                if (bossLegTransformData_[5]) {
                    Matrix4x4 w = bossLegTransformData_[5]->World;
                    Vector3 legDir = { w.m[2][0], w.m[2][1], w.m[2][2] };
                    Vector3 legRoot = { w.m[3][0], w.m[3][1], w.m[3][2] };
                    
                    float dirLen = std::sqrt(legDir.x * legDir.x + legDir.y * legDir.y + legDir.z * legDir.z);
                    if (dirLen > 0.001f) {
                        legDir = { legDir.x / dirLen, legDir.y / dirLen, legDir.z / dirLen };
                    }
                    
                    float legLen = bossLegScale_ * 0.55f;
                    Vector3 legTip = {
                        legRoot.x + legDir.x * legLen,
                        legRoot.y + legDir.y * legLen,
                        legRoot.z + legDir.z * legLen
                    };
                    
                    Vector3 legMid = {
                        (legRoot.x + legTip.x) * 0.5f,
                        (legRoot.y + legTip.y) * 0.5f,
                        (legRoot.z + legTip.z) * 0.5f
                    };

                    // 1. 脚全体を包む極太の這いずり電撃オーラ（スケールを負の巨大値 -1.85f に拡張）
                    // 毎フレーム3組重複して放出することで、脚に絡みつく無数のうねる巨大電撃を生成
                    for (int k = 0; k < 3; ++k) {
                        particleManager_->EmitLSystemLightning(legRoot, legTip, 3, -1.85f, lightningColor);
                    }

                    // 2. 脚の先端・中間・根本から周囲の空間へ大きく弾け飛ぶ放射スパーク
                    // 半径 15.0f〜24.0f の非常に大きな紫の火花を空間へ放出する
                    particleManager_->EmitLightning(legTip, 24.0f, 3, lightningColor);
                    particleManager_->EmitLightning(legMid, 18.0f, 2, lightningColor);
                    particleManager_->EmitLightning(legRoot, 14.0f, 1, lightningColor);
                }

                // 狙っている地面（ターゲットエリア）にバチバチと警告予兆の雷を落とす
                float targetX = 0.0f;
                if (bossAttackTargetArea_ == 0) targetX = -30.0f;
                else if (bossAttackTargetArea_ == 2) targetX = 30.0f;

                // ターゲット位置のレール座標系を算出
                float targetProgress = fighterWorldZ_ + 65.0f;
                Vector3 targetRailPos = GetRailPosition(targetProgress);
                Vector3 targetRailDir = GetRailDirection(targetProgress);
                Vector3 targetRailRight = Normalize(Vector3{ targetRailDir.z, 0.0f, -targetRailDir.x });
                Vector3 targetRailUp = Cross(targetRailDir, targetRailRight);
                float targetRelY = bossYOffset_ + 14.0f; // -20m路面に対する相対Y

                Vector3 warningPos = Add(targetRailPos, Add(Scale(targetRailRight, targetX), Scale(targetRailUp, targetRelY)));
                particleManager_->EmitLightning(warningPos, 5.0f, 1, lightningColor);

            } else if (bossActionTimer_ <= 5.3f) {
                // 2. 振り下ろし叩きつけ (5.0〜5.3秒)
                bossZOffset_ = 65.0f;
                bossLegSwingRange_ = 1.2f; // 足を激しく動かす
                
                // 振り下ろし開始時にターゲットエリアをロックオン
                isBossAttackTargetLocked_ = true;

                // 振り下ろし中も激しいスパークをボスとターゲット先に落とす
                float targetX = 0.0f;
                if (bossAttackTargetArea_ == 0) targetX = -30.0f;
                else if (bossAttackTargetArea_ == 2) targetX = 30.0f;

                float targetProgress = fighterWorldZ_ + 65.0f;
                Vector3 targetRailPos = GetRailPosition(targetProgress);
                Vector3 targetRailDir = GetRailDirection(targetProgress);
                Vector3 targetRailRight = Normalize(Vector3{ targetRailDir.z, 0.0f, -targetRailDir.x });
                Vector3 targetRailUp = Cross(targetRailDir, targetRailRight);
                float targetRelY = bossYOffset_ + 14.0f;

                Vector3 warningPos = Add(targetRailPos, Add(Scale(targetRailRight, targetX), Scale(targetRailUp, targetRelY)));
                particleManager_->EmitLightning(currentBossPos, 12.0f, 2, lightningColor);
                particleManager_->EmitLightning(warningPos, 12.0f, 2, lightningColor);

            } else if (bossActionTimer_ <= 6.8f) {
                // 3. 戻り (5.3〜6.8秒)
                // ─── 巨大な「1本の雷」の3フェーズタイムライン (先行微光➔本落雷➔残光明滅) ───
                float strikeElapsed = bossActionTimer_ - 5.3f; // 激突からの経過時間

                // ターゲットレール情報の準備
                float targetProgress = fighterWorldZ_ + 65.0f;
                Vector3 targetRailPos = GetRailPosition(targetProgress);
                Vector3 targetRailDir = GetRailDirection(targetProgress);
                Vector3 targetRailRight = Normalize(Vector3{ targetRailDir.z, 0.0f, -targetRailDir.x });
                Vector3 targetRailUp = Cross(targetRailDir, targetRailRight);
                float targetRelY = bossYOffset_ + 14.0f;

                // ボス手元の電撃開始レール情報の準備
                float bossProgress = fighterWorldZ_ + bossZOffset_;
                Vector3 bossRailPos = GetRailPosition(bossProgress);
                Vector3 bossRailDir = GetRailDirection(bossProgress);
                Vector3 bossRailRight = Normalize(Vector3{ bossRailDir.z, 0.0f, -bossRailDir.x });
                Vector3 bossRailUp = Cross(bossRailDir, bossRailRight);

                if (strikeElapsed <= 0.05f) {
                    // ① 先行微光 (Leader) [0.00s 〜 0.05s / 約3フレーム]
                    float targetX = 0.0f;
                    if (bossAttackTargetArea_ == 0) targetX = -30.0f;
                    else if (bossAttackTargetArea_ == 2) targetX = 30.0f;

                    Vector3 impactPos = Add(targetRailPos, Add(Scale(targetRailRight, targetX), Scale(targetRailUp, targetRelY)));
                    Vector3 lightningStart = Add(bossRailPos, Add(Scale(bossRailUp, bossYOffset_ + 16.0f), Scale(bossRailDir, -15.0f)));
                    
                    // 暗めの紫色 (-0.18fスケールで細い枝として生成)
                    Vector3 leaderColor = { 0.35f, 0.02f, 0.5f };
                    particleManager_->EmitLSystemLightning(lightningStart, impactPos, 3, -0.18f, leaderColor);
                    
                    // ポストプロセスフラッシュもまだ極小
                    activePostProcess_ = kVignette;
                    // 雷のX座標をUV座標にマッピング (-30➔0.16, 0➔0.5, 30➔0.84)
                    float targetUvX = 0.5f;
                    if (bossAttackTargetArea_ == 0) targetUvX = 0.16f;
                    else if (bossAttackTargetArea_ == 2) targetUvX = 0.84f;
                    vignetteParamData_->scale = targetUvX;
                    vignetteParamData_->power = 10.0f + 0.15f; // ごく僅かな明かり

                } else if (strikeElapsed <= 0.10f) {
                    // ② 本落雷 (Return Stroke) [0.05s 〜 0.10s / 約3フレーム]
                    float targetX = 0.0f;
                    if (bossAttackTargetArea_ == 0) targetX = -30.0f;
                    else if (bossAttackTargetArea_ == 2) targetX = 30.0f;

                    Vector3 impactPos = Add(targetRailPos, Add(Scale(targetRailRight, targetX), Scale(targetRailUp, targetRelY)));
                    Vector3 lightningStart = Add(bossRailPos, Add(Scale(bossRailUp, bossYOffset_ + 16.0f), Scale(bossRailDir, -15.0f)));

                    if (!hasShaken) {
                        cameraShakeTimer_ = 0.78f;
                        cameraShakeIntensity_ = 10.5f; // 最大震度
                        activeShakeIntensity_ = 10.5f;
                        cameraShakeTimeMax_ = 0.78f;
                        hasShaken = true;

                        // 地面の破片を飛び散らせる
                        triggerDebrisEmit_ = true;
                        debrisEmitPos_ = Add(targetRailPos, Scale(targetRailRight, targetX));

                        // 極太の主幹雷撃(1.6fスケール)を走らせる
                        particleManager_->EmitLSystemLightning(lightningStart, impactPos, 4, 1.6f, lightningColor);

                        // 周囲の爆発エフェクトと大音響SE
                        particleManager_->EmitCylinder(impactPos, lightningColor);
                        particleManager_->EmitRing(impactPos, lightningColor);
                        particleManager_->EmitCustomSparks(impactPos, 25.0f, 15, { 0.5f, 0.5f, 0.5f }, 1.0f);
                        audio_->PlayWave(jumpSE_, false, 1.7f);

                        // ダメージ判定
                        // レール横方向（右方向ベクトル）の内積などからプレイヤーの左右エリアを判定
                        // 簡略化のため、プレイヤーの相対X (fighterModel_->transform.translate.x) を使用する
                        float playerRelX = fighterModel_ ? fighterModel_->transform.translate.x : 0.0f;
                        int playerArea = 1;
                        if (playerRelX < -15.0f) playerArea = 0;
                        else if (playerRelX > 15.0f) playerArea = 2;
                        if (playerArea == bossAttackTargetArea_) {
                            playerHP_ -= 25.0f;
                            if (playerHP_ < 0.0f) playerHP_ = 0.0f;
                            particleManager_->EmitHit(fighterWorldPos);
                            audio_->PlayWave(jumpSE_, false, 1.2f);
                        }
                    }

                    // 全画面フラッシュバースト（最大露出）
                    activePostProcess_ = kVignette;
                    float targetUvX = 0.5f;
                    if (bossAttackTargetArea_ == 0) targetUvX = 0.16f;
                    else if (bossAttackTargetArea_ == 2) targetUvX = 0.84f;
                    vignetteParamData_->scale = targetUvX;
                    vignetteParamData_->power = 11.0f; // 10.0 + 1.0 (最大フラッシュ)

                } else {
                    // ③ 残光・明滅 (Flicker) [0.10s 〜 1.5s]
                    float currentIntensity = 1.0f - (strikeElapsed - 0.10f) / 1.4f; // 1.0 ➔ 0.0
                    currentIntensity = (std::max)(0.0f, currentIntensity);

                    // 高速なバチバチ明滅
                    float flicker = (std::sin(strikeElapsed * 48.0f) * 0.5f + 0.5f); // 0.0 〜 1.0
                    float flashIntensity = currentIntensity * (0.3f + 0.7f * flicker);

                    if (currentIntensity <= 0.0f) {
                        activePostProcess_ = kNone;
                        vignetteParamData_->power = 0.8f;
                    } else {
                        activePostProcess_ = kVignette;
                        float targetUvX = 0.5f;
                        if (bossAttackTargetArea_ == 0) targetUvX = 0.16f;
                        else if (bossAttackTargetArea_ == 2) targetUvX = 0.84f;
                        vignetteParamData_->scale = targetUvX;
                        vignetteParamData_->power = 10.0f + flashIntensity;

                        // 残光中のバチバチスパーク
                        if (flicker > 0.7f) {
                            float targetX = 0.0f;
                            if (bossAttackTargetArea_ == 0) targetX = -30.0f;
                            else if (bossAttackTargetArea_ == 2) targetX = 30.0f;
                            Vector3 impactPos = Add(targetRailPos, Add(Scale(targetRailRight, targetX), Scale(targetRailUp, targetRelY)));
                            particleManager_->EmitLightning(impactPos, 8.0f, 1, lightningColor);
                        }
                    }
                }

                float t = (bossActionTimer_ - 5.3f) / 1.5f;
                bossZOffset_ = std::lerp(65.0f, 169.0f, t);
                isBossAttackTargetLocked_ = true;
            } else {
                // 戻りフェーズ中、または攻撃が終わったら徐々に全画面雷撃フラッシュエフェクトをフェードアウトさせる
                if (activePostProcess_ == kVignette && vignetteParamData_->power >= 10.0f) {
                    float currentIntensity = vignetteParamData_->power - 10.0f;
                    currentIntensity -= kDeltaTime * 1.5f;
                    if (currentIntensity <= 0.0f) {
                        activePostProcess_ = kNone;
                        vignetteParamData_->power = 0.8f;
                    } else {
                        vignetteParamData_->power = 10.0f + currentIntensity;
                    }
                }

                bossZOffset_ = 169.0f;
                bossLegSwingRange_ = 0.3f; // 元に戻す
                bossActionState_ = BossActionState::kIdle;
                bossActionTimer_ = 0.0f;
                isBossAttackTargetLocked_ = false;
            }
        } 
        else if (bossActionState_ == BossActionState::kLaserAttack) {
            // 糸レーザー：3秒間、高密度で直進パーティクルを射出する
            if (bossActionTimer_ <= 3.0f) {
                bossLaserTimer_ += kDeltaTime;
                if (bossLaserTimer_ >= 0.08f) { // 0.08秒ごとに2発
                    bossLaserTimer_ = 0.0f;
                    
                    float bodyBounce = std::sin(bossTime_ * 2.0f) * bossBodyBounceRange_;
                    Vector3 bossWebFirePos = {
                        bossWebFireOffset_.x,
                        bossYOffset_ + bodyBounce + bossWebFireOffset_.y,
                        fighterWorldZ_ + bossZOffset_ + bossWebFireOffset_.z
                    };
                    
                    EulerTransform& camTransForLaser = camera_->GetTransform();
                    fighterWorldPos = {
                        camTransForLaser.translate.x + fighterModel_->transform.translate.x,
                        camTransForLaser.translate.y - 3.0f + fighterModel_->transform.translate.y,
                        fighterWorldZ_
                    };
                    
                    particleManager_->EmitLaserThread(bossWebFirePos, fighterWorldPos);
                    audio_->PlayWave(jumpSE_, false, 0.3f);
                }
            } else {
                bossActionState_ = BossActionState::kIdle;
                bossActionTimer_ = 0.0f;
            }
        } 
        else if (bossActionState_ == BossActionState::kWebAttack) {
            // 蜘蛛の巣弾：プレイヤーに向けて巨大な蜘蛛の巣弾を1発射出する
            float bodyBounce = std::sin(bossTime_ * 2.0f) * bossBodyBounceRange_;
            Vector3 bossWebFirePos = {
                bossWebFireOffset_.x,
                bossYOffset_ + bodyBounce + bossWebFireOffset_.y,
                fighterWorldZ_ + bossZOffset_ + bossWebFireOffset_.z
            };
            
            EulerTransform& camTransForWeb = camera_->GetTransform();
            fighterWorldPos = {
                camTransForWeb.translate.x + fighterModel_->transform.translate.x,
                camTransForWeb.translate.y - 3.0f + fighterModel_->transform.translate.y,
                fighterWorldZ_
            };

            for (auto& web : bossWebBullets_) {
                if (!web.isAlive) {
                    web.isAlive = true;
                    web.position = bossWebFirePos;
                    
                    Vector3 dir = { fighterWorldPos.x - bossWebFirePos.x, fighterWorldPos.y - bossWebFirePos.y, fighterWorldPos.z - bossWebFirePos.z };
                    float len = std::sqrt(dir.x * dir.x + dir.y * dir.y + dir.z * dir.z);
                    if (len > 0.01f) {
                        dir = { dir.x / len, dir.y / len, dir.z / len };
                    } else {
                        dir = { 0.0f, 0.0f, -1.0f };
                    }
                    float speed = 100.0f; // 中速
                    web.velocity = { dir.x * speed, dir.y * speed, dir.z * speed };
                    
                    audio_->PlayWave(jumpSE_, false, 1.4f);
                    break;
                }
            }
            
            bossActionState_ = BossActionState::kIdle;
            bossActionTimer_ = 0.0f;
        }

        // ── 蜘蛛の巣弾の更新処理と自機との衝突・回避判定 ──
        EulerTransform& camTrans = camera_->GetTransform();
        fighterWorldPos = {
            camTrans.translate.x + fighterModel_->transform.translate.x,
            camTrans.translate.y - 3.0f + fighterModel_->transform.translate.y,
            fighterWorldZ_
        };

        for (auto& web : bossWebBullets_) {
            if (web.isAlive) {
                // 弾の移動
                web.position = {
                    web.position.x + web.velocity.x * kDeltaTime,
                    web.position.y + web.velocity.y * kDeltaTime,
                    web.position.z + web.velocity.z * kDeltaTime
                };

                // 自機との衝突判定
                Vector3 diff = { fighterWorldPos.x - web.position.x, fighterWorldPos.y - web.position.y, fighterWorldPos.z - web.position.z };
                float dist = std::sqrt(diff.x * diff.x + diff.y * diff.y + diff.z * diff.z);
                if (web.position.z < fighterWorldPos.z + 5.0f && web.position.z > fighterWorldPos.z - 5.0f && dist <= (web.radius + playerCollisionRadius_)) { // 自機半径
                    web.isAlive = false;
                    playerHP_ -= 20.0f; // 大ダメージ
                    if (playerHP_ < 0.0f) playerHP_ = 0.0f;
                    
                    // デバフ発動：3秒間の移動速度半減
                    playerSpeedDebuffTimer_ = 3.0f;

                    particleManager_->EmitHit(fighterWorldPos);
                    audio_->PlayWave(jumpSE_, false, 1.5f);
                }
                // 自機を通り越して回避成功したか判定
                else if (web.position.z < fighterWorldPos.z - 5.0f) {
                    web.isAlive = false;
                    screenWebTimer_ = 5.0f; // 画面蜘蛛の巣効果タイマー設定（5秒間）
                }
            }
        }

        // ── 糸レーザーと自機との衝突判定 ──
        if (bossActionState_ == BossActionState::kLaserAttack && bossActionTimer_ <= 3.0f) {
            static float laserHitCooldown = 0.0f;
            laserHitCooldown += kDeltaTime;
            if (laserHitCooldown >= 0.2f) {
                laserHitCooldown = 0.0f;
                
                float dx = fighterWorldPos.x - 0.0f;
                float dy = fighterWorldPos.y - (bossYOffset_ - 2.0f);
                float dist2D = std::sqrt(dx * dx + dy * dy);
                
                // 避けていなければ小ダメージ
                if (dist2D < 15.0f) {
                    playerHP_ -= 1.5f;
                    if (playerHP_ < 0.0f) playerHP_ = 0.0f;
                    
                    particleManager_->EmitHit(fighterWorldPos);
                    audio_->PlayWave(jumpSE_, false, 0.5f);
                }
            }
        }
    }

    // ── 蜘蛛ボス（Big Spider）のトランスフォームとアニメーション更新 ──
    if (currentPhase_ == GamePhase::kBossFight && bossBodyTransformData_ && isBossModelVisible_) {
        // 歩行速度が0より大きい時のみ、サイン波に同期した胴体の揺れ（上下バウンシング＆左右ロール）を計算
        float bodyBounce = 0.0f;
        float bodyRoll = 0.0f;
        if (bossLegSwingSpeed_ > 0.0f) {
            bodyBounce = std::sin(bossTime_ * 2.0f) * bossBodyBounceRange_;
            bodyRoll = std::cos(bossTime_) * (bossBodyRollRange_ * (float)M_PI / 180.0f);
        }

        // ── 振りかぶり近接攻撃のアニメーション計算 ──
        camTrans = camera_->GetTransform();
        fighterWorldPos = {
            camTrans.translate.x + fighterModel_->transform.translate.x,
            camTrans.translate.y - 3.0f + fighterModel_->transform.translate.y,
            fighterWorldZ_
        };
        // 常に右前脚の Pair 1 (i=5) の足で殴る
        int attackLegIdx = 5;

        float bossYAttackOffset = 0.0f;

        if (currentPhase_ == GamePhase::kBossFight && bossActionState_ == BossActionState::kLegAttack) {
            if (bossActionTimer_ <= 5.0f) {
                // 1. 接近・振りかぶり溜め (0.0〜5.0秒)
                float t = bossActionTimer_ / 5.0f;
                bossYAttackOffset = std::lerp(0.0f, -4.0f, t);
            } else if (bossActionTimer_ <= 5.3f) {
                // 2. 叩きつけ (5.0〜5.3秒)
                float t = (bossActionTimer_ - 5.0f) / 0.3f;
                bossYAttackOffset = std::lerp(-4.0f, -6.0f, t);
            } else if (bossActionTimer_ <= 6.8f) {
                // 3. 戻り (5.3〜6.8秒)
                float t = (bossActionTimer_ - 5.3f) / 1.5f;
                bossYAttackOffset = std::lerp(-6.0f, 0.0f, t);
            }
        }

        // 落下登場演出時のオフセット
        float dropOffset = 0.0f;
        if (bossAppearanceTimer_ > 0.0f) {
            float t = 5.0f - bossAppearanceTimer_;
            if (t < 1.8f) {
                float appRate = t / 1.8f;
                dropOffset = 120.0f * (1.0f - appRate) * (1.0f - appRate);
            }
        }

        // 胴体のワールド行列計算
        // ボスはプレイヤーの前方 bossZOffset_ の位置に進み、高さは接地高さ bossYOffset_
        Vector3 bossPos = GetBossPosition(bodyBounce, dropOffset, bossYAttackOffset);
        Vector3 bossBodyScale = { bossBodyScale_, bossBodyScale_, bossBodyScale_ }; // 胴体専用スケール
        // Y軸回転に加えて、Z軸のロール回転を合成
        Vector3 bossRotate = { 0.0f, bossBodyRotY_ * (float)M_PI / 180.0f, bodyRoll };

        // ボス咆哮時の姿勢（4.0秒から5.0秒の咆哮フェーズで上を向いて吠えるポーズと微振動を適用）
        if (bossAppearanceTimer_ > 0.0f) {
            float t = 5.0f - bossAppearanceTimer_;
            if (t >= 4.0f && t < 5.0f) {
                float roarProgress = (t - 4.0f) / 1.0f;
                float easeRoar = std::sin(roarProgress * static_cast<float>(M_PI)); // 0 -> 1 -> 0

                // 咆哮で天を仰ぐのけぞり (X軸回転、最大 -0.5ラジアン)
                bossRotate.x = -0.5f * easeRoar;

                // 胴体を大きく上方に持ち上げ、身を起こす威嚇動作 (最大 12.0m 上昇、8.0m 後退)
                bossPos.y += 12.0f * easeRoar;
                bossPos.z += 8.0f * easeRoar;

                // 咆哮の全身超激震 (高周波激震)
                float shakeFreq = 65.0f;
                bossRotate.y += std::sin(t * shakeFreq) * 0.08f;
                bossRotate.z += std::cos(t * shakeFreq) * 0.08f;
                bossPos.y += std::sin(t * shakeFreq) * 0.45f;
                bossPos.z += std::cos(t * shakeFreq) * 0.30f;
            }
        }

        // ボス位置におけるレール方向から姿勢行列を構築
        float bossProgress = fighterWorldZ_ + bossZOffset_;
        Vector3 bossRailDir = GetRailDirection(bossProgress);
        Vector3 bossRailRight = CalculateRailRight(bossRailDir);
        Vector3 bossRailUp = CalculateRailUp(bossRailDir, bossRailRight);

        Matrix4x4 R_rail = MakeIdentity4x4();
        R_rail.m[0][0] = bossRailRight.x; R_rail.m[0][1] = bossRailRight.y; R_rail.m[0][2] = bossRailRight.z;
        R_rail.m[1][0] = bossRailUp.x;    R_rail.m[1][1] = bossRailUp.y;    R_rail.m[1][2] = bossRailUp.z;
        R_rail.m[2][0] = bossRailDir.x;   R_rail.m[2][1] = bossRailDir.y;   R_rail.m[2][2] = bossRailDir.z;

        // ボスのローカル回転 (X:ピッチ/のけぞり -> Y:ヨー/微振動/180度対面 -> Z:ロール/歩行揺れ)
        Matrix4x4 rotateX = MakeRotateXMatrix(bossRotate.x);
        Matrix4x4 rotateY = MakeRotateYMatrix(bossRotate.y); // bossBodyRotY_に既に180度分が含まれているため、重複加算を削除
        Matrix4x4 rotateZ = MakeRotateZMatrix(bossRotate.z);
        Matrix4x4 R_local = Multiply(Multiply(rotateZ, rotateX), rotateY);

        // 最終姿勢行列の算出とワールド行列の組み立て
        Matrix4x4 R_final = Multiply(R_local, R_rail);

        Matrix4x4 bossWorld = Multiply(Multiply(MakeScaleMatrix(bossBodyScale), R_final), MakeTranslateMatrix(bossPos));
        bossBodyTransformData_->World = bossWorld;

        // 胴体の「スケールなし」の行列（足の大きさを胴体から完全に独立させるために使用、胴体の揺れも同期）
        Matrix4x4 bossWorldNoScale = Multiply(R_final, MakeTranslateMatrix(bossPos));

        // 4組の左右対称な足ペアパラメータを配列化してアクセス
        Vector3 legPairPos[4] = { bossLegPairPos0_, bossLegPairPos1_, bossLegPairPos2_, bossLegPairPos3_ };
        float legPairRotY[4] = { bossLegPairRotY0_, bossLegPairRotY1_, bossLegPairRotY2_, bossLegPairRotY3_ };

        // プレイヤーの現在のレール位置と軸方向を取得（足の攻撃ターゲット計算用）
        Vector3 playerRailPos = GetRailPosition(fighterWorldZ_);
        Vector3 playerRailDir = GetRailDirection(fighterWorldZ_);
        Vector3 playerRailRight = CalculateRailRight(playerRailDir);

        for (int i = 0; i < 8; ++i) {
            if (!bossLegTransformData_[i]) continue;

            int pairIdx = (i < 4) ? i : (i - 4); // 0,1,2,3 ➔ 左足、4,5,6,7 ➔ 右足

            // チドリ歩行パターンの位相オフセット（交互に動かす）
            float phaseOffset = (i == 0 || i == 2 || i == 5 || i == 7) ? 0.0f : (float)M_PI;

            // 前後のスイング（Y回転）
            float swing = std::sin(bossTime_ + phaseOffset) * bossLegSwingRange_;

            // 上下のステップ運動（Y軸平行移動の代わりに、根本をピボットにしたX軸回転に変更）
            // 指定されたリフト高さ（メートル）を、足の長さ（bossLegScale_ * 0.5f）を基準にラジアン角に変換
            float lift = (std::max)(0.0f, std::sin(bossTime_ + phaseOffset + (float)M_PI * 0.5f)) * bossLegLiftRange_;
            float liftAngleRad = lift / (bossLegScale_ * 0.5f);

            // 左右対称に配置するため、左足と右足で符号を分岐
            Vector3 finalOffset = legPairPos[pairIdx];
            float baseRotY = legPairRotY[pairIdx];

            Vector3 legRotate{};
            if (i < 4) {
                // 左足
                finalOffset.x = -finalOffset.x; // 左側なのでマイナス
                
                // 通常の回転姿勢
                float normalRotY = (-baseRotY) * (float)M_PI / 180.0f + swing;
                float normalPitch = -0.1f + liftAngleRad;
                
                float currentRotY = normalRotY;
                float currentPitch = normalPitch;
                
                // 攻撃中の左前足(i==1)の軌道を補間
                if (i == 1 && attackLegIdx == 1 && bossActionState_ == BossActionState::kLegAttack) {
                    float targetX = 0.0f;
                    if (bossAttackTargetArea_ == 0) targetX = -30.0f;
                    else if (bossAttackTargetArea_ == 2) targetX = 30.0f;
                    // レール空間での正しいターゲットのワールド座標を計算
                    Vector3 targetWorldPos = Add(playerRailPos, Scale(playerRailRight, targetX));
                    targetWorldPos.y = fighterWorldPos.y; // 高さは戦闘機の高さを追従

                    Vector3 diff = Subtract(targetWorldPos, bossPos);
                    // R_final の回転成分（直交行列）の転置を使って、ワールド差分 diff をボスのローカル空間に変換する
                    Vector3 playerLocal = {
                        diff.x * R_final.m[0][0] + diff.y * R_final.m[0][1] + diff.z * R_final.m[0][2],
                        diff.x * R_final.m[1][0] + diff.y * R_final.m[1][1] + diff.z * R_final.m[1][2],
                        diff.x * R_final.m[2][0] + diff.y * R_final.m[2][1] + diff.z * R_final.m[2][2]
                    };
                    
                    Vector3 jointLocal = finalOffset;
                    Vector3 scaledJointLocal = Scale(jointLocal, bossScale_);
                    Vector3 dirLocal = Subtract(playerLocal, scaledJointLocal);
                    
                    float len = std::sqrt(dirLocal.x * dirLocal.x + dirLocal.y * dirLocal.y + dirLocal.z * dirLocal.z);
                    if (len > 0.01f) {
                        Vector3 dirNorm = { dirLocal.x / len, dirLocal.y / len, dirLocal.z / len };
                        
                        float targetRotY = (-baseRotY) * (float)M_PI / 180.0f + std::atan2(dirNorm.x, dirNorm.z);
                        float upPitch = 1.4f; // 天高く振りかぶる角度
                        // プレイヤーの方向を向く叩きつけピッチを動的計算
                        float horizDist = std::sqrt(dirLocal.x * dirLocal.x + dirLocal.z * dirLocal.z);
                        float strikePitch = 0.1f;
                        if (horizDist > 0.01f) {
                            strikePitch = std::atan2(dirLocal.y, horizDist);
                            // 角度制限（ピッチの深い叩きつけ制限 -1.2 に拡張）
                            strikePitch = (std::max)(-1.2f, (std::min)(0.2f, strikePitch));
                        }
                        
                        // 角度の差を [-PI, PI] の範囲に正規化して最短経路にする
                        float diffRotY = targetRotY - normalRotY;
                        while (diffRotY > (float)M_PI) diffRotY -= 2.0f * (float)M_PI;
                        while (diffRotY < -(float)M_PI) diffRotY += 2.0f * (float)M_PI;

                        if (bossActionTimer_ <= 5.0f) {
                            // 1. 接近・振りかぶり溜め (0.0〜5.0秒)
                            float t = bossActionTimer_ / 5.0f;
                            currentRotY = normalRotY + diffRotY * t;
                            currentPitch = std::lerp(normalPitch, upPitch, t);
                        } else if (bossActionTimer_ <= 5.3f) {
                            // 2. 叩きつけ (5.0〜5.3秒)
                            float t = (bossActionTimer_ - 5.0f) / 0.3f;
                            currentRotY = targetRotY;
                            currentPitch = std::lerp(upPitch, strikePitch, t);
                        } else if (bossActionTimer_ <= 6.8f) {
                            // 3. 戻り (5.3〜6.8秒)
                            float t = (bossActionTimer_ - 5.3f) / 1.5f;
                            currentRotY = targetRotY - diffRotY * t;
                            currentPitch = std::lerp(strikePitch, normalPitch, t);
                        }
                    }
                }

                // 咆哮演出中 (4.0s ～ 5.0s) の前脚威嚇モーション
                if (bossAppearanceTimer_ > 0.0f) {
                    float t_appearance = 5.0f - bossAppearanceTimer_;
                    if (t_appearance >= 4.0f && t_appearance < 5.0f) {
                        float roarProgress = (t_appearance - 4.0f) / 1.0f;
                        float easeRoar = std::sin(roarProgress * static_cast<float>(M_PI));

                        if (i == 0 || i == 1) { // 左側の前脚
                            currentPitch = std::lerp(normalPitch, 1.5f, easeRoar);
                            currentRotY += std::sin(t_appearance * 80.0f) * 0.15f * easeRoar; // ガタガタ動かす
                        } else {
                            // 後ろ脚：踏みしめる
                            currentPitch = std::lerp(normalPitch, -0.4f, easeRoar);
                        }
                    }
                }
                
                legRotate = { 
                    currentPitch, 
                    currentRotY, 
                    0.0f 
                };
            } else {
                // 右足
                // 通常の回転姿勢
                float normalRotY = baseRotY * (float)M_PI / 180.0f + swing;
                float normalPitch = 0.1f - liftAngleRad;
                
                float currentRotY = normalRotY;
                float currentPitch = normalPitch;
                
                // 攻撃中の右前足(i==5)の軌道を補間
                if (i == 5 && attackLegIdx == 5 && bossActionState_ == BossActionState::kLegAttack) {
                    float targetX = 0.0f;
                    if (bossAttackTargetArea_ == 0) targetX = -30.0f;
                    else if (bossAttackTargetArea_ == 2) targetX = 30.0f;
                    // レール空間での正しいターゲットのワールド座標を計算
                    Vector3 targetWorldPos = Add(playerRailPos, Scale(playerRailRight, targetX));
                    targetWorldPos.y = fighterWorldPos.y; // 高さは戦闘機の高さを追従

                    Vector3 diff = Subtract(targetWorldPos, bossPos);
                    // R_final の回転成分（直交行列）の転置を使って、ワールド差分 diff をボスのローカル空間に変換する
                    Vector3 playerLocal = {
                        diff.x * R_final.m[0][0] + diff.y * R_final.m[0][1] + diff.z * R_final.m[0][2],
                        diff.x * R_final.m[1][0] + diff.y * R_final.m[1][1] + diff.z * R_final.m[1][2],
                        diff.x * R_final.m[2][0] + diff.y * R_final.m[2][1] + diff.z * R_final.m[2][2]
                    };
                    
                    Vector3 jointLocal = finalOffset;
                    Vector3 scaledJointLocal = Scale(jointLocal, bossScale_);
                    Vector3 dirLocal = Subtract(playerLocal, scaledJointLocal);
                    
                    float len = std::sqrt(dirLocal.x * dirLocal.x + dirLocal.y * dirLocal.y + dirLocal.z * dirLocal.z);
                    if (len > 0.01f) {
                        Vector3 dirNorm = { dirLocal.x / len, dirLocal.y / len, dirLocal.z / len };
                        
                        float targetRotY = baseRotY * (float)M_PI / 180.0f - std::atan2(dirNorm.x, dirNorm.z); // 右足はミラー反転関節のためマイナスが正しい
                        float upPitch = 1.4f; // 天高く振りかぶる角度
                        // プレイヤーの方向を向く叩きつけピッチを動的計算
                        float horizDist = std::sqrt(dirLocal.x * dirLocal.x + dirLocal.z * dirLocal.z);
                        float strikePitch = 0.1f;
                        if (horizDist > 0.01f) {
                            strikePitch = std::atan2(dirLocal.y, horizDist);
                            // 角度制限（ピッチの深い叩きつけ制限 -1.2 に拡張）
                            strikePitch = (std::max)(-1.2f, (std::min)(0.2f, strikePitch));
                        }
                        
                        // 角度の差を [-PI, PI] の範囲に正規化して最短経路にする
                        float diffRotY = targetRotY - normalRotY;
                        while (diffRotY > (float)M_PI) diffRotY -= 2.0f * (float)M_PI;
                        while (diffRotY < -(float)M_PI) diffRotY += 2.0f * (float)M_PI;

                        if (bossActionTimer_ <= 5.0f) {
                            // 1. 接近・振りかぶり溜め (0.0〜5.0秒)
                            float t = bossActionTimer_ / 5.0f;
                            currentRotY = normalRotY + diffRotY * t;
                            currentPitch = std::lerp(normalPitch, upPitch, t);
                        } else if (bossActionTimer_ <= 5.3f) {
                            // 2. 叩きつけ (5.0〜5.3秒)
                            float t = (bossActionTimer_ - 5.0f) / 0.3f;
                            currentRotY = targetRotY;
                            currentPitch = std::lerp(upPitch, strikePitch, t);
                        } else if (bossActionTimer_ <= 6.8f) {
                            // 3. 戻り (5.3〜6.8秒)
                            float t = (bossActionTimer_ - 5.3f) / 1.5f;
                            currentRotY = targetRotY - diffRotY * t;
                            currentPitch = std::lerp(strikePitch, normalPitch, t);
                        }
                    }
                }

                // 咆哮演出中 (4.0s ～ 5.0s) の前脚威嚇モーション
                if (bossAppearanceTimer_ > 0.0f) {
                    float t_appearance = 5.0f - bossAppearanceTimer_;
                    if (t_appearance >= 4.0f && t_appearance < 5.0f) {
                        float roarProgress = (t_appearance - 4.0f) / 1.0f;
                        float easeRoar = std::sin(roarProgress * static_cast<float>(M_PI));

                        if (i == 4 || i == 5) { // 右側の前脚
                            // 前脚：天高く持ち上げる ＆ 小刻みに震わせる (右足なのでマイナス方向)
                            currentPitch = std::lerp(normalPitch, -1.5f, easeRoar);
                            currentRotY += std::sin(t_appearance * 80.0f) * 0.15f * easeRoar;
                        } else {
                            // 後ろ脚：踏みしめる
                            currentPitch = std::lerp(normalPitch, 0.4f, easeRoar);
                        }
                    }
                }
                
                legRotate = { 
                    currentPitch, 
                    currentRotY, 
                    0.0f 
                };
            }

            // 足モデル自体のスケール（独立した足専用スケールを適用）
            Vector3 legScale = { bossLegScale_, bossLegScale_, bossLegScale_ };

            // 初期姿勢の回転角 (R0) の計算
            Vector3 legRotate0{};
            if (i < 4) {
                // 左足
                legRotate0 = { 
                    -0.1f, 
                    (-baseRotY) * (float)M_PI / 180.0f, 
                    0.0f 
                };
            } else {
                // 右足
                legRotate0 = { 
                    0.1f, 
                    baseRotY * (float)M_PI / 180.0f, 
                    0.0f 
                };
            }

            // 初期姿勢の回転行列 (R0) と 現在の回転行列 (R) を作成
            Matrix4x4 mRotate0 = Multiply(Multiply(MakeRotateXMatrix(legRotate0.x), MakeRotateYMatrix(legRotate0.y)), MakeRotateZMatrix(legRotate0.z));
            Matrix4x4 mRotate = Multiply(Multiply(MakeRotateXMatrix(legRotate.x), MakeRotateYMatrix(legRotate.y)), MakeRotateZMatrix(legRotate.z));

            // スケールされたローカルピボット位置 (根本)
            Vector3 scaledPivot = { 0.0f, bossLegPivotY_ * bossLegScale_, bossLegPivotZ_ * bossLegScale_ };
            Vector4 pivotV4 = { scaledPivot.x, scaledPivot.y, scaledPivot.z, 1.0f };

            // 回転による根本の変位（ズレ）を計算して相殺するベクトルを算出
            Vector4 rot0V4 = Multiply(pivotV4, mRotate0);
            Vector4 rotV4 = Multiply(pivotV4, mRotate);
            Vector3 offsetCompensation = { rot0V4.x - rotV4.x, rot0V4.y - rotV4.y, rot0V4.z - rotV4.z };

            // 以前の正常な配置座標に、ズレを打ち消す相殺ベクトルを加算
            Vector3 finalJointPos = Scale(finalOffset, bossScale_);
            finalJointPos.x += offsetCompensation.x;
            finalJointPos.y += offsetCompensation.y;
            finalJointPos.z += offsetCompensation.z;

            // 以前の正常な配置方式をベースにしつつ、ズレを打ち消した座標を適用
            Matrix4x4 legLocal = MakeAffineMatrix(legScale, legRotate, finalJointPos);

            // スケールなしの胴体行列と合成することで、胴体スケール変更が足の大きさに影響するのを防止
            Matrix4x4 legWorld = Multiply(legLocal, bossWorldNoScale);

            bossLegTransformData_[i]->World = legWorld;
        }
    }

    // 画面シェイク（カメラの振動）の適用
    Vector3 originalCamTranslate = camera_->GetTransform().translate;
    camera_->SetTranslate(Add(originalCamTranslate, cameraShakeOffset_));

    camera_->Update();
    Matrix4x4 viewProjectionMatrix = camera_->GetViewProjectionMatrix();

    camera_->SetTranslate(originalCamTranslate); // 描画後に座標を戻す

    // 蜘蛛ボスのWVP行列更新 (最新のカメラ行列を使用)
    if (bossBodyTransformData_) {
        bossBodyTransformData_->WVP = Multiply(bossBodyTransformData_->World, viewProjectionMatrix);
        for (int i = 0; i < 8; ++i) {
            if (bossLegTransformData_[i]) {
                bossLegTransformData_[i]->WVP = Multiply(bossLegTransformData_[i]->World, viewProjectionMatrix);
            }
        }
    }

    // ── 天球(SkyDome)のワールド・WVP行列を計算 ──
    // ビュー行列から平行移動成分をゼロにすることで「常に無限遠に見える」真のスカイドームを実現
    if (skydomeModel_ && skydomeTransformData_) {
        Vector3 skyScale  = { kSkydomeScale, kSkydomeScale, kSkydomeScale };
        Vector3 skyRotate = { 0.0f, 0.0f, 0.0f };
        Vector3 skyPos    = { 0.0f, 0.0f, 0.0f }; // 原点固定（ビューの平行移動除去で常にカメラ中心になる）

        // ビュー行列の平行移動成分をゼロに除去し、回転のみ残した ViewProjection を作成
        Matrix4x4 skyViewMatrix = camera_->GetViewMatrix();
        skyViewMatrix.m[3][0] = 0.0f;
        skyViewMatrix.m[3][1] = 0.0f;
        skyViewMatrix.m[3][2] = 0.0f;
        Matrix4x4 skyViewProjMatrix = Multiply(skyViewMatrix, camera_->GetProjectionMatrix());

        Matrix4x4 skyWorld = MakeAffineMatrix(skyScale, skyRotate, skyPos);
        skydomeTransformData_->World = skyWorld;
        skydomeTransformData_->WVP   = Multiply(skyWorld, skyViewProjMatrix);

        // UVスケールを適用してテクスチャを細かくタイリングする
        // これにより、星や雲が小さく（＝より遠くに）見え、テクスチャの荒さが大幅に改善されます
        float uvScale = 4.0f; 
        Matrix4x4 uvTranslate = MakeScaleMatrix({ uvScale, uvScale, 1.0f });

        // 天球マテリアルのUVトランスフォームを更新
        ID3D12Resource* materialRes = skydomeModel_->GetMaterialResource();
        if (materialRes) {
            ::Material* materialData = nullptr;
            materialRes->Map(0, nullptr, reinterpret_cast<void**>(&materialData));
            if (materialData) {
                materialData->uvTransform = uvTranslate;
            }
            materialRes->Unmap(0, nullptr);
        }
    }

    // 敵キャラのワールド行列・WVPの更新
    EulerTransform& camTransForEnemy = camera_->GetTransform();

    // 各グループの画面外判定とリポップチェック
    if (currentPhase_ != GamePhase::kBossFight) {
        // 自機の現在位置を取得
        Vector3 fighterWorldPos = { 0.0f, 0.0f, 0.0f };
        if (fighterModel_) {
            Vector3 playerRailPos = GetRailPosition(fighterWorldZ_);
            Vector3 playerRailDir = GetRailDirection(fighterWorldZ_);
            Vector3 playerRailRight = CalculateRailRight(playerRailDir);
            Vector3 playerRailUp = CalculateRailUp(playerRailDir, playerRailRight);
            float playerOffsetUp = fighterModel_->transform.translate.y + 17.0f;
            fighterWorldPos = Add(playerRailPos, Add(Scale(playerRailRight, fighterModel_->transform.translate.x), Scale(playerRailUp, playerOffsetUp)));
        }

        // グループ中心Zに基づく画面外判定と強制リポップ処理は、一括全滅リポップ制御に移行したため削除しました

        bool isPhaseIntroActive = (phaseIntroTimer_ >= 0.0f);

        // ── 雑魚敵の移動・状態更新 ──
        for (int i = 0; i < kMaxEnemies; ++i) {
            if (!enemies_[i].isAlive) continue;

            Enemy& enemy = enemies_[i];
            
            // フェーズ演出中（isPhaseIntroActive == true）は敵の動きやタイマー更新をストップ
            if (!isPhaseIntroActive) {
                enemy.stateTimer += kDeltaTime;

                if (enemy.state == Enemy::State::kSideWait) {
                    // 出現判定: レール累積距離でプレイヤーより前方300m以内に入ったら出現開始
                    float distAlongRail = enemy.railProgress - fighterWorldZ_;
                    if (distAlongRail > 0.0f && distAlongRail < 300.0f) {
                        enemy.state = Enemy::State::kAppear;
                        enemy.stateTimer = 0.0f;
                        // 出現時のレール相対距離を記録 (最大120m)
                        enemy.relativeZ = (std::min)(120.0f, distAlongRail);
                        // 出現開始時の位置を記憶
                        enemy.appearStartPos = enemy.position;
                    }
                }
                else if (enemy.state == Enemy::State::kAppear) {
                    // 2. 中央へ移動: 待機位置からフォーメーション目標位置(wanderAnchor)に向けてイージング＋カーブで合流
                    // wanderAnchorのレール累積距離を更新（プレイヤーとの相対累積距離を保持）
                    enemy.wanderAnchorRailProgress = fighterWorldZ_ + enemy.relativeZ;
                    // レール空間からワールド座標を再計算
                    {
                        Vector3 aRailPos   = GetRailPosition(enemy.wanderAnchorRailProgress);
                        Vector3 aRailDir   = GetRailDirection(enemy.wanderAnchorRailProgress);
                        Vector3 aRailRight = CalculateRailRight(aRailDir);
                        Vector3 aRailUp    = CalculateRailUp(aRailDir, aRailRight);
                        enemy.wanderAnchor = Add(aRailPos, Add(Scale(aRailRight, enemy.wanderAnchorRelX), Scale(aRailUp, enemy.wanderAnchorRelY)));
                    }

                    float kAppearDuration = 1.2f;
                    float t = std::clamp(enemy.stateTimer / kAppearDuration, 0.0f, 1.0f);
                    
                    // EaseOutQuad による滑らかな合流
                    float rate = 1.0f - (1.0f - t) * (1.0f - t);

                    // Z軸は徐々にプレイヤーとのキープ相対Zに近づける
                    float currentZ = std::lerp(enemy.appearStartPos.z, enemy.wanderAnchor.z, rate);
                    
                    // X軸は滑らかに目標フォーメーション位置へ
                    float currentX = std::lerp(enemy.appearStartPos.x, enemy.wanderAnchor.x, rate);

                    // Y軸は「上へ膨らむ山なりのカーブ」を加えてビルから飛び出す挙動にする
                    float arcHeight = 15.0f; // 飛び出しの最高到達高度
                    float heightOffset = arcHeight * std::sin(t * static_cast<float>(M_PI));
                    float currentY = std::lerp(enemy.appearStartPos.y, enemy.wanderAnchor.y, rate) + heightOffset;

                    enemy.position = { currentX, currentY, currentZ };

                    // 飛び出す方向（左から右か、右から左か）に応じて機体をローリング（傾き）させる演出
                    float rollDir = (enemy.appearStartPos.x < enemy.wanderAnchor.x) ? -0.8f : 0.8f; // 約45度傾く
                    enemy.rotate.z = rollDir * std::sin(t * static_cast<float>(M_PI)); // 合流完了で水平に戻る
                    
                    // 進行方向を向くヨー（rotate.y）
                    float yawDir = (enemy.appearStartPos.x < enemy.wanderAnchor.x) ? 0.4f : -0.4f;
                    enemy.rotate.y = yawDir * (1.0f - rate); // 合流完了で正面に戻る

                    if (t >= 1.0f) {
                        enemy.position = enemy.wanderAnchor;
                        enemy.rotate = { 0.0f, 0.0f, 0.0f }; // 回転をリセット
                        enemy.state = Enemy::State::kWander;
                        enemy.stateTimer = 0.0f;
                        enemy.wanderPhase = (float)(rand() % 100) / 10.0f;
                    }
                }
                else if (enemy.state == Enemy::State::kWander) {
                    // 3. 徘徊しながら後退: wanderAnchor の周囲を動きつつ、全体がZ軸手前(プレイヤー側)へ下がっていく
                    enemy.wanderPhase += kDeltaTime * 2.5f;
                    
                    // 相対Z距離を徐々に減らす (プレイヤーへ向けてゆっくり近づいてくる＝下がっていくように見せる)
                    // 秒速 15m で距離が縮まる (3秒で 45m 接近)
                    enemy.relativeZ -= 15.0f * kDeltaTime;
                    enemy.wanderAnchorRailProgress = fighterWorldZ_ + enemy.relativeZ;
                    // レール空間からwanderAnchorワールド座標を再計算
                    {
                        Vector3 wRailPos   = GetRailPosition(enemy.wanderAnchorRailProgress);
                        Vector3 wRailDir   = GetRailDirection(enemy.wanderAnchorRailProgress);
                        Vector3 wRailRight = CalculateRailRight(wRailDir);
                        Vector3 wRailUp    = CalculateRailUp(wRailDir, wRailRight);
                        enemy.wanderAnchor = Add(wRailPos, Add(Scale(wRailRight, enemy.wanderAnchorRelX), Scale(wRailUp, enemy.wanderAnchorRelY)));
                    }
                    
                    float radiusX = 6.0f;
                    float radiusY = 4.0f;
                    enemy.position.x = enemy.wanderAnchor.x + std::cos(enemy.wanderPhase) * radiusX;
                    enemy.position.y = enemy.wanderAnchor.y + std::sin(enemy.wanderPhase * 1.3f) * radiusY;
                    enemy.position.z = enemy.wanderAnchor.z + std::sin(enemy.wanderPhase * 0.7f) * 3.0f;

                    // 中央出現から一定時間 (3.0秒) 経過したら特攻開始
                    if (enemy.stateTimer >= 3.0f) {
                        enemy.state = Enemy::State::kDive;
                        enemy.stateTimer = 0.0f;
                        // 自機位置に向けて特攻方向を計算 (Z追従を解除してその瞬間の位置へ突進)
                        Vector3 playerTarget = fighterWorldPos;
                        Vector3 toPlayer = Subtract(playerTarget, enemy.position);

                        enemy.diveDirection = Normalize(toPlayer);
                        enemy.speed = 85.0f + boostForwardSpeed_; // プレイヤーの速度に合わせて特攻速度を上げる (ブースト対応)
                    }
                }
                else if (enemy.state == Enemy::State::kDive) {
                    // 4. 特攻状態: プレイヤーに向けて直線的に高速突進 (Z追従なし)
                    enemy.position = Add(enemy.position, Scale(enemy.diveDirection, enemy.speed * kDeltaTime));
                    
                    // 特攻中の回転
                    enemy.rotate.z += kDeltaTime * 18.0f;
                    enemy.rotate.x += kDeltaTime * 6.0f;
                    enemy.rotate.y += kDeltaTime * 4.0f;
                    
                    // プレイヤーを通り過ぎてレール後方20m超えたら非生存化してリポップ対象に
                    {
                        Vector3 localRailDir = GetRailDirection(fighterWorldZ_);
                        Vector3 toFighterRail = Subtract(enemy.position, fighterWorldPos);
                        float behindDot = toFighterRail.x * (-localRailDir.x) + toFighterRail.y * (-localRailDir.y) + toFighterRail.z * (-localRailDir.z);
                        if (behindDot > 20.0f) {
                            enemy.isAlive = false;
                        }
                    }
                }
            }
        }

        // ── 雑魚敵のグループごと個別全滅時のリポップ制御 ──
        if (currentPhase_ != GamePhase::kBossFight && !isPhaseIntroActive) {
            for (int g = 0; g < kNumGroups; ++g) {
                bool anyEnemyAliveInGroup = false;
                for (int idx = 0; idx < kEnemiesPerGroup; ++idx) {
                    int enemyIdx = g * kEnemiesPerGroup + idx;
                    if (enemies_[enemyIdx].isAlive) {
                        anyEnemyAliveInGroup = true;
                        break;
                    }
                }
                // そのグループの敵が1体もいなくなったら、そのグループを前方にリポップする
                if (!anyEnemyAliveInGroup) {
                    RespawnEnemyGroup(g, fighterWorldZ_);
                }
            }
        }

        for (int i = 0; i < kMaxEnemies; ++i) {
            if (enemies_[i].isAlive) {
                // 敵の進行方向の決定 (特攻中は特攻方向、それ以外はレール進行方向の逆＝プレイヤーと対面する方向)
                Vector3 eDir = {0.0f, 0.0f, -1.0f};
                if (enemies_[i].state == Enemy::State::kDive) {
                    eDir = enemies_[i].diveDirection;
                } else {
                    eDir = Scale(GetRailDirection(enemies_[i].wanderAnchorRailProgress), -1.0f);
                }
                
                float eDirLen = std::sqrt(eDir.x * eDir.x + eDir.y * eDir.y + eDir.z * eDir.z);
                if (eDirLen > 0.001f) {
                    eDir = Scale(eDir, 1.0f / eDirLen);
                } else {
                    eDir = {0.0f, 0.0f, -1.0f};
                }

                // モデルの初期姿勢が後ろ向きのため、正面(Zマイナス)をeDirに向かせるための基準姿勢を構築
                Vector3 d = eDir;
                Vector3 r = CalculateRailRight(d);
                Vector3 u = CalculateRailUp(d, r);

                Matrix4x4 R_enemy_base = MakeIdentity4x4();
                R_enemy_base.m[0][0] = r.x; R_enemy_base.m[0][1] = r.y; R_enemy_base.m[0][2] = r.z;
                R_enemy_base.m[1][0] = u.x; R_enemy_base.m[1][1] = u.y; R_enemy_base.m[1][2] = u.z;
                R_enemy_base.m[2][0] = d.x; R_enemy_base.m[2][1] = d.y; R_enemy_base.m[2][2] = d.z;

                // 180度反転して前を向かせる回転
                Matrix4x4 rotateY = MakeRotateYMatrix(static_cast<float>(M_PI));

                // 敵のローカル回転 (合流ローリングや特攻時スピンなど)
                Matrix4x4 rotateX_e = MakeRotateXMatrix(enemies_[i].rotate.x);
                Matrix4x4 rotateY_e = MakeRotateYMatrix(enemies_[i].rotate.y);
                Matrix4x4 rotateZ_e = MakeRotateZMatrix(enemies_[i].rotate.z);
                Matrix4x4 R_local = Multiply(Multiply(Multiply(rotateZ_e, rotateX_e), rotateY_e), rotateY);

                // ワールド行列の組み立て
                Matrix4x4 R_final = Multiply(R_local, R_enemy_base);
                Matrix4x4 scaleMatrix = MakeScaleMatrix(enemies_[i].scale);
                Matrix4x4 translateMatrix = MakeTranslateMatrix(enemies_[i].position);
                Matrix4x4 worldMatrix = Multiply(Multiply(scaleMatrix, R_final), translateMatrix);

                enemyTransformData_[i]->World = worldMatrix;
                enemyTransformData_[i]->WVP = Multiply(worldMatrix, viewProjectionMatrix);
            } else {
                // 撃破された敵は非表示にする
                enemyTransformData_[i]->World = MakeIdentity4x4();
                enemyTransformData_[i]->WVP = MakeIdentity4x4();
            }
        }
    } else {
        // ボス戦フェーズではすべての雑魚敵を完全に非表示にする
        for (int i = 0; i < kMaxEnemies; ++i) {
            enemyTransformData_[i]->World = MakeIdentity4x4();
            enemyTransformData_[i]->WVP = MakeIdentity4x4();
        }
    }

    // 戦闘機とエイミングのWVPの更新
    if (fighterModel_ && fighterTransformData_) {
        fighterTransformData_->WVP = Multiply(fighterTransformData_->World, viewProjectionMatrix);
    }
    if (sceneMode_ == SceneMode::kFighter) {
        if (aimingInstancingData_) {
            aimingInstancingData_->WVP = Multiply(aimingInstancingData_->World, viewProjectionMatrix);
            aimingInstancingData_->color = { 1.0f, 1.0f, 1.0f, 1.0f };
            aimingInstancingData_->uvTransform = MakeIdentity4x4();
        }
    }

    // 弾の描画用行列の更新
    for (int i = 0; i < kMaxBullets; ++i) {
        if (playerBullets_[i].currentTime < playerBullets_[i].lifeTime) {
            Matrix4x4 bulletWorld = MakeAffineMatrix(Vector3{ 0.5f, 0.5f, 0.5f }, Vector3{ 0.0f, 0.0f, 0.0f }, playerBullets_[i].position);
            bulletTransformData_[i]->World = bulletWorld;
            bulletTransformData_[i]->WVP = Multiply(bulletWorld, viewProjectionMatrix);
        } else {
            bulletTransformData_[i]->World = MakeIdentity4x4();
            bulletTransformData_[i]->WVP = MakeIdentity4x4();
        }
    }

    // トドメ巨大弾のWVPの更新
    if (defeatBulletTransformData_) {
        if (isDefeatBulletActive_) {
            Matrix4x4 bulletWorld = MakeAffineMatrix(
                Vector3{ defeatBulletSize_, defeatBulletSize_, defeatBulletSize_ },
                Vector3{ 0.0f, 0.0f, 0.0f },
                defeatBulletPos_
            );
            defeatBulletTransformData_->World = bulletWorld;
            defeatBulletTransformData_->WVP = Multiply(bulletWorld, viewProjectionMatrix);
        } else {
            defeatBulletTransformData_->World = MakeIdentity4x4();
            defeatBulletTransformData_->WVP = MakeIdentity4x4();
        }
    }

    if (playerModel_) {
        // アニメーションの更新 (DeltaTimeは固定60fpsとする)
        playerModel_->UpdateAnimation(kDeltaTime);
        playerModel_->Update();

        // モデル自体のトランスフォームを適用したWVP/Worldを計算
        Matrix4x4 modelWorldMatrix = MakeAffineMatrix(playerModel_->transform.scale, playerModel_->transform.rotate, playerModel_->transform.translate);
        
        transformData_->WVP = Multiply(modelWorldMatrix, viewProjectionMatrix);
        transformData_->World = modelWorldMatrix;

        if (cameraDataCB_) {
            cameraDataCB_->worldPosition = camTrans.translate;
        }
    }

    // simpleSkinのスケルトンアニメーション更新
    cubeAnimationTime_ += kDeltaTime;
    cubeAnimationTime_ = std::fmod(cubeAnimationTime_, cubeAnimation_.duration);

    // 1. アニメーションをスケルトンに適用
    AdvAnim::ApplyAnimation(cubeSkeleton_, cubeAnimation_, cubeAnimationTime_);
    // 2. スケルトンの階層行列を更新
    AdvAnim::Update(cubeSkeleton_);
    // 3. スキニングクラスターを更新
    AdvAnim::Update(cubeSkinCluster_, cubeSkeleton_);

    // 描画用のワールド行列を計算（スケルトン全体を移動させるための行列）
    cubeRenderModel_->transform.translate = Vector3{ 20.0f, 0.0f, 0.0f };
    cubeRenderModel_->transform.scale = Vector3{ 1.0f, 1.0f, 1.0f };
    Matrix4x4 cubeBaseWorldMatrix = MakeAffineMatrix(cubeRenderModel_->transform.scale, cubeRenderModel_->transform.rotate, cubeRenderModel_->transform.translate);

    // 資料の指示により、スキニングモデルのWVPにはルートジョイントの行列を含めない
    cubeTransformData_->WVP = Multiply(cubeBaseWorldMatrix, viewProjectionMatrix);
    cubeTransformData_->World = cubeBaseWorldMatrix;

    // AnimatedCubeのアニメーション更新
    animatedCubeAnimationTime_ += kDeltaTime;
    animatedCubeAnimationTime_ = std::fmod(animatedCubeAnimationTime_, animatedCubeAnimation_.duration);

    animatedCubeRenderModel_->transform.rotate.y += 0.02f;
    AdvAnim::ApplyAnimation(animatedCubeSkeleton_, animatedCubeAnimation_, animatedCubeAnimationTime_);
    AdvAnim::Update(animatedCubeSkeleton_);

    if (animatedCubeRenderModel_ && animatedCubeRenderModel_->skinningData_) {
        for (const auto& joint : animatedCubeSkeleton_.joints) {
            if (animatedCubeRenderModel_->bones_.count(joint.name)) {
                const auto& bone = animatedCubeRenderModel_->bones_.at(joint.name);
                animatedCubeRenderModel_->skinningData_->boneMatrices[bone.index] = Multiply(bone.offsetMatrix, joint.skeletonSpaceMatrix);
            }
        }
    }

    animatedCubeRenderModel_->transform.translate = Vector3{ -20.0f, 0.0f, 0.0f }; // 左側にずらす
    animatedCubeRenderModel_->transform.scale = Vector3{ 1.0f, 1.0f, 1.0f };
    Matrix4x4 animatedCubeBaseWorldMatrix = MakeAffineMatrix(animatedCubeRenderModel_->transform.scale, animatedCubeRenderModel_->transform.rotate, animatedCubeRenderModel_->transform.translate);

    animatedCubeTransformData_->WVP = Multiply(animatedCubeBaseWorldMatrix, viewProjectionMatrix);
    animatedCubeTransformData_->World = animatedCubeBaseWorldMatrix;

    // ── ビルと床(Plane)の更新・再配置（オブジェクトプール） ──
    // ボス戦中もビルの物理シミュレーション・衝突判定・行列計算を更新する。
    if (sceneMode_ == SceneMode::kFighter) {
        // ボス登場演出中は、カメラの円運動によるZ往復運動で再配置が破綻するのを防ぐため、基準Z座標を固定する
        bool isBossIntro = (currentPhase_ == GamePhase::kBossFight && bossAppearanceTimer_ > 0.0f);
        float cameraZ = isBossIntro ? (fighterWorldZ_ - 65.0f) : camera_->GetTransform().translate.z;

        // ビルの画面外再配置（自動直進コースのときのみオブジェクトプールによる再配置を行う）
        if (waypoints_.empty()) {
            for (int i = 0; i < kMaxBuildings; ++i) {
                // カメラの後方(間隔分)を超えたら、遥か前方（最前方のビルペアの先）に再配置
                if (buildings_[i].position.z < cameraZ - kBuildingInterval) {
                    buildings_[i].position.z += (float)kMaxBuildings * kBuildingInterval * 0.25f;
                    
                    // 破壊されているビルを元のきれいな状態にリセット
                    buildings_[i].position.x = buildings_[i].originalX;
                    buildings_[i].position.y = buildings_[i].originalY;
                    buildings_[i].rotate = { 0.0f, 0.0f, 0.0f };
                    buildings_[i].isDestroyed = false;
                    buildings_[i].velocity = { 0.0f, 0.0f, 0.0f };
                    buildings_[i].rotationSpeed = { 0.0f, 0.0f, 0.0f };
                    buildings_[i].destroyTimer = 0.0f;

                    buildings_[i].floors = buildings_[i].originalFloors;
                }
            }
        }

        // ── ビルの物理シミュレーションとボス衝突判定 ──
        {
            Vector3 bossPos = { 0.0f, 0.0f, 0.0f };
            Vector3 bRailRight = { 1.0f, 0.0f, 0.0f };
            Vector3 bRailDir = { 0.0f, 0.0f, 1.0f };
            bool isBossActive = (currentPhase_ == GamePhase::kBossFight && !isBossDefeatedSequence_);
            if (isBossActive) {
                float bodyBounce = std::sin(bossTime_ * 2.0f) * bossBodyBounceRange_;
                bossPos = GetBossPosition(bodyBounce, 0.0f, 0.0f); // 正しいレール空間のワールド座標を取得

                float bossProgress = fighterWorldZ_ + bossZOffset_;
                bRailDir = GetRailDirection(bossProgress);
                bRailRight = CalculateRailRight(bRailDir);
            }

            for (int i = 0; i < kMaxBuildings; ++i) {
                if (buildings_[i].isDestroyed) {
                    // すでに破壊されているビルの物理更新
                    buildings_[i].destroyTimer += kDeltaTime;

                    // 速度に従って移動
                    buildings_[i].position.x += buildings_[i].velocity.x * kDeltaTime;
                    buildings_[i].position.y += buildings_[i].velocity.y * kDeltaTime;
                    buildings_[i].position.z += buildings_[i].velocity.z * kDeltaTime;

                    // 重力を適用（落下）
                    buildings_[i].velocity.y -= 9.8f * 5.0f * kDeltaTime;

                    // 回転の更新
                    buildings_[i].rotate.x += buildings_[i].rotationSpeed.x * kDeltaTime;
                    buildings_[i].rotate.y += buildings_[i].rotationSpeed.y * kDeltaTime;
                    buildings_[i].rotate.z += buildings_[i].rotationSpeed.z * kDeltaTime;
                } else {
                    // ボスとの衝突判定（レールローカル空間での3D判定を行い、道路脇のビル誤爆を防ぐ）
                    if (isBossActive) {
                        Vector3 diff = Subtract(buildings_[i].position, bossPos);
                        diff.y = 0.0f; // 高低差を無視して判定を行う
                        float localX = diff.x * bRailRight.x + diff.y * bRailRight.y + diff.z * bRailRight.z; // 左右方向（道路幅方向）
                        float localZ = diff.x * bRailDir.x   + diff.y * bRailDir.y   + diff.z * bRailDir.z;   // 前後方向（進行方向）

                        // 進行方向（前後）に30m、道路幅（左右）に55mの範囲にあるビルを体当たり破壊（左右の外側二列目 X=±85m を誤爆から保護）
                        if (std::abs(localZ) < 30.0f && std::abs(localX) < 55.0f) {
                            buildings_[i].isDestroyed = true;
                            buildings_[i].destroyTimer = 0.0f;

                            // 吹き飛び方向
                            float signX = (buildings_[i].position.x >= 0.0f) ? 1.0f : -1.0f;

                            if (i % 2 == 0) {
                                // なぎ倒れタイプ (回転中心は底面。外側に倒れる)
                                buildings_[i].velocity = { signX * 25.0f, 15.0f, 10.0f };
                                buildings_[i].rotationSpeed = { 0.0f, 0.0f, -signX * 3.5f };
                            } else {
                                // 吹き飛びタイプ (上空に激しく吹き飛ぶ)
                                buildings_[i].velocity = { signX * 45.0f, 60.0f, 25.0f };
                                float rx = ((float)(randomEngine_() % 200) / 100.0f) - 1.0f;
                                float ry = ((float)(randomEngine_() % 200) / 100.0f) - 1.0f;
                                float rz = -signX * (1.5f + ((float)(randomEngine_() % 100) / 100.0f));
                                buildings_[i].rotationSpeed = { rx * 3.0f, ry * 3.0f, rz * 3.0f };
                            }

                            // 衝突時エフェクトの発生
                            EmitHitEffect(buildings_[i].position);
                            if (particleManager_) {
                                particleManager_->EmitCustomSparks(buildings_[i].position, 25.0f, 15, { 1.0f, 0.5f, 0.1f }, 1.0f);
                                particleManager_->EmitFlame(buildings_[i].position, 15.0f, 10, { 1.0f, 0.3f, 0.0f });
                            }
                        }
                    }
                }
            }
        }

        // 各ビルのワールド行列とWVP行列を計算（距離カリング & 1棟1ドロー統合最適化）
        {
            int cbIndex = 0;
            float camZ = camera_->GetTransform().translate.z;
            float camX = camera_->GetTransform().translate.x;

            for (size_t i = 0; i < buildings_.size(); ++i) {
                buildings_[i].cbIndexStart = -1;
                buildings_[i].cbCount = 0;

                if (buildings_[i].floors <= 0) continue;

                float dz = buildings_[i].position.z - camZ;
                float dx = buildings_[i].position.x - camX;

                // 距離カリング: カメラ後方または前方遠すぎるビルは計算・描画を完全スキップ
                if (dz < -kBuildingCullBackDist || dz > kBuildingCullFarDist) {
                    continue;
                }

                float distSq = dx * dx + dz * dz;

                // プレイヤーが通るルート近傍および直進延長上のビル(isNearCourseColumn)は視界限界(kBuildingCullFarDist)まで常に詳細描画（荒くしない）
                bool isDetailView = (buildings_[i].isNearCourseColumn && dz >= -kBuildingCullBackDist && dz <= kBuildingCullFarDist) ||
                                    buildings_[i].isDestroyed ||
                                    (distSq <= 120.0f * 120.0f);

                if (isDetailView) {
                    // 各階個別のマルチドロー（詳細描画）
                    buildings_[i].cbIndexStart = cbIndex;
                    int count = 0;

                    for (int f = 0; f < buildings_[i].floors; ++f) {
                        if (cbIndex >= kMaxBuildingCBs) break;

                        Vector3 floorPos = buildings_[i].position;
                        floorPos.y = buildings_[i].position.y + (float)f * kFloorHeight + kFloorHeight * 0.5f;

                        Matrix4x4 worldMatrix = MakeAffineMatrix(buildings_[i].scale, buildings_[i].rotate, floorPos);
                        buildingTransformData_[cbIndex]->World = worldMatrix;
                        buildingTransformData_[cbIndex]->WVP = Multiply(worldMatrix, viewProjectionMatrix);
                        cbIndex++;
                        count++;
                    }
                    buildings_[i].cbCount = count;
                } else {
                    // 遠距離または背景ダミービル: 1棟につき1ドローコールに統合（Yスケールを一括拡大して超軽量化）
                    if (cbIndex >= kMaxBuildingCBs) break;

                    buildings_[i].cbIndexStart = cbIndex;
                    buildings_[i].cbCount = 1;

                    Vector3 integratedScale = buildings_[i].scale;
                    integratedScale.y = kFloorHeight * (float)buildings_[i].floors;

                    Vector3 centerPos = buildings_[i].position;
                    centerPos.y = buildings_[i].position.y + integratedScale.y * 0.5f;

                    Matrix4x4 worldMatrix = MakeAffineMatrix(integratedScale, buildings_[i].rotate, centerPos);
                    buildingTransformData_[cbIndex]->World = worldMatrix;
                    buildingTransformData_[cbIndex]->WVP = Multiply(worldMatrix, viewProjectionMatrix);
                    cbIndex++;
                }
            }
        }

        // 床(Plane)のワールド・WVP行列を計算（scene_layout.txt から読み込んだ全タイル分）
        {
            // ベース回転: plane.obj を水平な道路にする回転 (X=90度, Y=90度)
            const Vector3 roadScale     = { kRoadDepthScale, kRoadWidthScale, 1.0f };
            const Matrix4x4 scaleMatrix = Matrix4x4MakeScaleMatrix(roadScale);
            const Matrix4x4 baseRotX    = MakeRotateXMatrix(1.57079632f);
            const Matrix4x4 baseRotY    = MakeRotateYMatrix(1.57079632f);
            const Matrix4x4 baseRotMat  = Multiply(baseRotX, baseRotY);

            for (int i = 0; i < numLoadedFloors_; ++i) {
                Matrix4x4 courseRotX   = MakeRotateXMatrix(floorRotations_[i].x);
                Matrix4x4 courseRotY   = MakeRotateYMatrix(floorRotations_[i].y);
                Matrix4x4 courseRotZ   = MakeRotateZMatrix(floorRotations_[i].z);
                Matrix4x4 courseRotMat = Multiply(Multiply(courseRotX, courseRotY), courseRotZ);
                Matrix4x4 rotMat       = Multiply(baseRotMat, courseRotMat);

                Matrix4x4 translateMatrix = MakeTranslateMatrix(floorPositions_[i]);
                Matrix4x4 worldMatrix = Multiply(Multiply(scaleMatrix, rotMat), translateMatrix);

                floorTransformData_[i]->World = worldMatrix;
                floorTransformData_[i]->WVP   = Multiply(worldMatrix, viewProjectionMatrix);
            }
        }

        // 広域地面ベース (Ground Base Plane) のワールド・WVP行列を計算 ($Y = -20.2\text{m}$ に配置してタイルの空白空間を完全隠蔽)
        if (groundBaseTransformData_) {
            const Vector3 groundScale   = { 10000.0f, 10000.0f, 1.0f };
            const Matrix4x4 scaleMatrix = Matrix4x4MakeScaleMatrix(groundScale);
            const Matrix4x4 baseRotX    = MakeRotateXMatrix(1.57079632f);
            const Matrix4x4 baseRotY    = MakeRotateYMatrix(1.57079632f);
            const Matrix4x4 rotMat      = Multiply(baseRotX, baseRotY);
            const Matrix4x4 translateMatrix = MakeTranslateMatrix({ 0.0f, -20.2f, 5000.0f });

            Matrix4x4 worldMatrix = Multiply(Multiply(scaleMatrix, rotMat), translateMatrix);
            groundBaseTransformData_->World = worldMatrix;
            groundBaseTransformData_->WVP   = Multiply(worldMatrix, viewProjectionMatrix);
        }

        // 地形(Terrain)のワールド・WVP行列を計算
        if (hasTerrain_ && terrainTransformData_) {
            // Blenderスケール 0.1 で出力されているため、ゲーム上では等倍(1.0f)のスケールにするために10倍にする
            const Vector3 terrainScale = { 10.0f, 10.0f, 10.0f };
            const Vector3 terrainRotate = { 0.0f, 0.0f, 0.0f };
            const Vector3 terrainTranslate = { 0.0f, 0.0f, 0.0f };
            
            Matrix4x4 worldMatrix = MakeAffineMatrix(terrainScale, terrainRotate, terrainTranslate);
            terrainTransformData_->World = worldMatrix;
            terrainTransformData_->WVP   = Multiply(worldMatrix, viewProjectionMatrix);
        }



    }

    // 戦闘機モードの場合のジェット噴射エミッター位置の計算
    Vector3 leftJetPos = { 0.0f, 0.0f, 0.0f };
    Vector3 rightJetPos = { 0.0f, 0.0f, 0.0f };
    Vector3 jetDirection = { 0.0f, 0.0f, 1.0f };
    fighterWorldPos = { 0.0f, 0.0f, 0.0f };
    if (sceneMode_ == SceneMode::kFighter) {
        Vector3 playerRailPos = GetRailPosition(fighterWorldZ_);
        Vector3 playerRailDir = GetRailDirection(fighterWorldZ_);
        jetDirection = playerRailDir;
        Vector3 playerRailRight = CalculateRailRight(playerRailDir);
        Vector3 playerRailUp = CalculateRailUp(playerRailDir, playerRailRight);
        
        float playerOffsetUp = fighterModel_->transform.translate.y + 17.0f;
        fighterWorldPos = Add(playerRailPos, Add(Scale(playerRailRight, fighterModel_->transform.translate.x), Scale(playerRailUp, playerOffsetUp)));
        
        // 自機の現在のロール・ピッチ角度から R_local を再構築して機体姿勢に同期させる
        float pitch = fighterModel_->transform.rotate.x;
        float yaw_180 = static_cast<float>(M_PI);
        float roll = fighterModel_->transform.rotate.z;

        Matrix4x4 rotateY = MakeRotateYMatrix(yaw_180);
        Matrix4x4 rotateX = MakeRotateXMatrix(pitch);
        Matrix4x4 rotateZ = MakeRotateZMatrix(roll);
        Matrix4x4 R_local = Multiply(Multiply(rotateZ, rotateX), rotateY);

        Matrix4x4 R_rail = MakeIdentity4x4();
        R_rail.m[0][0] = playerRailRight.x; R_rail.m[0][1] = playerRailRight.y; R_rail.m[0][2] = playerRailRight.z;
        R_rail.m[1][0] = playerRailUp.x;    R_rail.m[1][1] = playerRailUp.y;    R_rail.m[1][2] = playerRailUp.z;
        R_rail.m[2][0] = playerRailDir.x;   R_rail.m[2][1] = playerRailDir.y;   R_rail.m[2][2] = playerRailDir.z;

        Matrix4x4 R_final = Multiply(R_local, R_rail);

        // 機体のローカル回転に追従した Right, Up, Dir を抽出
        Vector3 finalRight = { R_final.m[0][0], R_final.m[0][1], R_final.m[0][2] };
        Vector3 finalUp    = { R_final.m[1][0], R_final.m[1][1], R_final.m[1][2] };
        Vector3 finalDir   = { R_final.m[2][0], R_final.m[2][1], R_final.m[2][2] };

        // 姿勢に追従した位置でジェット位置を計算
        Vector3 localOffsetLeft = Add(Scale(finalRight, -1.0f), Add(Scale(finalUp, 0.8f), Scale(finalDir, 0.0f)));
        Vector3 localOffsetRight = Add(Scale(finalRight, 0.3f), Add(Scale(finalUp, 0.8f), Scale(finalDir, 0.0f)));
        
        leftJetPos  = Add(fighterWorldPos, localOffsetLeft);
        rightJetPos = Add(fighterWorldPos, localOffsetRight);
    }

    // ── パーティクル/エフェクトエンジンの更新 ──
    particleManager_->Update(
        viewProjectionMatrix,
        camera_->GetBillboardMatrix(),
        kDeltaTime,
        camera_->GetTransform().translate.z,
        fighterWorldPos,
        isBoosting_,
        (sceneMode_ == SceneMode::kFighter && !isBossDefeatedSequence_),
        currentEffect_,
        emitterPos_,
        leftJetPos,
        rightJetPos,
        jetDirection
    );

    {
        D3D12_RESOURCE_DESC desc = textTextureResource_->GetDesc();
        float imageWidth = (float)desc.Width;
        float imageHeight = (float)desc.Height;
        Vector3 scale = { imageWidth, imageHeight, 1.0f };
        Vector3 rotate = { 0.0f, 0.0f, 0.0f };
        float halfClientW = (float)WinApp::kClientWidth / 2.0f;
        float halfClientH = (float)WinApp::kClientHeight / 2.0f;
        Vector3 translate = {
            -halfClientW + (imageWidth / 2.0f) + spritePos_.x,
            -halfClientH + (imageHeight / 2.0f) + spritePos_.y,
            0.0f
        };
        Matrix4x4 worldSprite = MakeAffineMatrix(scale, rotate, translate);
        Matrix4x4 projectionSprite = MakeOrthographicMatrix(-halfClientW, halfClientH, halfClientW, -halfClientH, 0.0f, 100.0f);
        Matrix4x4 viewSprite = MakeIdentity4x4();
        Matrix4x4 viewProjSprite = Multiply(viewSprite, projectionSprite);

        spriteInstancingData_[0].World = worldSprite;
        spriteInstancingData_[0].WVP = Multiply(worldSprite, viewProjSprite);
        spriteInstancingData_[0].color = { 1.0f, 1.0f, 1.0f, 1.0f };
    }

    // ── HPバーの更新 ──
    if (hpBarInstancingData_) {
        float halfClientW = (float)WinApp::kClientWidth / 2.0f;
        float halfClientH = (float)WinApp::kClientHeight / 2.0f;
        Matrix4x4 projectionSprite = MakeOrthographicMatrix(-halfClientW, halfClientH, halfClientW, -halfClientH, 0.0f, 100.0f);
        Matrix4x4 viewProjSprite = Multiply(MakeIdentity4x4(), projectionSprite);

        // 共通設定
        for (uint32_t i = 0; i < kHpBarInstanceCount; ++i) {
            hpBarInstancingData_[i].uvTransform = MakeIdentity4x4();
        }

        // 1. プレイヤーHPバー背景（インデックス0）
        {
            Vector3 scale = { 200.0f, 20.0f, 1.0f };
            // 左上基準配置 (左端を -halfClientW + 30px に固定)
            Vector3 translate = { -halfClientW + 30.0f + scale.x, halfClientH - 30.0f - (scale.y / 2.0f), 0.1f };
            Matrix4x4 world = MakeAffineMatrix(scale, Vector3{0, 0, 0}, translate);
            hpBarInstancingData_[0].World = world;
            hpBarInstancingData_[0].WVP = Multiply(world, viewProjSprite);
            hpBarInstancingData_[0].color = { 0.1f, 0.1f, 0.1f, 0.8f }; // ダークグレー
        }

        // 2. プレイヤーHPバー前景（インデックス1）
        {
            // 滑らかなHPVisual_を使用
            float hpRatio = std::clamp(playerHPVisual_ / playerMaxHP_, 0.0f, 1.0f);
            Vector3 scale = { 196.0f * hpRatio, 16.0f, 1.0f };
            // 背景の左端から4px内側に左端を固定
            Vector3 translate = { -halfClientW + 34.0f + scale.x, halfClientH - 32.0f - (scale.y / 2.0f), 0.0f };
            if (scale.x <= 0.0f) scale.x = 0.0001f;
            Matrix4x4 world = MakeAffineMatrix(scale, Vector3{0, 0, 0}, translate);
            hpBarInstancingData_[1].World = world;
            hpBarInstancingData_[1].WVP = Multiply(world, viewProjSprite);
            hpBarInstancingData_[1].color = { 0.0f, 1.0f, 0.2f, 1.0f }; // 鮮やかな緑
        }

        // 3. ボスHPバー外枠（インデックス2）
        {
            Vector3 scale = { 350.0f, 24.0f, 1.0f };
            // 右上基準配置 (右端を halfClientW - 30px に固定)
            Vector3 translate = { halfClientW - 30.0f - scale.x, halfClientH - 30.0f - (scale.y / 2.0f), 0.2f }; // 最背面
            Matrix4x4 world = MakeAffineMatrix(scale, Vector3{0, 0, 0}, translate);
            hpBarInstancingData_[2].World = world;
            hpBarInstancingData_[2].WVP = Multiply(world, viewProjSprite);
            hpBarInstancingData_[2].color = { 0.7f, 0.7f, 0.7f, 1.0f }; // ライトグレーの枠線
        }

        // 4. ボスHPバー内側背景（インデックス3）
        {
            Vector3 scale = { 346.0f, 20.0f, 1.0f };
            // 外枠から内側に2pxずつマージンをとって配置
            Vector3 translate = { halfClientW - 32.0f - scale.x, halfClientH - 32.0f - (scale.y / 2.0f), 0.1f }; // 中間
            Matrix4x4 world = MakeAffineMatrix(scale, Vector3{0, 0, 0}, translate);
            hpBarInstancingData_[3].World = world;
            hpBarInstancingData_[3].WVP = Multiply(world, viewProjSprite);
            hpBarInstancingData_[3].color = { 0.05f, 0.05f, 0.05f, 0.95f }; // ほぼ黒のダークグレー（減少部分＝後ろの背景を消す目的）
        }

        // 5. ボスHPバー前景 (残HP)（インデックス4）
        {
            // 滑らかなbossHPVisual_を使用
            float hpRatio = std::clamp(bossHPVisual_ / bossMaxHP_, 0.0f, 1.0f);
            Vector3 scale = { 346.0f * hpRatio, 20.0f, 1.0f };
            // 背景の右端から4px内側に右端を固定 (左端が右へ向けて縮む ＝ 左から減る)
            Vector3 translate = { (halfClientW - 32.0f) - scale.x, halfClientH - 32.0f - (scale.y / 2.0f), 0.0f }; // 最前面
            if (scale.x <= 0.0f) scale.x = 0.0001f;
            Matrix4x4 world = MakeAffineMatrix(scale, Vector3{0, 0, 0}, translate);
            hpBarInstancingData_[4].World = world;
            hpBarInstancingData_[4].WVP = Multiply(world, viewProjSprite);
            hpBarInstancingData_[4].color = { 0.8f, 0.1f, 0.1f, 1.0f }; // 赤 (残HP)
        }
    }

    // ── 蜘蛛の巣弾（3Dビルボード）の更新・行列計算 ──
    if (webBulletInstancingData_) {
        Matrix4x4 viewProj = camera_->GetViewProjectionMatrix();
        Matrix4x4 billboard = camera_->GetBillboardMatrix();

        for (int i = 0; i < kMaxWebBullets; ++i) {
            if (bossWebBullets_[i].isAlive) {
                Vector3 scale = { bossWebBullets_[i].radius, bossWebBullets_[i].radius, 1.0f };
                Matrix4x4 world = Multiply(MakeScaleMatrix(scale), billboard);
                world.m[3][0] = bossWebBullets_[i].position.x;
                world.m[3][1] = bossWebBullets_[i].position.y;
                world.m[3][2] = bossWebBullets_[i].position.z;

                webBulletInstancingData_[i].World = world;
                webBulletInstancingData_[i].WVP = Multiply(world, viewProj);
                webBulletInstancingData_[i].color = { 1.0f, 1.0f, 1.0f, 1.0f };
                webBulletInstancingData_[i].uvTransform = MakeIdentity4x4();
            } else {
                webBulletInstancingData_[i].World = MakeIdentity4x4();
                webBulletInstancingData_[i].WVP = MakeIdentity4x4();
                webBulletInstancingData_[i].color.w = 0.0f;
            }
        }
    }

    // ── 画面蜘蛛の巣（2Dスプライト）の更新・行列計算 ──
    if (screenWebTransformData_) {
        if (screenWebTimer_ > 0.0f) {
            float halfClientW = (float)WinApp::kClientWidth / 2.0f;
            float halfClientH = (float)WinApp::kClientHeight / 2.0f;
            Matrix4x4 projectionSprite = MakeOrthographicMatrix(-halfClientW, halfClientH, halfClientW, -halfClientH, 0.0f, 100.0f);
            Matrix4x4 viewProjSprite = Multiply(MakeIdentity4x4(), projectionSprite);

            Vector3 scale = { 650.0f, 650.0f, 1.0f };
            Vector3 translate = { 0.0f, 0.0f, 0.0f }; 
            Matrix4x4 world = MakeAffineMatrix(scale, Vector3{0,0,0}, translate);

            float alpha = std::clamp(screenWebTimer_ / 5.0f, 0.0f, 0.8f);

            screenWebTransformData_->World = world;
            screenWebTransformData_->WVP = Multiply(world, viewProjSprite);
            screenWebTransformData_->color = { 1.0f, 1.0f, 1.0f, alpha };
            screenWebTransformData_->uvTransform = MakeIdentity4x4();
        } else {
            screenWebTransformData_->World = MakeIdentity4x4();
            screenWebTransformData_->WVP = MakeIdentity4x4();
            screenWebTransformData_->color = { 1.0f, 1.0f, 1.0f, 0.0f };
        }
    }

    if (gpuParticleManager_) {
        gpuParticleManager_->Update(camera_->GetViewProjectionMatrix(), camera_->GetBillboardMatrix(), kDeltaTime);
    }

#ifdef USE_IMGUI
    ImGui::Begin("PostProcess");
    const char* items[] = { "None", "Grayscale", "Sepia", "Vignette", "BoxFilter", "Outline", "RadialBlur", "Dissolve", "Random" };
    int currentItem = static_cast<int>(activePostProcess_);
    if (ImGui::Combo("Effect", &currentItem, items, IM_ARRAYSIZE(items))) {
        activePostProcess_ = static_cast<PostProcessType>(currentItem);
    }
    if (activePostProcess_ == kRandom) {
        ImGui::SliderFloat("Noise Scale", &randomNoiseScale_, 1.0f, 1000.0f);
        ImGui::SliderFloat("Noise Strength", &randomNoiseStrength_, 0.0f, 1.0f);
        ImGui::SliderFloat("Time Speed", &randomSpeed_, 0.0f, 10.0f);
        ImGui::Checkbox("Color Noise", &randomIsColorNoise_);
        
        const char* noiseTypeItems[] = { "TV Static", "Multiply" };
        ImGui::Combo("Noise Type", &randomNoiseType_, noiseTypeItems, IM_ARRAYSIZE(noiseTypeItems));
        
        ImGui::Text("Random Effect Time: %.2f", randomEffectTime_);
    }
    if (activePostProcess_ == kVignette) {
        ImGui::SliderFloat("Vignette Scale", &vignetteParamData_->scale, 0.0f, 32.0f);
        ImGui::SliderFloat("Vignette Power", &vignetteParamData_->power, 0.0f, 5.0f);
    }
    if (activePostProcess_ == kBoxFilter) {
        ImGui::SliderInt("Kernel Size (k)", &boxFilterParamData_->kernelSize, 1, 10);
    }
    if (activePostProcess_ == kRadialBlur) {
        ImGui::SliderFloat2("Center", &radialBlurParamData_->center.x, 0.0f, 1.0f);
        ImGui::SliderFloat("Blur Width", &radialBlurParamData_->blurWidth, 0.0f, 0.1f);
    }
    if (activePostProcess_ == kDissolve) {
        ImGui::SliderFloat("Threshold", &dissolveParamData_->threshold, 0.0f, 1.0f);
        ImGui::ColorEdit3("EdgeColor", &dissolveParamData_->edgeColor.x);
        ImGui::SliderFloat("EdgeRange", &dissolveParamData_->edgeRange, 0.0f, 0.1f);
        const char* noiseItems[] = { "noise0", "noise1" };
        if (ImGui::Combo("Mask Texture", &selectedNoiseIndex_, noiseItems, IM_ARRAYSIZE(noiseItems))) {
            activeNoiseSrvIndex_ = (selectedNoiseIndex_ == 0) ? noise0SrvIndex_ : noise1SrvIndex_;
        }
    }
    ImGui::End();
#endif
}

void GamePlayScene::Draw() {
    if (isDemoMode_) {
        DrawDemo();
        return;
    }

    ID3D12GraphicsCommandList* commandList = dxCommon_->GetCommandList();

    // --- 1. RenderTextureへの描画開始 ---
    postProcess_->PreDraw();

    // スカイボックスの描画 (dds天球を完全に非表示にする)
    /*
    if (skybox_ && showSkybox_) {
        ID3D12DescriptorHeap* heaps[] = { TextureManager::GetInstance()->GetSrvHeap() };
        commandList->SetDescriptorHeaps(1, heaps);

        if (graphicsPipeline_ && graphicsPipeline_->GetSkyboxPipelineState()) {
            commandList->SetPipelineState(graphicsPipeline_->GetSkyboxPipelineState());
            commandList->SetGraphicsRootSignature(graphicsPipeline_->GetRootSignature());

            // カメラのビュー行列を取得し、平行移動成分をゼロにする
            Matrix4x4 viewMatrix = camera_->GetViewMatrix();
            viewMatrix.m[3][0] = 0.0f;
            viewMatrix.m[3][1] = 0.0f;
            viewMatrix.m[3][2] = 0.0f;

            // 進行感を出すために、時間経過でゆっくりY軸周りに自動回転させる
            float rotationAngle = randomEffectTime_ * 0.03f;
            Matrix4x4 rotateY = MakeRotateYMatrix(rotationAngle);
            Matrix4x4 skyboxViewMatrix = Multiply(rotateY, viewMatrix);

            Matrix4x4 projectionMatrix = camera_->GetProjectionMatrix();
            Matrix4x4 wvpMatrix = Multiply(skyboxViewMatrix, projectionMatrix);

            int index = std::clamp(skyboxType_, 0, static_cast<int>(kNumSkyboxTextures) - 1);
            std::string skyboxTexName = kSkyboxTextures[index];
            // 安全対策: 指定されたテクスチャがキューブマップでない場合は、有効なキューブマップにフォールバックする
            if (!TextureManager::GetInstance()->GetMetaData(skyboxTexName).IsCubemap()) {
                skyboxTexName = "rostock_laage_airport_4k.dds";
            }
            D3D12_GPU_DESCRIPTOR_HANDLE skyboxSrvHandle = TextureManager::GetInstance()->GetSrvHandleGPU(skyboxTexName);

            skybox_->Draw(commandList, wvpMatrix, skyboxSrvHandle);
        }
    }
    */

    // 1. キャラクターモデルの描画 (playerModel_)
    if (showSimpleSkin_ && playerModel_) {
        ID3D12DescriptorHeap* modelHeaps[] = { TextureManager::GetInstance()->GetSrvHeap() };
        commandList->SetDescriptorHeaps(1, modelHeaps);

        // パイプラインが生成されているか念のためチェック
        if (graphicsPipeline_ && graphicsPipeline_->GetObject3dPipelineState() && graphicsPipeline_->GetObject3dRootSignature()) {
            commandList->SetPipelineState(graphicsPipeline_->GetObject3dPipelineState());
            commandList->SetGraphicsRootSignature(graphicsPipeline_->GetObject3dRootSignature());

            // 行列、ライト、カメラの定数バッファをセット
            if (transformResource_) commandList->SetGraphicsRootConstantBufferView(1, transformResource_->GetGPUVirtualAddress());
            if (directionalLightResource_) commandList->SetGraphicsRootConstantBufferView(4, directionalLightResource_->GetGPUVirtualAddress());
            if (cameraResource_) commandList->SetGraphicsRootConstantBufferView(5, cameraResource_->GetGPUVirtualAddress());

            playerModel_->DrawModel(
                commandList,
                TextureManager::GetInstance()->GetSrvHandleGPU("human/white.png"),
                TextureManager::GetInstance()->GetSrvHandleGPU("test.dds")
            );
        }
    }

    // --- 3Dオブジェクト（戦闘機/デバッグカメラモード共通）の描画 ---
    if (sceneMode_ == SceneMode::kFighter || sceneMode_ == SceneMode::kCamera) {
        ID3D12DescriptorHeap* modelHeaps[] = { TextureManager::GetInstance()->GetSrvHeap() };
        commandList->SetDescriptorHeaps(1, modelHeaps);
        if (graphicsPipeline_ && graphicsPipeline_->GetObject3dPipelineState() && graphicsPipeline_->GetObject3dRootSignature()) {
            commandList->SetPipelineState(graphicsPipeline_->GetObject3dPipelineState());
            commandList->SetGraphicsRootSignature(graphicsPipeline_->GetObject3dRootSignature());

            // 自機(Fighter)描画
            if (fighterModel_) {
                if (fighterTransformResource_) commandList->SetGraphicsRootConstantBufferView(1, fighterTransformResource_->GetGPUVirtualAddress());
                if (directionalLightResource_) commandList->SetGraphicsRootConstantBufferView(4, directionalLightResource_->GetGPUVirtualAddress());
                if (cameraResource_) commandList->SetGraphicsRootConstantBufferView(5, cameraResource_->GetGPUVirtualAddress());
                fighterModel_->DrawModel(commandList, TextureManager::GetInstance()->GetSrvHandleGPU("Player2/Player_basecolor.JPEG"), TextureManager::GetInstance()->GetSrvHandleGPU("test.dds"));
            }

            // 床(Plane)描画
            if (floorModel_) {
                if (directionalLightResource_) commandList->SetGraphicsRootConstantBufferView(4, directionalLightResource_->GetGPUVirtualAddress());
                if (cameraResource_) commandList->SetGraphicsRootConstantBufferView(5, cameraResource_->GetGPUVirtualAddress());

                // 広域地面ベース ($Y = -20.2\text{m}$) を先に描画し、タイルのない隙間や背景漏れを隠蔽
                if (groundBaseTransformResource_) {
                    commandList->SetGraphicsRootConstantBufferView(1, groundBaseTransformResource_->GetGPUVirtualAddress());
                    floorModel_->DrawModel(commandList, TextureManager::GetInstance()->GetSrvHandleGPU("douro.jpg"), TextureManager::GetInstance()->GetSrvHandleGPU("test.dds"));
                }

                float camZ = camera_->GetTransform().translate.z;
                for (int i = 0; i < numLoadedFloors_; ++i) {
                    float dz = floorPositions_[i].z - camZ;
                    // 床タイルの距離カリング（画面外/遠方カリング）
                    if (dz < -kFloorCullBackDist || dz > kFloorCullFarDist) continue;

                    commandList->SetGraphicsRootConstantBufferView(1, floorTransformResources_[i]->GetGPUVirtualAddress());
                    floorModel_->DrawModel(commandList, TextureManager::GetInstance()->GetSrvHandleGPU("douro.jpg"), TextureManager::GetInstance()->GetSrvHandleGPU("test.dds"));
                }
            }

            // 地形(Terrain)描画
            if (hasTerrain_ && terrainModel_ && terrainTransformResource_) {
                if (directionalLightResource_) commandList->SetGraphicsRootConstantBufferView(4, directionalLightResource_->GetGPUVirtualAddress());
                if (cameraResource_) commandList->SetGraphicsRootConstantBufferView(5, cameraResource_->GetGPUVirtualAddress());
                commandList->SetGraphicsRootConstantBufferView(1, terrainTransformResource_->GetGPUVirtualAddress());
                terrainModel_->DrawModel(commandList, TextureManager::GetInstance()->GetSrvHandleGPU("human/white.png"), TextureManager::GetInstance()->GetSrvHandleGPU("test.dds"));
            }



            // 天球(SkyDome)描画 ── 専用パイプライン（Zバッファ書き込みなし・常に最遠面・ぼかしサンプラー）で描画
            if (skydomeModel_ && skydomeTransformRes_ && graphicsPipeline_ && graphicsPipeline_->GetSkydomePipelineState()) {
                // 天球専用パイプラインとルートシグネチャに切り替える
                commandList->SetPipelineState(graphicsPipeline_->GetSkydomePipelineState());
                commandList->SetGraphicsRootSignature(graphicsPipeline_->GetSkydomeRootSignature());
                if (directionalLightResource_) commandList->SetGraphicsRootConstantBufferView(4, directionalLightResource_->GetGPUVirtualAddress());
                if (cameraResource_) commandList->SetGraphicsRootConstantBufferView(5, cameraResource_->GetGPUVirtualAddress());
                commandList->SetGraphicsRootConstantBufferView(1, skydomeTransformRes_->GetGPUVirtualAddress());
                int index = std::clamp(skyboxType_, 0, static_cast<int>(kNumSkyboxTextures) - 1);
                std::string skyTex = kSkyboxTextures[index];
                skydomeModel_->DrawModel(commandList, TextureManager::GetInstance()->GetSrvHandleGPU(skyTex), TextureManager::GetInstance()->GetSrvHandleGPU("test.dds"));
                // 天球描画後に Object3d パイプラインに戻す
                commandList->SetPipelineState(graphicsPipeline_->GetObject3dPipelineState());
                commandList->SetGraphicsRootSignature(graphicsPipeline_->GetObject3dRootSignature());
            }

            // ビル(Building)描画（距離カリング & 1棟1ドローコール統合により超軽量化）
            if (buildingModel_) {
                if (directionalLightResource_) commandList->SetGraphicsRootConstantBufferView(4, directionalLightResource_->GetGPUVirtualAddress());
                if (cameraResource_) commandList->SetGraphicsRootConstantBufferView(5, cameraResource_->GetGPUVirtualAddress());

                for (size_t i = 0; i < buildings_.size(); ++i) {
                    if (buildings_[i].cbIndexStart < 0 || buildings_[i].cbCount <= 0) continue;

                    for (int c = 0; c < buildings_[i].cbCount; ++c) {
                        int cbIdx = buildings_[i].cbIndexStart + c;
                        if (cbIdx >= kMaxBuildingCBs) break;
                        commandList->SetGraphicsRootConstantBufferView(1, buildingTransformResources_[cbIdx]->GetGPUVirtualAddress());
                        buildingModel_->DrawModel(commandList, TextureManager::GetInstance()->GetSrvHandleGPU("building/buillding_uv.png"), TextureManager::GetInstance()->GetSrvHandleGPU("test.dds"));
                    }
                }
            }

            // 弾の描画
            if (debugSphereModel_) {
                for (int i = 0; i < kMaxBullets; ++i) {
                    if (playerBullets_[i].currentTime < playerBullets_[i].lifeTime) {
                        commandList->SetGraphicsRootConstantBufferView(1, bulletTransformResources_[i]->GetGPUVirtualAddress());
                        debugSphereModel_->DrawModel(commandList, TextureManager::GetInstance()->GetSrvHandleGPU("human/white.png"), TextureManager::GetInstance()->GetSrvHandleGPU("test.dds"));
                    }
                }
            }

            // トドメ巨大弾の描画
            if (isDefeatBulletActive_ && debugSphereModel_ && defeatBulletTransformResource_) {
                commandList->SetGraphicsRootConstantBufferView(1, defeatBulletTransformResource_->GetGPUVirtualAddress());
                debugSphereModel_->DrawModel(commandList, TextureManager::GetInstance()->GetSrvHandleGPU("human/white.png"), TextureManager::GetInstance()->GetSrvHandleGPU("test.dds"));
            }

            // 敵の描画（プレイヤーと同じ戦闘機モデルを使用）
            if (enemyModel_ && showEnemies_ && currentPhase_ != GamePhase::kBossFight) {
                for (int i = 0; i < kMaxEnemies; ++i) {
                    if (enemies_[i].isAlive && enemies_[i].state != Enemy::State::kSideWait) {
                        commandList->SetGraphicsRootConstantBufferView(1, enemyTransformResources_[i]->GetGPUVirtualAddress());
                        enemyModel_->DrawModel(commandList, TextureManager::GetInstance()->GetSrvHandleGPU("Player/player.png"), TextureManager::GetInstance()->GetSrvHandleGPU("test.dds"));
                    }
                }
            }

            // 蜘蛛ボス（Big Spider）の描画
            if (currentPhase_ == GamePhase::kBossFight && isBossModelVisible_) {
                float bossWorldZ = fighterWorldZ_ + bossZOffset_;
                float cameraWorldZ = camera_->GetTransform().translate.z;

                // ボス登場演出中、またはボスがカメラより一定以上後ろ（手前）に通り過ぎていない場合のみ描画
                bool isBossIntro = (currentPhase_ == GamePhase::kBossFight && bossAppearanceTimer_ > 0.0f);
                if (isBossIntro || bossWorldZ >= cameraWorldZ - 20.0f) {
                    if (directionalLightResource_) commandList->SetGraphicsRootConstantBufferView(4, directionalLightResource_->GetGPUVirtualAddress());
                    if (cameraResource_) commandList->SetGraphicsRootConstantBufferView(5, cameraResource_->GetGPUVirtualAddress());

                    // 胴体の描画 (常にオリジナル高画質モデルを使用)
                    if (bossBodyModel_ && bossBodyTransformResource_) {
                        commandList->SetGraphicsRootConstantBufferView(1, bossBodyTransformResource_->GetGPUVirtualAddress());
                        bossBodyModel_->DrawModel(
                            commandList,
                            TextureManager::GetInstance()->GetSrvHandleGPU("big Spider/big+Spider_basecolor.jpg"),
                            TextureManager::GetInstance()->GetSrvHandleGPU("test.dds")
                        );
                    }

                    // 足の描画（常にオリジナル高画質モデルを使用 ＆ バインドを1回に集約して高速化）
                    if (bossLegModel_) {
                        D3D12_VERTEX_BUFFER_VIEW vbView = bossLegModel_->GetVertexBufferView();
                        D3D12_INDEX_BUFFER_VIEW ibView = bossLegModel_->GetIndexBufferView();
                        UINT indexCount = bossLegModel_->GetIndexCount();
                        
                        commandList->IASetVertexBuffers(0, 1, &vbView);
                        commandList->IASetIndexBuffer(&ibView);
                        commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
                        commandList->SetGraphicsRootConstantBufferView(0, bossLegModel_->GetMaterialResource()->GetGPUVirtualAddress());
                        
                        D3D12_GPU_DESCRIPTOR_HANDLE texHandle = TextureManager::GetInstance()->GetSrvHandleGPU("big Spider/big+spider+arm_basecolor.jpg");
                        D3D12_GPU_DESCRIPTOR_HANDLE envHandle = TextureManager::GetInstance()->GetSrvHandleGPU("test.dds");
                        commandList->SetGraphicsRootDescriptorTable(2, texHandle);
                        commandList->SetGraphicsRootDescriptorTable(3, envHandle);

                        for (int i = 0; i < 8; ++i) {
                            if (bossLegTransformResources_[i]) {
                                commandList->SetGraphicsRootConstantBufferView(1, bossLegTransformResources_[i]->GetGPUVirtualAddress());
                                commandList->DrawIndexedInstanced(indexCount, 1, 0, 0, 0);
                            }
                        }
                    }
                }
            }
        }
        
        // エイミング(レティクル)の描画 (ボス登場演出中は非表示)
        bool isBossIntro = (currentPhase_ == GamePhase::kBossFight && bossAppearanceTimer_ > 0.0f);
        if (!isBossIntro && graphicsPipeline_ && graphicsPipeline_->GetRootSignature()) {
            commandList->SetGraphicsRootSignature(graphicsPipeline_->GetRootSignature());
            commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
            if (graphicsPipeline_->GetPipelineState(kBlendModeNormal)) {
                commandList->SetPipelineState(graphicsPipeline_->GetPipelineState(kBlendModeNormal));
                if (particleModel_) {
                    particleModel_->Draw(commandList, 1, TextureManager::GetInstance()->GetSrvHandleGPU("aiming.png"), aimingInstancingSrvHandleGPU_);
                }
            }
        }

        // 蜘蛛の巣弾（3Dビルボード）の描画
        if (currentPhase_ == GamePhase::kBossFight && graphicsPipeline_ && graphicsPipeline_->GetRootSignature()) {
            commandList->SetGraphicsRootSignature(graphicsPipeline_->GetRootSignature());
            commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
            if (graphicsPipeline_->GetPipelineState(kBlendModeNormal)) {
                commandList->SetPipelineState(graphicsPipeline_->GetPipelineState(kBlendModeNormal));
                if (particleModel_) {
                    particleModel_->Draw(commandList, kMaxWebBullets, spiderWebSrvHandleGPU_, webBulletInstancingSrvHandleGPU_);
                }
            }
        }
    }

    // simpleSkinの描画
    if (showSimpleSkin_ && cubeRenderModel_ && graphicsPipeline_ && graphicsPipeline_->GetSkinningPipelineState()) {
        ID3D12DescriptorHeap* modelHeaps[] = { TextureManager::GetInstance()->GetSrvHeap() };
        commandList->SetDescriptorHeaps(1, modelHeaps);
        commandList->SetPipelineState(graphicsPipeline_->GetSkinningPipelineState());
        commandList->SetGraphicsRootSignature(graphicsPipeline_->GetSkinningRootSignature());

        if (cubeTransformResource_) commandList->SetGraphicsRootConstantBufferView(1, cubeTransformResource_->GetGPUVirtualAddress());
        if (directionalLightResource_) commandList->SetGraphicsRootConstantBufferView(5, directionalLightResource_->GetGPUVirtualAddress());
        if (cameraResource_) commandList->SetGraphicsRootConstantBufferView(6, cameraResource_->GetGPUVirtualAddress());

        cubeRenderModel_->DrawSkinningModel(
            commandList,
            cubeSkinCluster_,
            TextureManager::GetInstance()->GetSrvHandleGPU("simpleSkin/uvChecker.png"),
            TextureManager::GetInstance()->GetSrvHandleGPU("test.dds"),
            cubeTransformResource_->GetGPUVirtualAddress(),
            directionalLightResource_->GetGPUVirtualAddress(),
            cameraResource_->GetGPUVirtualAddress()
        );
    }

    // AnimatedCubeの描画
    if (showAnimatedCube_ && animatedCubeRenderModel_ && graphicsPipeline_ && graphicsPipeline_->GetObject3dPipelineState()) {
        ID3D12DescriptorHeap* modelHeaps[] = { TextureManager::GetInstance()->GetSrvHeap() };
        commandList->SetDescriptorHeaps(1, modelHeaps);
        commandList->SetPipelineState(graphicsPipeline_->GetObject3dPipelineState());
        commandList->SetGraphicsRootSignature(graphicsPipeline_->GetObject3dRootSignature());

        if (animatedCubeTransformResource_) commandList->SetGraphicsRootConstantBufferView(1, animatedCubeTransformResource_->GetGPUVirtualAddress());
        if (directionalLightResource_) commandList->SetGraphicsRootConstantBufferView(4, directionalLightResource_->GetGPUVirtualAddress());
        if (cameraResource_) commandList->SetGraphicsRootConstantBufferView(5, cameraResource_->GetGPUVirtualAddress());

        animatedCubeRenderModel_->DrawModel(
            commandList,
            TextureManager::GetInstance()->GetSrvHandleGPU("AnimatedCube/AnimatedCube_BaseColor.png"),
            TextureManager::GetInstance()->GetSrvHandleGPU("test.dds")
        );
    }

    // --- スケルトンのデバッグ描画 ---
    if (showSimpleSkin_ && graphicsPipeline_ && graphicsPipeline_->GetObject3dPipelineState() && graphicsPipeline_->GetObject3dRootSignature()) {
        commandList->SetPipelineState(graphicsPipeline_->GetObject3dPipelineState());
        commandList->SetGraphicsRootSignature(graphicsPipeline_->GetObject3dRootSignature());
        if (directionalLightResource_) commandList->SetGraphicsRootConstantBufferView(4, directionalLightResource_->GetGPUVirtualAddress());
        if (cameraResource_) commandList->SetGraphicsRootConstantBufferView(5, cameraResource_->GetGPUVirtualAddress());
        Matrix4x4 debugBaseWorld = MakeAffineMatrix(cubeRenderModel_->transform.scale, cubeRenderModel_->transform.rotate, { 40.0f, 0.0f, 0.0f });
        DrawSkeleton(cubeSkeleton_, debugBaseWorld);
    }

    // 3. パーティクルの描画（半透明なので最後に描画）
    if (showParticles_) {
        // パイプラインとルートシグネチャのセット
        if (graphicsPipeline_ && graphicsPipeline_->GetRootSignature()) {
            commandList->SetGraphicsRootSignature(graphicsPipeline_->GetRootSignature());
            commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

            BlendMode blendMode = useAdditiveBlend_ ? kBlendModeAdd : kBlendModeNormal;
            if (graphicsPipeline_->GetPipelineState(blendMode)) {
                commandList->SetPipelineState(graphicsPipeline_->GetPipelineState(blendMode));
                
                // 自作パーティクルエンジンの描画呼び出し
                particleManager_->Draw(
                    commandList,
                    particleModel_.get(),
                    ringModel_.get(),
                    cylinderModel_.get(),
                    textureSrvHandleGPU_,
                    gradationSrvHandleGPU_,
                    textSrvHandleGPU_
                );
            }

            // スプライト（テキスト）の描画
            if (graphicsPipeline_->GetPipelineState(kBlendModeNormal)) {
                commandList->SetPipelineState(graphicsPipeline_->GetPipelineState(kBlendModeNormal));
                if (particleModel_) {
                    particleModel_->Draw(commandList, kSpriteInstanceCount, textSrvHandleGPU_, spriteInstancingSrvHandleGPU_);
                }
            }

            // HPバーの描画
            if (graphicsPipeline_->GetPipelineState(kBlendModeNormal)) {
                commandList->SetPipelineState(graphicsPipeline_->GetPipelineState(kBlendModeNormal));
                if (particleModel_) {
                    // ボスフェーズのときはボスHPバーも含めた5インスタンス、それ以外はプレイヤーHPバーのみの2インスタンスを描画
                    UINT hpBarDrawCount = (currentPhase_ == GamePhase::kBossFight) ? 5 : 2;
                    particleModel_->Draw(
                        commandList, 
                        hpBarDrawCount, 
                        TextureManager::GetInstance()->GetSrvHandleGPU("human/white.png"), 
                        hpBarInstancingSrvHandleGPU_
                    );
                }
            }

            // 画面蜘蛛の巣の描画（2D）
            if (screenWebTimer_ > 0.0f && graphicsPipeline_->GetPipelineState(kBlendModeNormal)) {
                commandList->SetPipelineState(graphicsPipeline_->GetPipelineState(kBlendModeNormal));
                if (particleModel_) {
                    particleModel_->Draw(commandList, 1, spiderWebSrvHandleGPU_, screenWebSrvHandleGPU_);
                }
            }

            // フェーズ演出スプライトの描画 (PHASE 1 / 2)
            if (phaseIntroTimer_ >= 0.0f) {
                float halfClientW = (float)WinApp::kClientWidth / 2.0f;
                float halfClientH = (float)WinApp::kClientHeight / 2.0f;
                Matrix4x4 projectionSprite = MakeOrthographicMatrix(-halfClientW, halfClientH, halfClientW, -halfClientH, 0.0f, 100.0f);
                Matrix4x4 viewProjSprite = Multiply(MakeIdentity4x4(), projectionSprite);

                if (graphicsPipeline_ && graphicsPipeline_->GetSpriteRootSignature() && graphicsPipeline_->GetSpritePipelineState()) {
                    commandList->SetGraphicsRootSignature(graphicsPipeline_->GetSpriteRootSignature());
                    commandList->SetPipelineState(graphicsPipeline_->GetSpritePipelineState());

                    if (phaseIntroSprite_) {
                        phaseIntroSprite_->Draw(commandList, viewProjSprite);
                    }
                    if (numberIntroSprite_) {
                        numberIntroSprite_->Draw(commandList, viewProjSprite);
                    }
                }
            }
        }

    if (gpuParticleManager_) {
        SrvManager::GetInstance()->PreDraw();
        if (triggerDebrisEmit_) {
            gpuParticleManager_->TriggerEmit(debrisEmitPos_, 30000);
            triggerDebrisEmit_ = false;
        }
        gpuParticleManager_->UpdateCS();

        // 頂点バッファとインデックスバッファのポインタを取得してDrawに渡す (安全なバインド順序)
        D3D12_VERTEX_BUFFER_VIEW* pVbView = nullptr;
        D3D12_INDEX_BUFFER_VIEW* pIbView = nullptr;
        D3D12_VERTEX_BUFFER_VIEW vbView{};
        D3D12_INDEX_BUFFER_VIEW ibView{};
        if (particleModel_) {
            vbView = particleModel_->GetVertexBufferView();
            ibView = particleModel_->GetIndexBufferView();
            pVbView = &vbView;
            pIbView = &ibView;
        }

        gpuParticleManager_->Draw(
            commandList, 
            TextureManager::GetInstance()->GetSrvHandleGPU("isihahen.png"),
            pVbView,
            pIbView
        );
    }
    } // End of if (showParticles_)

    // --- 2. RenderTextureから画面（Swapchain）へのコピー ---
    postProcess_->PostDraw();

    // 描画先をバックバッファに戻す
    D3D12_CPU_DESCRIPTOR_HANDLE backBufferHandle = dxCommon_->GetCurrentBackBufferRtvHandle();
    D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle = dxCommon_->GetDsvHandle();
    commandList->OMSetRenderTargets(1, &backBufferHandle, false, &dsvHandle);

    if (isTransitioning_) {
        // 深度バッファをDEPTH_WRITE状態へ安全に遷移
        dxCommon_->TransitionDepthStencilState(D3D12_RESOURCE_STATE_DEPTH_WRITE);
        // 前シーンの3D描画が、今シーンのオブジェクトの深度値で遮蔽されるのを防ぐために深度バッファをクリア
        commandList->ClearDepthStencilView(dsvHandle, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);
        SceneManager::GetInstance()->DrawPreviousScene();
        // 前のシーンの描画でコンテキスト状態が変更されている可能性があるため再設定
        commandList->OMSetRenderTargets(1, &backBufferHandle, false, &dsvHandle);
    }

    // SrvManagerのデスクリプタヒープをセット
    SrvManager::GetInstance()->PreDraw();

    // 適切なパイプラインを選択
    ID3D12PipelineState* pso = nullptr;
    switch (activePostProcess_) {
    case kNone:
    default:
        pso = graphicsPipeline_->GetFullscreenPipelineState();
        break;
    case kGrayscale:
        pso = graphicsPipeline_->GetGrayscalePipelineState();
        break;
    case kSepia:
        pso = graphicsPipeline_->GetSepiaPipelineState();
        break;
    case kVignette:
        pso = graphicsPipeline_->GetVignettePipelineState();
        break;
    case kBoxFilter:
        pso = graphicsPipeline_->GetBoxFilterPipelineState();
        break;
    case kOutline:
        pso = graphicsPipeline_->GetDepthOutlinePipelineState();
        break;
    case kRadialBlur:
        pso = graphicsPipeline_->GetRadialBlurPipelineState();
        break;
    case kDissolve:
        pso = graphicsPipeline_->GetDissolvePipelineState();
        break;
    case kRandom:
        pso = graphicsPipeline_->GetRandomPipelineState();
        break;
    }

    // Fullscreenパイプラインで描画
    commandList->SetPipelineState(pso);
    if (activePostProcess_ == kDissolve) {
        commandList->SetGraphicsRootSignature(graphicsPipeline_->GetDissolveRootSignature());
        SrvManager::GetInstance()->SetGraphicsRootDescriptorTable(0, postProcess_->GetSrvIndex());
        SrvManager::GetInstance()->SetGraphicsRootDescriptorTable(1, activeNoiseSrvIndex_);
        commandList->SetGraphicsRootConstantBufferView(2, dissolveParamResource_->GetGPUVirtualAddress());
    } else if (activePostProcess_ == kOutline) {
        // 深度バッファをPIXEL_SHADER_RESOURCE状態へ安全に遷移
        dxCommon_->TransitionDepthStencilState(D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        commandList->SetGraphicsRootSignature(graphicsPipeline_->GetDepthOutlineRootSignature());
        SrvManager::GetInstance()->SetGraphicsRootDescriptorTable(0, postProcess_->GetSrvIndex()); // t0: カラー
        SrvManager::GetInstance()->SetGraphicsRootDescriptorTable(1, depthSrvIndex_);               // t1: 深度
    } else {
        commandList->SetGraphicsRootSignature(graphicsPipeline_->GetFullscreenRootSignature());
        SrvManager::GetInstance()->SetGraphicsRootDescriptorTable(0, postProcess_->GetSrvIndex());
        if (activePostProcess_ == kVignette && vignetteParamResource_) {
            commandList->SetGraphicsRootConstantBufferView(1, vignetteParamResource_->GetGPUVirtualAddress());
        } else if (activePostProcess_ == kBoxFilter && boxFilterParamResource_) {
            commandList->SetGraphicsRootConstantBufferView(1, boxFilterParamResource_->GetGPUVirtualAddress());
        } else if (activePostProcess_ == kRadialBlur && radialBlurParamResource_) {
            commandList->SetGraphicsRootConstantBufferView(1, radialBlurParamResource_->GetGPUVirtualAddress());
        } else if (activePostProcess_ == kRandom && randomParamResource_) {
            commandList->SetGraphicsRootConstantBufferView(1, randomParamResource_->GetGPUVirtualAddress());
        }
    }
    commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    commandList->DrawInstanced(3, 1, 0, 0);
}



void GamePlayScene::DrawSkeleton(const AdvAnim::Skeleton& skeleton, const Matrix4x4& baseWorldMatrix) {
    debugTransformIndex_ = 0;
    Matrix4x4 viewProjectionMatrix = camera_->GetViewProjectionMatrix();
    ID3D12GraphicsCommandList* commandList = dxCommon_->GetCommandList();

    for (const auto& joint : skeleton.joints) {
        if (debugTransformIndex_ >= kMaxDebugInstances) break;

        // Jointの球体描画
        Matrix4x4 sphereWorld = Multiply(joint.skeletonSpaceMatrix, baseWorldMatrix);
        // 必ず見えるように大きくする (0.3 -> 1.0)
        Matrix4x4 sphereFinal = Multiply(MakeScaleMatrix({ 1.0f, 1.0f, 1.0f }), sphereWorld);

        debugTransformData_[debugTransformIndex_]->WVP = Multiply(sphereFinal, viewProjectionMatrix);
        debugTransformData_[debugTransformIndex_]->World = sphereFinal;
        commandList->SetGraphicsRootConstantBufferView(1, debugTransformResources_[debugTransformIndex_]->GetGPUVirtualAddress());
        debugSphereModel_->DrawModel(commandList, TextureManager::GetInstance()->GetSrvHandleGPU("human/white.png"), TextureManager::GetInstance()->GetSrvHandleGPU("test.dds"));
        debugTransformIndex_++;

        // ボーン（線）の描画
        if (joint.parent.has_value() && debugTransformIndex_ < kMaxDebugInstances) {
            const auto& parentJoint = skeleton.joints[*joint.parent];
            Vector3 p1 = { parentJoint.skeletonSpaceMatrix.m[3][0], parentJoint.skeletonSpaceMatrix.m[3][1], parentJoint.skeletonSpaceMatrix.m[3][2] };
            Vector3 p2 = { joint.skeletonSpaceMatrix.m[3][0], joint.skeletonSpaceMatrix.m[3][1], joint.skeletonSpaceMatrix.m[3][2] };

            // baseWorldMatrixを適用した座標を計算
            Vector4 p1W = Multiply(Vector4{ p1.x, p1.y, p1.z, 1.0f }, baseWorldMatrix);
            Vector4 p2W = Multiply(Vector4{ p2.x, p2.y, p2.z, 1.0f }, baseWorldMatrix);
            Vector3 start = { p1W.x, p1W.y, p1W.z };
            Vector3 end = { p2W.x, p2W.y, p2W.z };

            Vector3 diff = { end.x - start.x, end.y - start.y, end.z - start.z };
            float length = std::sqrt(diff.x * diff.x + diff.y * diff.y + diff.z * diff.z);
            if (length > 0.01f) {
                Vector3 direction = { diff.x / length, diff.y / length, diff.z / length };

                // Z軸をdirectionに向け、原点をstartに置く行列を作成
                Matrix4x4 rotateMatrix = MakeIdentity4x4();
                Vector3 up = (std::abs(direction.y) < 0.999f) ? Vector3{ 0, 1, 0 } : Vector3{ 1, 0, 0 };
                Vector3 right = Normalize(Cross(up, direction));
                Vector3 realUp = Cross(direction, right);
                rotateMatrix.m[0][0] = right.x; rotateMatrix.m[0][1] = right.y; rotateMatrix.m[0][2] = right.z;
                rotateMatrix.m[1][0] = realUp.x; rotateMatrix.m[1][1] = realUp.y; rotateMatrix.m[1][2] = realUp.z;
                rotateMatrix.m[2][0] = direction.x; rotateMatrix.m[2][1] = direction.y; rotateMatrix.m[2][2] = direction.z;

                Matrix4x4 boxWorld = Multiply(Multiply(MakeScaleMatrix({ 1.0f, 1.0f, length }), rotateMatrix), MakeTranslateMatrix(start));

                debugTransformData_[debugTransformIndex_]->WVP = Multiply(boxWorld, viewProjectionMatrix);
                debugTransformData_[debugTransformIndex_]->World = boxWorld;
                commandList->SetGraphicsRootConstantBufferView(1, debugTransformResources_[debugTransformIndex_]->GetGPUVirtualAddress());
                debugBoxModel_->DrawModel(commandList, TextureManager::GetInstance()->GetSrvHandleGPU("human/white.png"), TextureManager::GetInstance()->GetSrvHandleGPU("test.dds"));
                debugTransformIndex_++;
            }
        }
    }
}

void GamePlayScene::ApplyGroupFormation(int groupIndex) {
    EnemyGroup& group = enemyGroups_[groupIndex];
    
    for (int idx = 0; idx < kEnemiesPerGroup; ++idx) {
        int enemyIdx = groupIndex * kEnemiesPerGroup + idx;
        Enemy& enemy = enemies_[enemyIdx];
        
        // フォーメーションに応じたレール左右(relX)・上下(relY)・先後(railOfs)オフセット
        float relX = 0.0f, relY = 0.0f, railOfs = 0.0f;
        
        switch (group.formation) {
        case FormationType::kVShape:
            if (idx == 0) { relX =   0.0f; relY = 0.0f; railOfs =   0.0f; }
            else if (idx == 1) { relX = -10.0f; relY = 0.0f; railOfs = -15.0f; }
            else if (idx == 2) { relX =  10.0f; relY = 0.0f; railOfs = -15.0f; }
            else if (idx == 3) { relX = -20.0f; relY = 0.0f; railOfs = -30.0f; }
            else if (idx == 4) { relX =  20.0f; relY = 0.0f; railOfs = -30.0f; }
            break;
        case FormationType::kCircle: {
            float angle = ((float)idx / kEnemiesPerGroup) * 2.0f * (float)M_PI;
            relX = std::cos(angle) * 14.0f;
            relY = std::sin(angle) * 14.0f;
            railOfs = 0.0f;
            break;
        }
        case FormationType::kLineX:
            relX = ((float)idx - (kEnemiesPerGroup - 1) * 0.5f) * 12.0f;
            relY = 0.0f;
            railOfs = 0.0f;
            break;
        case FormationType::kSlant:
            relX = ((float)idx - (kEnemiesPerGroup - 1) * 0.5f) * 12.0f;
            relY = ((float)idx - (kEnemiesPerGroup - 1) * 0.5f) * 6.0f;
            railOfs = ((float)idx - (kEnemiesPerGroup - 1) * 0.5f) * -10.0f;
            break;
        }
        
        enemy.localOffset = { relX, relY, railOfs };
        
        // wanderAnchorをレール空間で記録
        enemy.wanderAnchorRailProgress = group.centerRailProgress + railOfs;
        enemy.wanderAnchorRelX = group.centerX + relX;
        enemy.wanderAnchorRelY = group.centerY + relY;
        if (enemy.wanderAnchorRelY < 5.0f) {
            enemy.wanderAnchorRelY = 5.0f; // 地面に潜るのを防止
        }
        
        // 初期ワールド座標をレールから計算
        Vector3 railPos  = GetRailPosition(enemy.wanderAnchorRailProgress);
        Vector3 railDir  = GetRailDirection(enemy.wanderAnchorRailProgress);
        Vector3 railRight = CalculateRailRight(railDir);
        Vector3 railUp   = CalculateRailUp(railDir, railRight);
        enemy.wanderAnchor = Add(railPos, Add(Scale(railRight, enemy.wanderAnchorRelX), Scale(railUp, enemy.wanderAnchorRelY)));
        
        // railProgress をセット
        enemy.railProgress = enemy.wanderAnchorRailProgress;
        
        // 初期状態は横側待機 (kSideWait)
        enemy.state = Enemy::State::kSideWait;
        enemy.stateTimer = 0.0f;
        enemy.rotate = { 0.0f, 0.0f, 0.0f };
        enemy.relativeZ = 220.0f;
        
        // 「ビルに隠れた状態から真ん中に現れる」を表現するため、初期位置をレール左右の极端(+/-45m)に設定
        float spawnRelX = (idx % 2 == 0) ? -45.0f : 45.0f;
        enemy.position = Add(railPos, Add(Scale(railRight, spawnRelX), Scale(railUp, enemy.wanderAnchorRelY)));
    }
}

void GamePlayScene::RespawnEnemyGroup(int groupIndex, float playerZ) {
    EnemyGroup& group = enemyGroups_[groupIndex];
    
    // フォーメーション形状をランダム決定
    std::uniform_int_distribution<int> distForm(0, (int)FormationType::kCount - 1);
    group.formation = (FormationType)distForm(randomEngine_);
    
    // レール左右方向の相対位置と高さをランダム決定
    std::uniform_real_distribution<float> distX(-10.0f, 10.0f);
    std::uniform_real_distribution<float> distY(5.0f, 20.0f);
    
    // レール累積距離に対する前方オフセット（グループごとに50mずらす）
    float minProgress = 160.0f + (float)groupIndex * 50.0f;
    float maxProgress = 220.0f + (float)groupIndex * 50.0f;
    std::uniform_real_distribution<float> distProgress(minProgress, maxProgress);
    
    group.centerX = distX(randomEngine_);
    group.centerY = distY(randomEngine_);
    group.centerRailProgress = playerZ + distProgress(randomEngine_);
    group.centerZ = group.centerRailProgress; // 互換性のため残留
    
    // 小隊メンバー全員を生存状態（Alive）にして再配置
    for (int idx = 0; idx < kEnemiesPerGroup; ++idx) {
        int enemyIdx = groupIndex * kEnemiesPerGroup + idx;
        enemies_[enemyIdx].isAlive = true;
        enemies_[enemyIdx].hp = 30.0f;
        enemies_[enemyIdx].maxHP = 30.0f;
    }
    
    ApplyGroupFormation(groupIndex);
}

void GamePlayScene::UpdateDemo(float deltaTime) {
    // 0. 数字キーによるプリセット切り替え
    if (input_->IsKeyTriggered(DIK_1)) ApplyPreset(0);
    else if (input_->IsKeyTriggered(DIK_2)) ApplyPreset(1);
    else if (input_->IsKeyTriggered(DIK_3)) ApplyPreset(2);
    else if (input_->IsKeyTriggered(DIK_4)) ApplyPreset(3);
    else if (input_->IsKeyTriggered(DIK_5)) ApplyPreset(4);
    else if (input_->IsKeyTriggered(DIK_6)) ApplyPreset(5);
    else if (input_->IsKeyTriggered(DIK_7)) ApplyPreset(6);
    else if (input_->IsKeyTriggered(DIK_8)) ApplyPreset(7);
    else if (input_->IsKeyTriggered(DIK_9)) ApplyPreset(8);
    else if (input_->IsKeyTriggered(DIK_0)) ApplyPreset(9);

    // 1. ヒットストップ処理
    if (hitstopTimer_ > 0.0f) {
        hitstopTimer_ -= deltaTime;
        // ヒットストップ中は時間の進行を極端に遅くする（スローモーション化）
        deltaTime = deltaTime * 0.05f;
    }

    // 2. カメラシェイク処理
    if (cameraShakeTimer_ > 0.0f) {
        cameraShakeTimer_ -= deltaTime;
        if (cameraShakeTimer_ < 0.0f) cameraShakeTimer_ = 0.0f;
        
        float progress = cameraShakeTimer_ / (cameraShakeTimeMax_ > 0.0f ? cameraShakeTimeMax_ : 1.0f);
        progress = std::clamp(progress, 0.0f, 1.0f);
        // 自乗でイージングアウトさせる
        float currentIntensity = activeShakeIntensity_ * (progress * progress);
        
        std::uniform_real_distribution<float> distShake(-1.0f, 1.0f);
        if (selectedEffectPreset_ == 7) {
            // 風竜巻は横揺れ（Xのみ）で煽られる表現
            cameraShakeOffset_ = {
                distShake(randomEngine_) * currentIntensity,
                0.0f,
                0.0f
            };
        } else {
            cameraShakeOffset_ = {
                distShake(randomEngine_) * currentIntensity,
                distShake(randomEngine_) * currentIntensity,
                distShake(randomEngine_) * currentIntensity
            };
        }
    } else {
        cameraShakeOffset_ = { 0.0f, 0.0f, 0.0f };
    }

    // カメラの座標と角度を設定して更新
    camera_->GetTransform().translate = Add(cameraBasePos_, cameraShakeOffset_);
    camera_->GetTransform().rotate = cameraBaseRot_;
    camera_->Update();

    // 3. インパクトフラッシュの更新
    if (flashAlpha_ > 0.0f) {
        // 神聖属性(Preset 8)はゆっくり余韻を残してフェードアウト、他は高速フェード
        float fadeSpeed = (selectedEffectPreset_ == 8) ? 1.5f : 3.5f;
        flashAlpha_ -= deltaTime * fadeSpeed;
        if (flashAlpha_ < 0.0f) flashAlpha_ = 0.0f;
    }

    // 4. ラジアルブラーの更新
    if (useRadialBlur_ && blurIntensity_ > 0.0f) {
        blurIntensity_ -= deltaTime * (maxBlurWidth_ / 0.25f);
        if (blurIntensity_ < 0.0f) blurIntensity_ = 0.0f;
        
        activePostProcess_ = kRadialBlur;
        if (radialBlurParamData_) {
            radialBlurParamData_->center = { 0.5f, 0.5f };
            radialBlurParamData_->blurWidth = -blurIntensity_;
        }
    } else {
        if (activePostProcess_ == kRadialBlur) {
            activePostProcess_ = kNone;
        }
    }

    // 4.5 デジタルバグ（グリッチノイズポストプロセス）の更新
    if (digitalGlitchTimer_ > 0.0f) {
        digitalGlitchTimer_ -= deltaTime;
        if (digitalGlitchTimer_ < 0.0f) digitalGlitchTimer_ = 0.0f;
        
        activePostProcess_ = kRandom;
        if (randomParamResource_ && randomParamData_) {
            randomParamData_->time = randomEffectTime_;
            randomParamData_->noiseScale = 100.0f;
            randomParamData_->noiseStrength = 0.85f; // 強めのノイズ
            randomParamData_->isColorNoise = false;
            randomParamData_->isMultiplyNoise = false;
        }
    } else {
        if (activePostProcess_ == kRandom) {
            activePostProcess_ = kNone;
        }
    }

    // 5. 攻撃モード制御 ＆ 近接突撃・ビーム射撃処理
    if (input_->IsKeyTriggered(DIK_TAB)) {
        attackMode_ = (attackMode_ == AttackMode::kShooting) ? AttackMode::kMelee : AttackMode::kShooting;
        meleeState_ = MeleeState::kIdle;
        meleeTimer_ = 0.0f;
        currentFighterPos_ = playerPos_;
    }

    bool attackTrigger = input_->IsKeyTriggered(DIK_SPACE);
    
    // オートデモタイマー更新
    if (autoPlay_) {
        autoPlayTimer_ += deltaTime;
        if (autoPlayTimer_ >= autoPlayInterval_) {
            attackTrigger = true;
            autoPlayTimer_ = 0.0f;
        }
    }

    if (attackMode_ == AttackMode::kShooting) {
        // ── 射撃モード ──
        float hoverY = std::sin(randomEffectTime_ * 3.0f) * 0.15f;
        currentFighterPos_ = playerPos_;
        currentFighterPos_.y += hoverY;
        fighterModel_->transform.rotate.z = 0.0f;
        
        if (attackTrigger) {
            for (int i = 0; i < kMaxShotBeams; ++i) {
                if (!shotBeams_[i].isAlive) {
                    shotBeams_[i].position = playerPos_;
                    Vector3 toTarget = Subtract(targetPos_, playerPos_);
                    shotBeams_[i].velocity = Scale(Normalize(toTarget), 150.0f);
                    shotBeams_[i].isAlive = true;
                    if (audio_) {
                        audio_->PlayWave(jumpSE_, false, 0.35f);
                    }
                    break;
                }
            }
        }
    } else {
        // ── 近接突撃モード ──
        const float kMeleeDashDuration = 0.12f; // より高速に（7〜8フレーム）
        const float kMeleeHitDuration = 0.06f;
        const float kMeleeReturnDuration = 0.30f;

        if (meleeState_ == MeleeState::kIdle) {
            currentFighterPos_ = playerPos_;
            float hoverY = std::sin(randomEffectTime_ * 3.0f) * 0.15f;
            currentFighterPos_.y += hoverY;
            fighterModel_->transform.rotate.z = 0.0f;

            if (attackTrigger) {
                meleeState_ = MeleeState::kDash;
                meleeTimer_ = 0.0f;
                if (audio_) {
                    audio_->PlayWave(jumpSE_, false, 0.7f);
                }
            }
        }
        
        if (meleeState_ == MeleeState::kDash) {
            meleeTimer_ += deltaTime;
            float t = std::clamp(meleeTimer_ / kMeleeDashDuration, 0.0f, 1.0f);
            
            // イージング付き突進
            currentFighterPos_.x = std::lerp(playerPos_.x, targetPos_.x, t * t);
            currentFighterPos_.y = std::lerp(playerPos_.y, targetPos_.y, t * t);
            currentFighterPos_.z = std::lerp(playerPos_.z, targetPos_.z - 1.5f, t * t);
            
            // バレルスピン
            fighterModel_->transform.rotate.z = t * static_cast<float>(M_PI) * 4.0f;
            
            if (t >= 1.0f) {
                meleeState_ = MeleeState::kHit;
                meleeTimer_ = 0.0f;
            }
        }
        
        if (meleeState_ == MeleeState::kHit) {
            currentFighterPos_ = targetPos_;
            currentFighterPos_.z -= 1.5f;
            
            if (meleeTimer_ == 0.0f) {
                EmitHitEffect(targetPos_);
            }
            
            meleeTimer_ += deltaTime;
            if (meleeTimer_ >= kMeleeHitDuration) {
                meleeState_ = MeleeState::kReturn;
                meleeTimer_ = 0.0f;
            }
        }
        
        if (meleeState_ == MeleeState::kReturn) {
            meleeTimer_ += deltaTime;
            float t = std::clamp(meleeTimer_ / kMeleeReturnDuration, 0.0f, 1.0f);
            
            // 元の位置に戻る
            currentFighterPos_.x = std::lerp(targetPos_.x, playerPos_.x, t);
            currentFighterPos_.y = std::lerp(targetPos_.y, playerPos_.y, t);
            currentFighterPos_.z = std::lerp(targetPos_.z - 1.5f, playerPos_.z, t);
            
            // 回転を徐々に戻す
            fighterModel_->transform.rotate.z = (1.0f - t) * static_cast<float>(M_PI) * 4.0f;
            
            if (t >= 1.0f) {
                meleeState_ = MeleeState::kIdle;
                meleeTimer_ = 0.0f;
                fighterModel_->transform.rotate.z = 0.0f;
            }
        }
    }

    // ビーム前進と着弾判定
    for (int i = 0; i < kMaxShotBeams; ++i) {
        if (shotBeams_[i].isAlive) {
            shotBeams_[i].position = Add(shotBeams_[i].position, Scale(shotBeams_[i].velocity, deltaTime));
            
            if (shotBeams_[i].position.z >= targetPos_.z - 0.5f) {
                shotBeams_[i].isAlive = false;
                EmitHitEffect(targetPos_);
            }

            if (bulletTransformResources_[i]) {
                bulletTransformData_[i]->World = MakeAffineMatrix(Vector3{0.4f, 0.4f, 1.2f}, Vector3{0.0f, 0.0f, 0.0f}, shotBeams_[i].position);
                bulletTransformData_[i]->WVP = Multiply(bulletTransformData_[i]->World, camera_->GetViewProjectionMatrix());
            }
        }
    }

    // 6. パーティクル更新
    if (particleManager_) {
        particleManager_->Update(
            camera_->GetViewProjectionMatrix(),
            camera_->GetBillboardMatrix(),
            deltaTime,
            camera_->GetTransform().translate.z,
            playerPos_,
            false,
            false,
            0,
            targetPos_
        );
    }

    if (gpuParticleManager_) {
        gpuParticleManager_->Update(
            camera_->GetViewProjectionMatrix(),
            camera_->GetBillboardMatrix(),
            deltaTime
        );
    }

    // 7. 自機と標的の行列更新
    if (fighterModel_ && fighterTransformResource_) {
        fighterModel_->transform.translate = currentFighterPos_;
        
        float recoilPitch = 0.0f;
        if (attackMode_ == AttackMode::kShooting && attackTrigger) recoilPitch = 0.12f;
        else if (meleeState_ == MeleeState::kDash) recoilPitch = -0.2f;
        
        fighterModel_->transform.rotate.x = std::lerp(fighterModel_->transform.rotate.x, recoilPitch, 0.1f);
        
        fighterTransformData_->World = MakeAffineMatrix(
            Vector3{0.5f, 0.5f, 0.5f},
            fighterModel_->transform.rotate,
            currentFighterPos_
        );
        fighterTransformData_->WVP = Multiply(fighterTransformData_->World, camera_->GetViewProjectionMatrix());
    }

    if (enemyModel_ && enemyTransformResources_[0]) {
        float hoverY = std::cos(randomEffectTime_ * 2.0f) * 0.25f;
        Vector3 renderTargetPos = targetPos_;
        renderTargetPos.y += hoverY;
        
        Vector3 targetRot = { randomEffectTime_ * 0.5f, randomEffectTime_ * 0.8f, 0.0f };
        enemyTransformData_[0]->World = MakeAffineMatrix(
            Vector3{1.2f, 1.2f, 1.2f},
            targetRot,
            renderTargetPos
        );
        enemyTransformData_[0]->WVP = Multiply(enemyTransformData_[0]->World, camera_->GetViewProjectionMatrix());
    }
    
    // スプライト（HPバー等）を非表示化するため、WVPを0行列にする
    for (uint32_t i = 0; i < kHpBarInstanceCount; ++i) {
        hpBarInstancingData_[i].WVP = MakeIdentity4x4();
        hpBarInstancingData_[i].WVP.m[0][0] = 0.0f;
        hpBarInstancingData_[i].WVP.m[1][1] = 0.0f;
        hpBarInstancingData_[i].WVP.m[2][2] = 0.0f;
        hpBarInstancingData_[i].WVP.m[3][3] = 0.0f;
    }
}

void GamePlayScene::EmitHitEffect(const Vector3& pos) {
    bool isMelee = (attackMode_ == AttackMode::kMelee);

    if (isMelee) {
        // ── 近接（打撃・斬撃）モード専用の豪華複合エフェクト ──
        // 基本的な打撃衝撃波リングとシリンダーの重ね合わせ
        particleManager_->EmitRing(pos);
        particleManager_->EmitRing(Add(pos, { 0.0f, 0.0f, 0.8f })); // 二重衝撃波
        particleManager_->EmitCylinder(pos);

        switch (selectedEffectPreset_) {
        case 0: // 通常近接
            // 巨大な十字・X字斬撃軌跡と、重みのあるスパーク
            particleManager_->EmitSlash(pos, effectParticleSpeed_ * 1.5f, static_cast<int>(effectParticleCount_ * 1.2f), effectBaseColor_);
            particleManager_->EmitSlash(pos, effectParticleSpeed_ * 1.2f, static_cast<int>(effectParticleCount_ * 0.8f), { 1.0f, 1.0f, 1.0f });
            particleManager_->EmitCustomSparks(pos, effectParticleSpeed_ * 1.8f, static_cast<int>(effectParticleCount_ * 1.5f), effectBaseColor_, effectGravity_ + 0.5f);
            break;

        case 1: // 火炎近接
            // 巨大炎スラッシュ ＋ 炎バースト ＋ 飛び散る残り火
            particleManager_->EmitFlame(pos, effectParticleSpeed_ * 1.2f, static_cast<int>(effectParticleCount_ * 1.3f), effectBaseColor_);
            particleManager_->EmitSlash(pos, effectParticleSpeed_ * 1.6f, effectParticleCount_, { 1.0f, 0.3f, 0.0f });
            particleManager_->EmitSlash(pos, effectParticleSpeed_ * 1.4f, static_cast<int>(effectParticleCount_ * 0.8f), { 1.0f, 0.8f, 0.2f });
            particleManager_->EmitCylinder(pos);
            particleManager_->EmitCustomSparks(pos, effectParticleSpeed_ * 1.6f, effectParticleCount_, effectBaseColor_, 0.6f);
            break;

        case 2: // 雷撃近接
            // 雷刃十字斬 ＋ 四方からの放電スレッド収縮 ＋ 超高圧放電リング
            particleManager_->EmitLightning(pos, effectParticleSpeed_ * 2.0f, static_cast<int>(effectParticleCount_ * 1.3f), effectBaseColor_);
            particleManager_->EmitSlash(pos, effectParticleSpeed_ * 1.6f, effectParticleCount_, { 0.2f, 0.9f, 1.0f });
            particleManager_->EmitLaserThread(Add(pos, { -12.0f, 0.0f, 0.0f }), pos);
            particleManager_->EmitLaserThread(Add(pos, { 12.0f, 0.0f, 0.0f }), pos);
            particleManager_->EmitLaserThread(Add(pos, { 0.0f, -12.0f, 0.0f }), pos);
            particleManager_->EmitLaserThread(Add(pos, { 0.0f, 12.0f, 0.0f }), pos);
            particleManager_->EmitCylinder(pos);
            break;

        case 3: // 真空斬撃近接
            // 鋭く交叉する真空の風刃（4枚刃） ＋ 多重真空リング
            particleManager_->EmitSlash(pos, effectParticleSpeed_ * 1.8f, static_cast<int>(effectParticleCount_ * 1.5f), effectBaseColor_);
            particleManager_->EmitSlash(pos, effectParticleSpeed_ * 1.5f, effectParticleCount_, { 0.7f, 1.0f, 0.8f });
            particleManager_->EmitSlash(pos, effectParticleSpeed_ * 1.3f, static_cast<int>(effectParticleCount_ * 0.8f), { 1.0f, 1.0f, 1.0f });
            particleManager_->EmitRing(Add(pos, { 0.0f, 0.0f, -0.8f }));
            particleManager_->EmitRing(Add(pos, { 0.0f, 0.0f, 0.8f }));
            break;

        case 4: // 特異点近接
            // 重力吸い込みから瞬時に空間を切り裂く重力崩壊斬撃
            particleManager_->EmitGravityVortex(pos, effectParticleSpeed_ * 0.9f, static_cast<int>(effectParticleCount_ * 1.3f), effectBaseColor_);
            particleManager_->EmitGravityOut(pos, static_cast<int>(effectParticleCount_ * 1.3f), effectBaseColor_);
            particleManager_->EmitSlash(pos, effectParticleSpeed_ * 1.6f, effectParticleCount_, { 0.6f, 0.1f, 0.9f });
            particleManager_->EmitCylinder(pos);
            break;

        case 5: // 氷結近接
            // 巨大氷華の破砕 ＋ 氷の鋭いクロス斬撃
            particleManager_->EmitGlacial(pos, effectParticleSpeed_ * 1.2f, static_cast<int>(effectParticleCount_ * 1.4f), effectBaseColor_);
            particleManager_->EmitSlash(pos, effectParticleSpeed_ * 1.6f, effectParticleCount_, { 0.5f, 0.8f, 1.0f });
            particleManager_->EmitRing(pos);
            particleManager_->EmitRing(Add(pos, { 0.0f, 0.0f, -1.0f }));
            break;

        case 6: // デジタル近接
            // デジタルバグ ＋ マゼンタとグリーンの交差スライスカッター
            particleManager_->EmitDigitalGlitch(pos, effectParticleSpeed_ * 1.3f, static_cast<int>(effectParticleCount_ * 1.4f), effectBaseColor_);
            particleManager_->EmitSlash(pos, effectParticleSpeed_ * 1.5f, effectParticleCount_, { 0.0f, 1.0f, 0.4f });
            particleManager_->EmitSlash(pos, effectParticleSpeed_ * 1.5f, effectParticleCount_, { 1.0f, 0.0f, 0.8f });
            particleManager_->EmitCylinder(pos);
            break;

        case 7: // 風竜巻近接
            // 竜巻渦巻き ＋ 高速回転する風の斬撃
            particleManager_->EmitAeroWind(pos, effectParticleSpeed_ * 1.2f, static_cast<int>(effectParticleCount_ * 1.3f), effectBaseColor_);
            particleManager_->EmitSlash(pos, effectParticleSpeed_ * 1.8f, effectParticleCount_, { 0.8f, 1.0f, 0.9f });
            particleManager_->EmitCylinder(pos);
            particleManager_->EmitRing(pos);
            break;

        case 8: // 神聖近接
            // 天から降り注ぐ極太の光の柱 ＋ 黄金の十字斬撃 ＋ 聖なる光輪
            particleManager_->EmitHolyLight(pos, effectParticleSpeed_ * 1.2f, static_cast<int>(effectParticleCount_ * 1.3f), effectBaseColor_);
            particleManager_->EmitSlash(pos, effectParticleSpeed_ * 1.6f, effectParticleCount_, { 1.0f, 0.95f, 0.7f });
            particleManager_->EmitLaserThread(Add(pos, { 0.0f, 25.0f, 0.0f }), pos);
            particleManager_->EmitLaserThread(Add(pos, { -5.0f, 20.0f, -3.0f }), pos);
            particleManager_->EmitLaserThread(Add(pos, { 5.0f, 20.0f, 3.0f }), pos);
            particleManager_->EmitCylinder(pos);
            break;

        case 9: // カオス近接
            // 混沌物質の炸裂 ＋ 闇の稲妻 ＋ 混沌の衝撃斬撃 ＋ 特異点渦
            particleManager_->EmitChaosVoid(pos, effectParticleSpeed_ * 1.2f, static_cast<int>(effectParticleCount_ * 1.3f), effectBaseColor_);
            particleManager_->EmitLightning(pos, effectParticleSpeed_ * 1.8f, static_cast<int>(effectParticleCount_ * 0.8f), { 0.8f, 0.0f, 0.9f });
            particleManager_->EmitSlash(pos, effectParticleSpeed_ * 1.6f, effectParticleCount_, { 0.4f, 0.0f, 0.6f });
            particleManager_->EmitGravityVortex(pos, effectParticleSpeed_ * 0.8f, static_cast<int>(effectParticleCount_ * 0.6f), { 0.1f, 0.0f, 0.2f });
            particleManager_->EmitCylinder(pos);
            break;
        }
    } else {
        // ── 射撃（ビームヒット）モードのエフェクト（既存） ──
        switch (selectedEffectPreset_) {
        case 0: // 通常スパーク
            particleManager_->EmitCustomSparks(pos, effectParticleSpeed_, effectParticleCount_, effectBaseColor_, effectGravity_);
            particleManager_->EmitRing(pos, effectBaseColor_);
            break;
            
        case 1: // 火炎バースト
            particleManager_->EmitFlame(pos, effectParticleSpeed_ * 0.8f, effectParticleCount_, effectBaseColor_);
            particleManager_->EmitCylinder(pos, effectBaseColor_);
            particleManager_->EmitRing(pos, effectBaseColor_);
            particleManager_->EmitCustomSparks(pos, effectParticleSpeed_ * 1.5f, effectParticleCount_ / 2, effectBaseColor_, 0.45f);
            break;
            
        case 2: // 超高圧放電
            particleManager_->EmitLightning(pos, effectParticleSpeed_ * 1.8f, effectParticleCount_, effectBaseColor_);
            particleManager_->EmitLaserThread(Add(pos, {-8.0f,0.0f,0.0f}), pos);
            particleManager_->EmitLaserThread(Add(pos, {8.0f,0.0f,0.0f}), pos);
            particleManager_->EmitLaserThread(Add(pos, {0.0f,-8.0f,0.0f}), pos);
            particleManager_->EmitLaserThread(Add(pos, {0.0f,8.0f,0.0f}), pos);
            particleManager_->EmitCylinder(pos, effectBaseColor_);
            break;
            
        case 3: // 真空斬撃
            particleManager_->EmitSlash(pos, effectParticleSpeed_ * 1.5f, effectParticleCount_, effectBaseColor_);
            particleManager_->EmitRing(pos, effectBaseColor_);
            particleManager_->EmitRing(Add(pos, {0.0f, 0.0f, 1.0f}), effectBaseColor_);
            break;
            
        case 4: // 特異点爆発
            particleManager_->EmitGravityVortex(pos, effectParticleSpeed_ * 0.8f, effectParticleCount_, effectBaseColor_);
            particleManager_->EmitCylinder(pos, effectBaseColor_);
            particleManager_->EmitRing(pos, effectBaseColor_);
            particleManager_->EmitGravityOut(pos, effectParticleCount_, effectBaseColor_);
            break;

        case 5: // 氷結・氷華
            particleManager_->EmitGlacial(pos, effectParticleSpeed_, effectParticleCount_, effectBaseColor_);
            break;
            
        case 6: // デジタルバグ
            particleManager_->EmitDigitalGlitch(pos, effectParticleSpeed_, effectParticleCount_, effectBaseColor_);
            break;

        case 7: // 風・竜巻
            particleManager_->EmitAeroWind(pos, effectParticleSpeed_, effectParticleCount_, effectBaseColor_);
            particleManager_->EmitRing(pos, effectBaseColor_);
            particleManager_->EmitCylinder(pos, effectBaseColor_);
            break;
            
        case 8: // 神聖・天光
            particleManager_->EmitHolyLight(pos, effectParticleSpeed_, effectParticleCount_, effectBaseColor_);
            break;
            
        case 9: // カオスボイド・闇物質
            particleManager_->EmitChaosVoid(pos, effectParticleSpeed_, effectParticleCount_, effectBaseColor_);
            particleManager_->EmitLightning(pos, effectParticleSpeed_ * 1.5f, effectParticleCount_ / 2, {0.8f, 0.0f, 0.9f});
            particleManager_->EmitGravityVortex(pos, effectParticleSpeed_ * 0.7f, effectParticleCount_ / 2, {0.1f, 0.0f, 0.2f});
            particleManager_->EmitCylinder(pos, effectBaseColor_);
            particleManager_->EmitRing(pos, effectBaseColor_);
            break;
        }
    }

    // ── ダイナミック画面演出のトリガー（近接時は約1.5〜1.8倍に強化。射撃時は強さを20%に抑えプレイしやすく調整） ──
    float shakeMultiplier = isMelee ? 1.6f : 0.8f;
    float hitstopMultiplier = isMelee ? 1.8f : 1.0f;

    activeShakeIntensity_ = cameraShakeIntensity_ * (isMelee ? 1.6f : 0.20f);
    cameraShakeTimer_ = cameraShakeTimeMax_ * shakeMultiplier;
    hitstopTimer_ = isMelee ? (hitstopTimeMax_ * hitstopMultiplier) : 0.0f;
    
    // デジタルバグ（Preset 6）の砂嵐演出は無効化 (ユーザー要求により削除)

    if (useRadialBlur_) {
        blurIntensity_ = maxBlurWidth_ * (isMelee ? 1.5f : 1.0f);
    }
    if (useImpactFlash_) {
        flashAlpha_ = isMelee ? 0.80f : 0.55f; // 近接はフラッシュ強め
        if (selectedEffectPreset_ == 1) flashColor_ = {1.0f, 0.4f, 0.1f, 1.0f};
        else if (selectedEffectPreset_ == 2) flashColor_ = {0.2f, 0.8f, 1.0f, 1.0f};
        else if (selectedEffectPreset_ == 4) flashColor_ = {0.15f, 0.0f, 0.25f, 1.0f}; // 特異点は暗い黒紫色
        else if (selectedEffectPreset_ == 5) flashColor_ = {0.4f, 0.75f, 1.0f, 1.0f};
        else if (selectedEffectPreset_ == 8) flashColor_ = {1.0f, 0.95f, 0.5f, 1.0f};
        else if (selectedEffectPreset_ == 9) flashColor_ = {0.35f, 0.0f, 0.55f, 1.0f};
        else flashColor_ = {1.0f, 1.0f, 1.0f, 1.0f};
    } else {
        flashAlpha_ = 0.0f;
    }
}

void GamePlayScene::DrawDemo() {
    ID3D12GraphicsCommandList* commandList = dxCommon_->GetCommandList();

    // 1. RenderTextureへの描画開始
    postProcess_->PreDraw();

    // 3Dモデル用パイプライン設定
    if (graphicsPipeline_ && graphicsPipeline_->GetObject3dPipelineState() && graphicsPipeline_->GetObject3dRootSignature()) {
        commandList->SetPipelineState(graphicsPipeline_->GetObject3dPipelineState());
        commandList->SetGraphicsRootSignature(graphicsPipeline_->GetObject3dRootSignature());

        ID3D12DescriptorHeap* heaps[] = { TextureManager::GetInstance()->GetSrvHeap() };
        commandList->SetDescriptorHeaps(1, heaps);

        if (directionalLightResource_) commandList->SetGraphicsRootConstantBufferView(4, directionalLightResource_->GetGPUVirtualAddress());
        if (cameraResource_) commandList->SetGraphicsRootConstantBufferView(5, cameraResource_->GetGPUVirtualAddress());

        // プレイヤー戦闘機の描画
        if (fighterModel_ && fighterTransformResource_) {
            commandList->SetGraphicsRootConstantBufferView(1, fighterTransformResource_->GetGPUVirtualAddress());
            fighterModel_->DrawModel(commandList, TextureManager::GetInstance()->GetSrvHandleGPU("Player2/Player_basecolor.JPEG"), TextureManager::GetInstance()->GetSrvHandleGPU("test.dds"));
        }

        // 標的の描画
        if (enemyModel_ && enemyTransformResources_[0]) {
            commandList->SetGraphicsRootConstantBufferView(1, enemyTransformResources_[0]->GetGPUVirtualAddress());
            enemyModel_->DrawModel(commandList, TextureManager::GetInstance()->GetSrvHandleGPU("Player/player.png"), TextureManager::GetInstance()->GetSrvHandleGPU("test.dds"));
        }

        // ビームの描画
        if (debugSphereModel_) {
            for (int i = 0; i < kMaxShotBeams; ++i) {
                if (shotBeams_[i].isAlive) {
                    commandList->SetGraphicsRootConstantBufferView(1, bulletTransformResources_[i]->GetGPUVirtualAddress());
                    debugSphereModel_->DrawModel(commandList, TextureManager::GetInstance()->GetSrvHandleGPU("human/white.png"), TextureManager::GetInstance()->GetSrvHandleGPU("test.dds"));
                }
            }
        }
    }

    // パーティクルの描画（半透明）
    if (showParticles_ && graphicsPipeline_ && graphicsPipeline_->GetRootSignature()) {
        commandList->SetGraphicsRootSignature(graphicsPipeline_->GetRootSignature());
        commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

        BlendMode blendMode = useAdditiveBlend_ ? kBlendModeAdd : kBlendModeNormal;
        if (graphicsPipeline_->GetPipelineState(blendMode)) {
            commandList->SetPipelineState(graphicsPipeline_->GetPipelineState(blendMode));
            
            particleManager_->Draw(
                commandList,
                particleModel_.get(),
                ringModel_.get(),
                cylinderModel_.get(),
                textureSrvHandleGPU_,
                gradationSrvHandleGPU_,
                textSrvHandleGPU_
            );
        }
    }

    // 画面インパクトフラッシュの描画（半透明ブレンド）
    if (flashAlpha_ > 0.0f && graphicsPipeline_ && graphicsPipeline_->GetRootSignature()) {
        commandList->SetGraphicsRootSignature(graphicsPipeline_->GetRootSignature());
        commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        if (graphicsPipeline_->GetPipelineState(kBlendModeNormal)) {
            commandList->SetPipelineState(graphicsPipeline_->GetPipelineState(kBlendModeNormal));
            
            Vector4 colorWithAlpha = { flashColor_.x, flashColor_.y, flashColor_.z, flashAlpha_ };
            
            float flashZ = camera_->GetTransform().translate.z + 1.5f;
            Matrix4x4 worldMatrix = MakeAffineMatrix(Vector3{120.0f, 120.0f, 1.0f}, Vector3{0.0f, 0.0f, 0.0f}, Vector3{0.0f, 0.0f, flashZ});
            
            aimingInstancingData_->World = worldMatrix;
            aimingInstancingData_->WVP = Multiply(worldMatrix, camera_->GetViewProjectionMatrix());
            aimingInstancingData_->color = colorWithAlpha;
            aimingInstancingData_->uvTransform = MakeIdentity4x4();
            
            if (particleModel_) {
                particleModel_->Draw(commandList, 1, TextureManager::GetInstance()->GetSrvHandleGPU("human/white.png"), aimingInstancingSrvHandleGPU_);
            }
        }
    }

    // 2. RenderTextureへの描画終了 ＆ ポストプロセス適用
    postProcess_->PostDraw();

    // 描画先をバックバッファに戻す
    D3D12_CPU_DESCRIPTOR_HANDLE backBufferHandle = dxCommon_->GetCurrentBackBufferRtvHandle();
    D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle = dxCommon_->GetDsvHandle();
    commandList->OMSetRenderTargets(1, &backBufferHandle, false, &dsvHandle);

    // SrvManagerのデスクリプタヒープをセット
    SrvManager::GetInstance()->PreDraw();

    // 適切なパイプラインを選択
    ID3D12PipelineState* pso = nullptr;
    switch (activePostProcess_) {
    case kNone:
    default:
        pso = graphicsPipeline_->GetFullscreenPipelineState();
        break;
    case kGrayscale:
        pso = graphicsPipeline_->GetGrayscalePipelineState();
        break;
    case kSepia:
        pso = graphicsPipeline_->GetSepiaPipelineState();
        break;
    case kVignette:
        pso = graphicsPipeline_->GetVignettePipelineState();
        break;
    case kBoxFilter:
        pso = graphicsPipeline_->GetBoxFilterPipelineState();
        break;
    case kOutline:
        pso = graphicsPipeline_->GetDepthOutlinePipelineState();
        break;
    case kRadialBlur:
        pso = graphicsPipeline_->GetRadialBlurPipelineState();
        break;
    case kDissolve:
        pso = graphicsPipeline_->GetDissolvePipelineState();
        break;
    case kRandom:
        pso = graphicsPipeline_->GetRandomPipelineState();
        break;
    }

    // Fullscreenパイプラインで描画
    commandList->SetPipelineState(pso);
    if (activePostProcess_ == kDissolve) {
        commandList->SetGraphicsRootSignature(graphicsPipeline_->GetDissolveRootSignature());
        SrvManager::GetInstance()->SetGraphicsRootDescriptorTable(0, postProcess_->GetSrvIndex());
        SrvManager::GetInstance()->SetGraphicsRootDescriptorTable(1, activeNoiseSrvIndex_);
        commandList->SetGraphicsRootConstantBufferView(2, dissolveParamResource_->GetGPUVirtualAddress());
    } else if (activePostProcess_ == kOutline) {
        // 深度バッファをPIXEL_SHADER_RESOURCE状態へ安全に遷移
        dxCommon_->TransitionDepthStencilState(D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        commandList->SetGraphicsRootSignature(graphicsPipeline_->GetDepthOutlineRootSignature());
        SrvManager::GetInstance()->SetGraphicsRootDescriptorTable(0, postProcess_->GetSrvIndex()); // t0: カラー
        SrvManager::GetInstance()->SetGraphicsRootDescriptorTable(1, depthSrvIndex_);               // t1: 深度
    } else {
        commandList->SetGraphicsRootSignature(graphicsPipeline_->GetFullscreenRootSignature());
        SrvManager::GetInstance()->SetGraphicsRootDescriptorTable(0, postProcess_->GetSrvIndex());
        if (activePostProcess_ == kVignette && vignetteParamResource_) {
            commandList->SetGraphicsRootConstantBufferView(1, vignetteParamResource_->GetGPUVirtualAddress());
        } else if (activePostProcess_ == kBoxFilter && boxFilterParamResource_) {
            commandList->SetGraphicsRootConstantBufferView(1, boxFilterParamResource_->GetGPUVirtualAddress());
        } else if (activePostProcess_ == kRadialBlur && radialBlurParamResource_) {
            commandList->SetGraphicsRootConstantBufferView(1, radialBlurParamResource_->GetGPUVirtualAddress());
        } else if (activePostProcess_ == kRandom && randomParamResource_) {
            commandList->SetGraphicsRootConstantBufferView(1, randomParamResource_->GetGPUVirtualAddress());
        }
    }
    commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    commandList->DrawInstanced(3, 1, 0, 0);

    // 3. ImGuiデモパネル
#ifdef USE_IMGUI
    ImGui::SetNextWindowSize(ImVec2(480, 580), ImGuiCond_FirstUseEver);
    ImGui::Begin("SPECTACULAR HIT EFFECT SHOWCASE", nullptr, ImGuiWindowFlags_NoCollapse);

    ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "CG4 HIT EFFECT PREVIEWER");
    ImGui::Separator();

    // 攻撃モード切り替えUI
    ImGui::TextColored(ImVec4(0.3f, 0.8f, 1.0f, 1.0f), "Attack Mode Selection: [TAB key to toggle]");
    const char* modeNames[] = { "Shooting Mode (射撃ビーム)", "Melee Mode (打撃・斬撃近接突撃)" };
    int currentModeInt = static_cast<int>(attackMode_);
    if (ImGui::Combo("Attack Mode", &currentModeInt, modeNames, IM_ARRAYSIZE(modeNames))) {
        attackMode_ = static_cast<AttackMode>(currentModeInt);
        meleeState_ = MeleeState::kIdle;
        meleeTimer_ = 0.0f;
        currentFighterPos_ = playerPos_;
    }

    if (attackMode_ == AttackMode::kMelee) {
        const char* stateStr = "Unknown";
        ImVec4 stateColor = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
        switch (meleeState_) {
        case MeleeState::kIdle:   stateStr = "Idle (待機中)"; stateColor = ImVec4(0.7f, 0.7f, 0.7f, 1.0f); break;
        case MeleeState::kDash:   stateStr = "Dash (超高速突進中)"; stateColor = ImVec4(1.0f, 0.5f, 0.0f, 1.0f); break;
        case MeleeState::kHit:    stateStr = "Hit (衝突・エフェクト発生)"; stateColor = ImVec4(1.0f, 0.2f, 0.2f, 1.0f); break;
        case MeleeState::kReturn: stateStr = "Return (元の位置へ帰還中)"; stateColor = ImVec4(0.2f, 0.8f, 1.0f, 1.0f); break;
        }
        ImGui::Text("Melee State: ");
        ImGui::SameLine();
        ImGui::TextColored(stateColor, "%s", stateStr);
    } else {
        ImGui::Text("Shooting Status: Ready");
    }
    ImGui::Separator();

    ImGui::Text("Effect Presets:");
    const char* presetNames[] = {
        "Standard Sparks (通常火花) [1]",
        "Flame Burst (火炎バースト) [2]",
        "Volt Lightning Strike (超高圧放電) [3]",
        "Sonic Blade Slash (真空斬撃) [4]",
        "Gravity Singularity (特異点爆発) [5]",
        "Glacial Freeze (氷結・氷華) [6]",
        "Cyber Spiral (サイバースパイラル) [7]",
        "Aero Wind (風・竜巻) [8]",
        "Holy Light (神聖・天光) [9]",
        "Chaos Void (カオスボイド・闇物質) [0]"
    };
    if (ImGui::Combo("Preset", &selectedEffectPreset_, presetNames, IM_ARRAYSIZE(presetNames))) {
        ApplyPreset(selectedEffectPreset_);
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Text("Sparks Parameters:");
    ImGui::SliderInt("Particle Count", &effectParticleCount_, 10, 150);
    ImGui::SliderFloat("Particle Speed", &effectParticleSpeed_, 5.0f, 40.0f, "%.1f");
    ImGui::SliderFloat("Gravity Multiplier", &effectGravity_, 0.0f, 1.5f, "%.2f");
    ImGui::ColorEdit3("Base Color", &effectBaseColor_.x);

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Text("Dynamic Screen Impact Effects:");
    ImGui::SliderFloat("Shake Intensity", &cameraShakeIntensity_, 0.0f, 5.0f, "%.1f");
    ImGui::SliderFloat("Shake Duration", &cameraShakeTimeMax_, 0.1f, 0.8f, "%.2f");

    ImGui::SliderFloat("Hitstop Duration", &hitstopTimeMax_, 0.0f, 0.4f, "%.2f");

    ImGui::Spacing();
    ImGui::Checkbox("Enable Radial Blur", &useRadialBlur_);
    ImGui::SliderFloat("Max Blur Width", &maxBlurWidth_, 0.0f, 0.15f, "%.3f");

    ImGui::Checkbox("Enable Impact Flash", &useImpactFlash_);

    ImGui::Spacing();
    ImGui::Separator();

    ImGui::Checkbox("Auto-play Mode (自動連続再生)", &autoPlay_);
    if (autoPlay_) {
        ImGui::SliderFloat("Auto-play Interval", &autoPlayInterval_, 0.3f, 4.0f, "%.1f s");
    }

    ImGui::Spacing();
    
    // モードによってラベルと挙動を変更
    std::string buttonLabel = (attackMode_ == AttackMode::kShooting) 
        ? "EMIT BEAM SHOT! (SPACE key)" 
        : "EXECUTE MELEE DASH! (SPACE key)";
        
    if (ImGui::Button(buttonLabel.c_str(), ImVec2(-1, 40))) {
        if (attackMode_ == AttackMode::kShooting) {
            for (int i = 0; i < kMaxShotBeams; ++i) {
                if (!shotBeams_[i].isAlive) {
                    shotBeams_[i].position = playerPos_;
                    Vector3 toTarget = Subtract(targetPos_, playerPos_);
                    shotBeams_[i].velocity = Scale(Normalize(toTarget), 150.0f);
                    shotBeams_[i].isAlive = true;
                    if (audio_) {
                        audio_->PlayWave(jumpSE_, false, 0.35f);
                    }
                    break;
                }
            }
        } else {
            if (meleeState_ == MeleeState::kIdle) {
                meleeState_ = MeleeState::kDash;
                meleeTimer_ = 0.0f;
                if (audio_) {
                    audio_->PlayWave(jumpSE_, false, 0.7f);
                }
            }
        }
    }

    ImGui::Spacing();
    ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), 
        "Controls:\n"
        "[TAB] key: Toggle Attack Mode (Shooting / Melee)\n"
        "[SPACE] key: Trigger Attack / Dash\n"
        "[1] - [0] keys: Select Effect Preset\n"
        "[Z/C] key: Rotate Free Camera Left/Right");
    
    ImGui::End();
#endif
}

void GamePlayScene::ApplyPreset(int presetIndex) {
    selectedEffectPreset_ = presetIndex;
    
    switch (presetIndex) {
    case 0: // 通常スパーク
        effectBaseColor_ = {1.0f, 0.6f, 0.1f};
        effectParticleCount_ = 60;
        effectParticleSpeed_ = 15.0f;
        effectGravity_ = 0.0f;
        cameraShakeIntensity_ = 1.5f;
        cameraShakeTimeMax_ = 0.30f;
        hitstopTimeMax_ = 0.08f;
        useRadialBlur_ = true;
        maxBlurWidth_ = 0.04f;
        useImpactFlash_ = true;
        flashColor_ = {1.0f, 1.0f, 1.0f, 1.0f};
        break;
    case 1: // 火炎バースト
        effectBaseColor_ = {1.0f, 0.35f, 0.0f};
        effectParticleCount_ = 100;
        effectParticleSpeed_ = 12.0f;
        effectGravity_ = -0.1f;
        cameraShakeIntensity_ = 2.8f;
        cameraShakeTimeMax_ = 0.45f;
        hitstopTimeMax_ = 0.12f;
        useRadialBlur_ = true;
        maxBlurWidth_ = 0.06f;
        useImpactFlash_ = true;
        flashColor_ = {1.0f, 0.4f, 0.1f, 1.0f};
        break;
    case 2: // 超高圧放電
        effectBaseColor_ = {0.1f, 0.8f, 1.0f};
        effectParticleCount_ = 75;
        effectParticleSpeed_ = 28.0f;
        effectGravity_ = 0.0f;
        cameraShakeIntensity_ = 2.0f;
        cameraShakeTimeMax_ = 0.15f;
        hitstopTimeMax_ = 0.03f;
        useRadialBlur_ = true;
        maxBlurWidth_ = 0.08f;
        useImpactFlash_ = true;
        flashColor_ = {0.2f, 0.8f, 1.0f, 1.0f};
        break;
    case 3: // 真空斬撃 (揺れなし・フラッシュなし・ストップなし)
        effectBaseColor_ = {0.2f, 1.0f, 0.4f};
        effectParticleCount_ = 50;
        effectParticleSpeed_ = 22.0f;
        effectGravity_ = 0.0f;
        cameraShakeIntensity_ = 0.0f;
        cameraShakeTimeMax_ = 0.0f;
        hitstopTimeMax_ = 0.0f;
        useRadialBlur_ = false;
        maxBlurWidth_ = 0.0f;
        useImpactFlash_ = false;
        flashColor_ = {1.0f, 1.0f, 1.0f, 1.0f};
        break;
    case 4: // 特異点重力爆発 (時間差重揺れ・暗黒フラッシュ)
        effectBaseColor_ = {0.6f, 0.1f, 0.9f};
        effectParticleCount_ = 120;
        effectParticleSpeed_ = 18.0f;
        effectGravity_ = 0.8f;
        cameraShakeIntensity_ = 2.5f;
        cameraShakeTimeMax_ = 0.60f;
        hitstopTimeMax_ = 0.20f;
        useRadialBlur_ = true;
        maxBlurWidth_ = 0.12f;
        useImpactFlash_ = true;
        flashColor_ = {0.15f, 0.0f, 0.25f, 1.0f}; // 暗い黒紫色
        break;
    case 5: // 氷結・氷華 (揺れなし・最大フリーズ・冷気フラッシュ)
        effectBaseColor_ = {0.7f, 0.9f, 1.0f};
        effectParticleCount_ = 80;
        effectParticleSpeed_ = 8.0f;
        effectGravity_ = 0.02f;
        cameraShakeIntensity_ = 0.0f;
        cameraShakeTimeMax_ = 0.0f;
        hitstopTimeMax_ = 0.30f;
        useRadialBlur_ = false;
        maxBlurWidth_ = 0.0f;
        useImpactFlash_ = true;
        flashColor_ = {0.4f, 0.75f, 1.0f, 1.0f};
        break;
    case 6: // デジタルバグ (物理シェイクなし・フラッシュなし・ノイズ画面バグ)
        effectBaseColor_ = {0.0f, 1.0f, 0.3f};
        effectParticleCount_ = 90;
        effectParticleSpeed_ = 25.0f;
        effectGravity_ = 0.0f;
        cameraShakeIntensity_ = 0.0f;
        cameraShakeTimeMax_ = 0.0f;
        hitstopTimeMax_ = 0.0f;
        useRadialBlur_ = false;
        maxBlurWidth_ = 0.0f;
        useImpactFlash_ = false;
        flashColor_ = {0.0f, 1.0f, 0.2f, 1.0f};
        break;
    case 7: // 風・竜巻 (緩い横揺れ・フラッシュなし)
        effectBaseColor_ = {0.6f, 0.95f, 0.75f};
        effectParticleCount_ = 70;
        effectParticleSpeed_ = 18.0f;
        effectGravity_ = -0.05f;
        cameraShakeIntensity_ = 0.8f;
        cameraShakeTimeMax_ = 0.30f;
        hitstopTimeMax_ = 0.04f;
        useRadialBlur_ = true;
        maxBlurWidth_ = 0.045f;
        useImpactFlash_ = false;
        flashColor_ = {0.7f, 1.0f, 0.8f, 1.0f};
        break;
    case 8: // 神聖・天光 (揺れなし・神々しい黄金フラッシュ)
        effectBaseColor_ = {1.0f, 0.95f, 0.6f};
        effectParticleCount_ = 80;
        effectParticleSpeed_ = 14.0f;
        effectGravity_ = 0.05f;
        cameraShakeIntensity_ = 0.0f;
        cameraShakeTimeMax_ = 0.0f;
        hitstopTimeMax_ = 0.10f;
        useRadialBlur_ = true;
        maxBlurWidth_ = 0.09f;
        useImpactFlash_ = true;
        flashColor_ = {1.0f, 0.95f, 0.5f, 1.0f};
        break;
    case 9: // カオスボイド・闇物質 (最大全軸揺れ・長ヒットストップ)
        effectBaseColor_ = {0.45f, 0.05f, 0.75f};
        effectParticleCount_ = 130;
        effectParticleSpeed_ = 20.0f;
        effectGravity_ = 0.5f;
        cameraShakeIntensity_ = 4.0f;
        cameraShakeTimeMax_ = 0.75f;
        hitstopTimeMax_ = 0.25f;
        useRadialBlur_ = true;
        maxBlurWidth_ = 0.14f;
        useImpactFlash_ = true;
        flashColor_ = {0.35f, 0.0f, 0.55f, 1.0f};
        break;
    }
}


static Vector3 CatmullRom(const Vector3& p0, const Vector3& p1, const Vector3& p2, const Vector3& p3, float t) {
    float t2 = t * t;
    float t3 = t2 * t;

    Vector3 a = Scale(p1, 2.0f);
    Vector3 b = Subtract(p2, p0);
    Vector3 c = Subtract(Add(Scale(p0, 2.0f), Scale(p2, 4.0f)), Add(Scale(p1, 5.0f), p3));
    Vector3 d = Add(Subtract(Scale(p1, 3.0f), p0), Subtract(p3, Scale(p2, 3.0f)));

    return Scale(Add(Add(a, Scale(b, t)), Add(Scale(c, t2), Scale(d, t3))), 0.5f);
}

static Vector3 CatmullRomDerivative(const Vector3& p0, const Vector3& p1, const Vector3& p2, const Vector3& p3, float t) {
    float t2 = t * t;

    Vector3 b = Subtract(p2, p0);
    Vector3 c = Subtract(Add(Scale(p0, 2.0f), Scale(p2, 4.0f)), Add(Scale(p1, 5.0f), p3));
    Vector3 d = Add(Subtract(Scale(p1, 3.0f), p0), Subtract(p3, Scale(p2, 3.0f)));

    return Scale(Add(b, Add(Scale(c, 2.0f * t), Scale(d, 3.0f * t2))), 0.5f);
}

Vector3 GamePlayScene::GetRailPosition(float progress) {
    if (waypoints_.empty()) {
        return { kWorldShiftX, -20.0f, progress };
    }
    if (waypoints_.size() == 1) {
        return waypoints_[0];
    }
    
    auto it = std::lower_bound(waypointDistances_.begin(), waypointDistances_.end(), progress);
    
    if (it == waypointDistances_.begin()) {
        Vector3 dir = Normalize(Subtract(waypoints_[1], waypoints_[0]));
        float dist = progress - waypointDistances_[0];
        return Add(waypoints_[0], Scale(dir, dist));
    }
    
    if (it == waypointDistances_.end()) {
        size_t size = waypoints_.size();
        Vector3 dir = Normalize(Subtract(waypoints_[size - 1], waypoints_[size - 2]));
        float dist = progress - waypointDistances_[size - 1];
        return Add(waypoints_[size - 1], Scale(dir, dist));
    }
    
    size_t index = std::distance(waypointDistances_.begin(), it);
    const Vector3& p2 = waypoints_[index];
    const Vector3& p1 = waypoints_[index - 1];
    float d2 = waypointDistances_[index];
    float d1 = waypointDistances_[index - 1];
    
    float denom = d2 - d1;
    if (std::abs(denom) < 0.001f) {
        return p1;
    }
    
    float t = (progress - d1) / denom;

    // Catmull-Rom スプライン補間
    const Vector3& p0 = (index > 1) ? waypoints_[index - 2] : Add(p1, Subtract(p1, p2));
    const Vector3& p3 = (index < waypoints_.size() - 1) ? waypoints_[index + 1] : Add(p2, Subtract(p2, p1));

    return CatmullRom(p0, p1, p2, p3, t);
}

Vector3 GamePlayScene::GetRailDirection(float progress) {
    if (waypoints_.empty()) {
        return { 0.0f, 0.0f, 1.0f };
    }
    if (waypoints_.size() == 1) {
        return { 0.0f, 0.0f, 1.0f };
    }
    
    auto it = std::lower_bound(waypointDistances_.begin(), waypointDistances_.end(), progress);
    
    if (it == waypointDistances_.begin()) {
        return Normalize(Subtract(waypoints_[1], waypoints_[0]));
    }
    
    if (it == waypointDistances_.end()) {
        size_t size = waypoints_.size();
        return Normalize(Subtract(waypoints_[size - 1], waypoints_[size - 2]));
    }
    
    size_t index = std::distance(waypointDistances_.begin(), it);
    const Vector3& p2 = waypoints_[index];
    const Vector3& p1 = waypoints_[index - 1];
    float d2 = waypointDistances_[index];
    float d1 = waypointDistances_[index - 1];

    float denom = d2 - d1;
    if (std::abs(denom) < 0.001f) {
        return Normalize(Subtract(p2, p1));
    }

    float t = (progress - d1) / denom;

    const Vector3& p0 = (index > 1) ? waypoints_[index - 2] : Add(p1, Subtract(p1, p2));
    const Vector3& p3 = (index < waypoints_.size() - 1) ? waypoints_[index + 1] : Add(p2, Subtract(p2, p1));

    Vector3 derivative = CatmullRomDerivative(p0, p1, p2, p3, t);
    float len = Length(derivative);
    if (len > 0.0001f) {
        return Scale(derivative, 1.0f / len);
    }
    
    return Normalize(Subtract(p2, p1));
}

Vector3 GamePlayScene::CalculateRailRight(const Vector3& dir) {
    Vector3 up = { 0.0f, 1.0f, 0.0f };
    if (std::abs(dir.y) > 0.999f) {
        up = { 0.0f, 0.0f, -1.0f };
    }
    return Normalize(Cross(up, dir));
}

Vector3 GamePlayScene::CalculateRailUp(const Vector3& dir, const Vector3& right) {
    return Cross(dir, right);
}

Vector3 GamePlayScene::GetBossPosition(float bodyBounce, float dropOffset, float yAttackOffset) {
    float bossProgress = fighterWorldZ_ + bossZOffset_;
    Vector3 railPos = GetRailPosition(bossProgress);
    Vector3 railDir = GetRailDirection(bossProgress);
    Vector3 railRight = Normalize(Vector3{ railDir.z, 0.0f, -railDir.x });
    Vector3 railUp = Cross(railDir, railRight);
    
    float relativeY = (bossYOffset_ + 20.0f) + bodyBounce + dropOffset + yAttackOffset;
    
    return Add(railPos, Scale(railUp, relativeY));
}

