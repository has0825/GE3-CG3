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
}

// staticメンバの定義
int GamePlayScene::blenderSyncCounter_ = 0;

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
            outfile << "LIMIT_X=20.0\n";
            outfile << "LIMIT_Y=12.0\n";
            outfile << "COLLISION_RADIUS=2.0\n";
            outfile << "SPEED_X=25.0\n";
            outfile << "SPEED_Y=20.0\n";
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
    debrisModel_ = std::unique_ptr<Model>(Model::CreateParticleModel(device));
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
    dissolveParamData_->edgeColor = { 1.0f, 0.4f, 0.3f };
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

    // 初期フォーメーション位置の適用
    for (int g = 0; g < kNumGroups; ++g) {
        ApplyGroupFormation(g);
    }

    // 最初はすべてのグループをアクティブ化して配置 (Z距離がそれぞれ分散されます)
    activeGroupIndex_ = 0;
    for (int g = 0; g < kNumGroups; ++g) {
        RespawnEnemyGroup(g, fighterWorldZ_);
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
    // 3列（中央・右・左）分のバッファを確保し、各列を kRoadColumnXOffset ずつX方向にオフセットして並べる
    floorModel_ = std::unique_ptr<Model>(Model::LoadGLTF("Resources/plane.obj", device));
    TextureManager::GetInstance()->LoadTexture("douro.jpg");

    // X方向オフセット: lane=0→中央(0), lane=1→右(+offset), lane=2→左(-offset)
    const float laneOffsets[kNumRoadLanes] = { 0.0f, +kRoadColumnXOffset, -kRoadColumnXOffset };
    for (int lane = 0; lane < kNumRoadLanes; ++lane) {
        for (int col = 0; col < kNumFloorColumns; ++col) {
            int idx = lane * kNumFloorColumns + col;
            floorTransformResources_[idx] = CreateBufferResource(device, sizeof(TransformationMatrix));
            floorTransformResources_[idx]->Map(0, nullptr, reinterpret_cast<void**>(&floorTransformData_[idx]));
            floorTransformData_[idx]->WVP   = MakeIdentity4x4();
            floorTransformData_[idx]->World = MakeIdentity4x4();

            floorPositions_[idx] = { laneOffsets[lane], kFloorY, (float)col * kFloorSizeZ };
        }
    }

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

    // 地面の破片用テクスチャのロードと定数バッファ生成
    TextureManager::GetInstance()->LoadTexture("isihahen.png");
    for (int i = 0; i < kMaxDebris; ++i) {
        debrisTransformResources_[i] = CreateBufferResource(device, sizeof(TransformationMatrix));
        debrisTransformResources_[i]->Map(0, nullptr, reinterpret_cast<void**>(&debrisTransformData_[i]));
        debrisTransformData_[i]->WVP = MakeIdentity4x4();
        debrisTransformData_[i]->World = MakeIdentity4x4();
    }
    debris_.resize(kMaxDebris);
    for (int i = 0; i < kMaxDebris; ++i) {
        debris_[i].isAlive = false;
    }

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
        
        // 自機とカメラのZ前進は継続（ボスの位置との整合性を保つため）
        fighterWorldZ_ += boostForwardSpeed_ * kDeltaTime;
        camTrans.translate.z = fighterWorldZ_ - 65.0f;
        
        // スターフォックス風のカメラX/Y追従も中央に戻す
        camTrans.translate.x = std::lerp(camTrans.translate.x, 0.0f, 0.08f);
        camTrans.translate.y = std::lerp(camTrans.translate.y, 0.0f, 0.08f);
        camTrans.rotate.x = std::lerp(camTrans.rotate.x, 0.0f, 0.08f);
        camTrans.rotate.y = std::lerp(camTrans.rotate.y, 0.0f, 0.08f);
        camTrans.rotate.z = std::lerp(camTrans.rotate.z, 0.0f, 0.08f);

        // 2. トドメの巨大弾の自動発射 (0.8秒時点)
        if (bossDefeatTimer_ >= 0.8f && !isDefeatBulletActive_ && !hasHitBoss_) {
            isDefeatBulletActive_ = true;
            
            // 自機のワールド位置
            Vector3 fighterWorldPos = {
                camTrans.translate.x + (fighterModel_ ? fighterModel_->transform.translate.x : 0.0f),
                camTrans.translate.y - 3.0f + (fighterModel_ ? fighterModel_->transform.translate.y : 0.0f),
                fighterWorldZ_
            };
            
            defeatBulletPos_ = fighterWorldPos;
            
            // ボスのワールド位置
            float bodyBounce = 0.0f;
            if (bossLegSwingSpeed_ > 0.0f) {
                bodyBounce = std::sin(bossTime_ * 2.0f) * bossBodyBounceRange_;
            }
            Vector3 bossPos = { 0.0f, bossYOffset_ + bodyBounce, fighterWorldZ_ + bossZOffset_ };
            
            // 弾の速度ベクトル (ボスへ向かう方向)
            Vector3 dir = Normalize(Subtract(bossPos, defeatBulletPos_));
            float bulletSpeed = 380.0f; // より高速に射撃
            defeatBulletVel_ = Scale(dir, bulletSpeed);
            
            // 発射SE
            if (audio_) {
                audio_->PlayWave(jumpSE_, false, 1.2f);
            }
        }
        
        // 3. トドメの巨大弾の更新とヒット判定
        if (isDefeatBulletActive_) {
            defeatBulletPos_ = Add(defeatBulletPos_, Scale(defeatBulletVel_, kDeltaTime));
            
            // トレイルパーティクル (白く輝くスパークを毎フレーム放出)
            for (int i = 0; i < 2; ++i) {
                particleManager_->EmitHolyLight(defeatBulletPos_, 2.0f, 1, {1.0f, 1.0f, 1.0f});
            }
            
            // ボスのワールド位置
            float bodyBounce = 0.0f;
            if (bossLegSwingSpeed_ > 0.0f) {
                bodyBounce = std::sin(bossTime_ * 2.0f) * bossBodyBounceRange_;
            }
            Vector3 bossPos = { 0.0f, bossYOffset_ + bodyBounce, fighterWorldZ_ + bossZOffset_ };
            
            // 衝突判定 (ボスの衝突半径または弾がボスを追い抜いたか)
            Vector3 diff = Subtract(defeatBulletPos_, bossPos);
            float dist = Length(diff);
            if (dist <= bossCollisionRadius_ || defeatBulletPos_.z >= bossPos.z) {
                isDefeatBulletActive_ = false;
                hasHitBoss_ = true;
                isBossModelVisible_ = false; // ボスを消滅させる
                bossDefeatHitTimer_ = 0.0f;  // ヒットした瞬間からタイマーを開始
                
                // 大きく白い十字Particleエフェクトを発生
                particleManager_->EmitWhiteCross(bossPos);
                
                // 白い超巨大多重衝撃波リングと巨大シリンダーエフェクトを追加してかっこよく演出
                particleManager_->EmitMegaRing(bossPos, {1.0f, 1.0f, 1.0f});
                particleManager_->EmitMegaCylinder(bossPos, {1.0f, 1.0f, 1.0f});
                
                // 周囲に飛び散る純白の激しいスパークエフェクトを追加
                particleManager_->EmitFlame(bossPos, 20.0f, 30, {1.0f, 1.0f, 1.0f});
                
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
                cameraShakeIntensity_ = 10.0f;
                activeShakeIntensity_ = 10.0f;
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
                bossAppearanceTimer_ = 3.0f; // ボス登場演出開始（3秒）
            }
        }
    }

    // ── ボス登場落下演出タイマーの更新 ──
    if (bossAppearanceTimer_ > 0.0f) {
        bossAppearanceTimer_ -= kDeltaTime;
        if (bossAppearanceTimer_ <= 0.0f) {
            bossAppearanceTimer_ = -1.0f;
            // 着地！画面シェイクと土煙・SE
            cameraShakeTimer_ = 0.50f;
            cameraShakeIntensity_ = 3.5f;
            activeShakeIntensity_ = 3.5f;
            cameraShakeTimeMax_ = 0.50f;
            if (audio_) {
                audio_->PlayWave(jumpSE_, false, 2.0f); // 大音量で着地SE
            }
            // 土煙パーティクル
            Vector3 landPos = { 0.0f, bossYOffset_, fighterWorldZ_ + bossZOffset_ };
            for (int k = 0; k < 12; ++k) {
                particleManager_->EmitHit(landPos);
            }
            particleManager_->EmitRing(landPos);
            particleManager_->EmitCylinder(landPos);
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
    static Vector3 gpuParticlePos = { -10.0f, 0.0f, 0.0f };
    if (ImGui::DragFloat3("GPU Particle Position", &gpuParticlePos.x, 0.1f)) {
        if (gpuParticleManager_) gpuParticleManager_->SetTranslate(gpuParticlePos);
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
                repFileHdr << ",boss_active,boss_x,boss_y,boss_z\n";
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
            bossAppearanceTimer_ = 3.0f; // ボス登場演出（落下）開始（3秒）
            phaseTimer_ = 0.0f;          // 通常フェーズのタイマーをリセット
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
        if (isBoosting_) {
            // バレルロール進行度を正弦波（0〜1〜0）にマップしてカメラを後方に引き離す
            float t = barrelRollTimer_ / kBarrelRollDuration;
            float wave = std::sin(t * static_cast<float>(M_PI));

            // ブースト時カメラ引き離しG演出（通常65m ➔ 最大120m引き離す）
            float currentDist = 65.0f + wave * 55.0f;
            camTrans.translate.z = fighterWorldZ_ - currentDist;
        } else {
            // 通常時はプレイヤーと完全に等速（遅延lerpなしの直接代入）
            camTrans.translate.z = fighterWorldZ_ - 65.0f;
        }

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
                fighterModel_->transform.translate.x += inputDir.x * playerSpeedX_ * speedFactor * kDeltaTime;
                fighterModel_->transform.translate.y += inputDir.y * playerSpeedY_ * speedFactor * kDeltaTime;
            }

            fighterModel_->transform.translate.x = std::clamp(fighterModel_->transform.translate.x, -playerLimitX_, playerLimitX_);
            fighterModel_->transform.translate.y = std::clamp(fighterModel_->transform.translate.y, -playerLimitY_, playerLimitY_);

            // スターフォックス風のカメラX/Y追従
            float cameraLag = 0.08f;
            float targetCamX = fighterModel_->transform.translate.x * 0.5f;
            float targetCamY = fighterModel_->transform.translate.y * 0.5f;
            camTrans.translate.x = std::lerp(camTrans.translate.x, targetCamX, cameraLag);
            camTrans.translate.y = std::lerp(camTrans.translate.y, targetCamY, cameraLag);

            // ── Star Fox / Ex-Zodiac 風のダイナミックなカメラの首振り（ピッチ・ヨー・ロール） ──
            float targetCamRotateY = fighterModel_->transform.translate.x * 0.003f;
            float targetCamRotateX = -fighterModel_->transform.translate.y * 0.003f;
            
            float targetCamRotateZ = -inputDir.x * 0.03f;
            if (isBarrelRolling_) {
                float rollT = barrelRollTimer_ / kBarrelRollDuration;
                targetCamRotateZ += std::sin(rollT * static_cast<float>(M_PI)) * 0.15f;
            }

            camTrans.rotate.x = std::lerp(camTrans.rotate.x, targetCamRotateX, 0.08f);
            camTrans.rotate.y = std::lerp(camTrans.rotate.y, targetCamRotateY, 0.08f);
            camTrans.rotate.z = std::lerp(camTrans.rotate.z, targetCamRotateZ, 0.08f);

            // ── プレイヤーのワールド座標（Z はfighterWorldZ_を直接使う）──
            Vector3 fighterWorldPos = {
                camTrans.translate.x + fighterModel_->transform.translate.x,
                camTrans.translate.y - 3.0f + fighterModel_->transform.translate.y,
                fighterWorldZ_   // ← カメラZ+固定オフセットではなく、独立Z座標を使用
            };

            // ── 4. バレルロール開始（LSHIFTのブースト切り替え時に連動） ──

            // ── 5. 機体の傾き（通常ロール/ピッチ + バレルロール合成） ──
            float baseRoll  = inputDir.x * -0.6f;
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
            fighterModel_->transform.rotate.x = playerRotationPitch_;

            fighterTransformData_->World = MakeAffineMatrix(
                fighterModel_->transform.scale,
                fighterModel_->transform.rotate,
                fighterWorldPos
            );

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
                        repFile << ",boss_active,boss_x,boss_y,boss_z\n";
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
                    bool bossActiveRec = (currentPhase_ == GamePhase::kBossFight);
                    repFile << "," << (bossActiveRec ? 1 : 0)
                            << ",0.0"
                            << "," << (bossYOffset_ + bodyBounceRec + dropOffsetRec)
                            << "," << (fighterWorldZ_ + bossZOffset_) << "\n";
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
            Vector3 defaultReticlePos = { fighterWorldPos.x, fighterWorldPos.y, fighterWorldPos.z + 120.0f };

            float bestDist2D = 30.0f;
            Enemy* lockedEnemy = nullptr;
            for (auto& enemy : enemies_) {
                if (!enemy.isAlive) continue;
                if (enemy.position.z > fighterWorldPos.z) {
                    float dx = enemy.position.x - fighterWorldPos.x;
                    float dy = enemy.position.y - fighterWorldPos.y;
                    float dist2D = std::sqrt(dx * dx + dy * dy);
                    if (dist2D < bestDist2D) {
                        bestDist2D = dist2D;
                        lockedEnemy = &enemy;
                    }
                }
            }
            Vector3 targetReticlePos = lockedEnemy ? lockedEnemy->position : defaultReticlePos;
            float aimLerpSpeed = 0.05f;
            aimReticlePos_.x = std::lerp(aimReticlePos_.x, targetReticlePos.x, aimLerpSpeed);
            aimReticlePos_.y = std::lerp(aimReticlePos_.y, targetReticlePos.y, aimLerpSpeed);
            aimReticlePos_.z = std::lerp(aimReticlePos_.z, targetReticlePos.z, aimLerpSpeed);
            Vector3 reticlePos = aimReticlePos_;

            if (input_->IsKeyTriggered(DIK_SPACE) && !(phaseIntroTimer_ >= 0.0f) && !isBossDefeatedSequence_) {
                Vector3 leftWing  = { fighterWorldPos.x - 2.5f, fighterWorldPos.y + 0.8f, fighterWorldPos.z };
                Vector3 rightWing = { fighterWorldPos.x + 2.5f, fighterWorldPos.y + 0.8f, fighterWorldPos.z };

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
            if (currentPhase_ == GamePhase::kBossFight) {
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
                Vector3 bossPos = { 0.0f, bossYOffset_ + bodyBounce + dropOffset, fighterWorldZ_ + bossZOffset_ };
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
                // 雑魚敵との衝突
                for (auto& enemy : enemies_) {
                    if (!enemy.isAlive) continue;

                    Vector3 diff = Subtract(fighterWorldPos, enemy.position);
                    float dist = Length(diff);
                    
                    bool hit = false;
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

                if (currentPhase_ == GamePhase::kBossFight) {
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
                Vector3 bossPos = { 0.0f, bossYOffset_ + bodyBounce + dropOffset, fighterWorldZ_ + bossZOffset_ };

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

                    Vector3 diff = Subtract(playerBullets_[i].position, enemy.position);
                    float dist = Length(diff);
                    // 弾の当たり判定半径を広げて（0.5f -> 2.5f）、すり抜けや近距離で当たらない現象を緩和
                    if (dist <= (enemy.radius + 2.5f)) {
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
    if (currentPhase_ == GamePhase::kBossFight && !isBossDefeatedSequence_) {
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
                
                // 振りかぶり中にプレイヤーの位置を追従し、ターゲットエリアを決定
                if (fighterWorldPos.x < -15.0f) {
                    bossAttackTargetArea_ = 0; // 左
                } else if (fighterWorldPos.x > 15.0f) {
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
                Vector3 warningPos = { targetX, bossYOffset_ - 6.0f, fighterWorldZ_ + 65.0f };
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
                Vector3 warningPos = { targetX, bossYOffset_ - 6.0f, fighterWorldZ_ + 65.0f };
                particleManager_->EmitLightning(currentBossPos, 12.0f, 2, lightningColor);
                particleManager_->EmitLightning(warningPos, 12.0f, 2, lightningColor);

            } else if (bossActionTimer_ <= 6.8f) {
                // 3. 戻り (5.3〜6.8秒)
                // ─── 巨大な「1本の雷」の3フェーズタイムライン (先行微光➔本落雷➔残光明滅) ───
                float strikeElapsed = bossActionTimer_ - 5.3f; // 激突からの経過時間

                if (strikeElapsed <= 0.05f) {
                    // ① 先行微光 (Leader) [0.00s 〜 0.05s / 約3フレーム]
                    // 細く暗めの紫色の線をターゲットに向けて数本走らせる
                    float targetX = 0.0f;
                    if (bossAttackTargetArea_ == 0) targetX = -30.0f;
                    else if (bossAttackTargetArea_ == 2) targetX = 30.0f;
                    Vector3 impactPos = { targetX, bossYOffset_ - 6.0f, fighterWorldZ_ + 65.0f };
                    Vector3 lightningStart = { currentBossPos.x, currentBossPos.y - 4.0f, currentBossPos.z - 15.0f };
                    
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
                    // 地面に激突した瞬間。主幹が極太(純白コア入り)になり爆発！
                    float targetX = 0.0f;
                    if (bossAttackTargetArea_ == 0) targetX = -30.0f;
                    else if (bossAttackTargetArea_ == 2) targetX = 30.0f;
                    Vector3 impactPos = { targetX, bossYOffset_ - 6.0f, fighterWorldZ_ + 65.0f };
                    Vector3 lightningStart = { currentBossPos.x, currentBossPos.y - 4.0f, currentBossPos.z - 15.0f };

                    if (!hasShaken) {
                        cameraShakeTimer_ = 0.78f;
                        cameraShakeIntensity_ = 10.5f; // 最大震度
                        activeShakeIntensity_ = 10.5f;
                        cameraShakeTimeMax_ = 0.78f;
                        hasShaken = true;

                        // 地面の破片を飛び散らせる
                        SpawnDebris(impactPos);

                        // 極太の主幹雷撃(1.6fスケール)を走らせる（内部で純白コアとまとわりつく枝が自動生成される）
                        particleManager_->EmitLSystemLightning(lightningStart, impactPos, 4, 1.6f, lightningColor);

                        // 周囲の爆発エフェクトと大音響SE
                        particleManager_->EmitCylinder(impactPos, lightningColor);
                        particleManager_->EmitRing(impactPos, lightningColor);
                        for (int j = 0; j < 15; ++j) {
                            particleManager_->EmitHit(impactPos);
                        }
                        audio_->PlayWave(jumpSE_, false, 1.7f);

                        // ダメージ判定
                        int playerArea = 1;
                        if (fighterWorldPos.x < -15.0f) playerArea = 0;
                        else if (fighterWorldPos.x > 15.0f) playerArea = 2;
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
                    // 形状を維持したまま、全体の輝度を激しくバチバチ明滅させながら減衰
                    float currentIntensity = 1.0f - (strikeElapsed - 0.10f) / 1.4f; // 1.0 ➔ 0.0
                    currentIntensity = (std::max)(0.0f, currentIntensity);

                    // 高速なバチバチ明滅（サイン波による強度の上下）
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
                            Vector3 impactPos = { targetX, bossYOffset_ - 6.0f, fighterWorldZ_ + 65.0f };
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
            float appRate = (3.0f - bossAppearanceTimer_) / 3.0f;
            dropOffset = 120.0f * (1.0f - appRate) * (1.0f - appRate);
        }

        // 胴体のワールド行列計算
        // ボスはプレイヤーの前方 bossZOffset_ の位置に進み、高さは接地高さ bossYOffset_
        Vector3 bossPos = { 0.0f, bossYOffset_ + bodyBounce + bossYAttackOffset + dropOffset, fighterWorldZ_ + bossZOffset_ };
        Vector3 bossBodyScale = { bossBodyScale_, bossBodyScale_, bossBodyScale_ }; // 胴体専用スケール
        // Y軸回転に加えて、Z軸のロール回転を合成
        Vector3 bossRotate = { 0.0f, bossBodyRotY_ * (float)M_PI / 180.0f, bodyRoll };

        Matrix4x4 bossWorld = MakeAffineMatrix(bossBodyScale, bossRotate, bossPos);
        bossBodyTransformData_->World = bossWorld;

        // 胴体の「スケールなし」の行列（足の大きさを胴体から完全に独立させるために使用、胴体の揺れも同期）
        Matrix4x4 bossWorldNoScale = MakeAffineMatrix(Vector3{ 1.0f, 1.0f, 1.0f }, bossRotate, bossPos);

        // 4組の左右対称な足ペアパラメータを配列化してアクセス
        Vector3 legPairPos[4] = { bossLegPairPos0_, bossLegPairPos1_, bossLegPairPos2_, bossLegPairPos3_ };
        float legPairRotY[4] = { bossLegPairRotY0_, bossLegPairRotY1_, bossLegPairRotY2_, bossLegPairRotY3_ };

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
                    Vector3 targetWorldPos = { targetX, fighterWorldPos.y, fighterWorldPos.z };

                    float angleY = -bossBodyRotY_ * (float)M_PI / 180.0f;
                    Vector3 diff = Subtract(targetWorldPos, bossPos);
                    Vector3 playerLocal = {
                        diff.x * std::cos(angleY) - diff.z * std::sin(angleY),
                        diff.y,
                        diff.x * std::sin(angleY) + diff.z * std::cos(angleY)
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
                    Vector3 targetWorldPos = { targetX, fighterWorldPos.y, fighterWorldPos.z };

                    float angleY = -bossBodyRotY_ * (float)M_PI / 180.0f;
                    Vector3 diff = Subtract(targetWorldPos, bossPos);
                    Vector3 playerLocal = {
                        diff.x * std::cos(angleY) - diff.z * std::sin(angleY),
                        diff.y,
                        diff.x * std::sin(angleY) + diff.z * std::cos(angleY)
                    };
                    
                    Vector3 jointLocal = finalOffset;
                    Vector3 scaledJointLocal = Scale(jointLocal, bossScale_);
                    Vector3 dirLocal = Subtract(playerLocal, scaledJointLocal);
                    
                    float len = std::sqrt(dirLocal.x * dirLocal.x + dirLocal.y * dirLocal.y + dirLocal.z * dirLocal.z);
                    if (len > 0.01f) {
                        Vector3 dirNorm = { dirLocal.x / len, dirLocal.y / len, dirLocal.z / len };
                        
                        float targetRotY = baseRotY * (float)M_PI / 180.0f - std::atan2(dirNorm.x, dirNorm.z);
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
            fighterWorldPos = {
                camTransForEnemy.translate.x + fighterModel_->transform.translate.x,
                camTransForEnemy.translate.y - 3.0f + fighterModel_->transform.translate.y,
                fighterWorldZ_
            };
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
                    // 1. 横側で待機: プレイヤーが一定距離内 (Z軸で150m) に近づくまでその場で待機（プレイヤーとの距離短縮対応）
                    float distZ = enemy.position.z - fighterWorldZ_;
                    if (distZ > 0.0f && distZ < 150.0f) {
                        enemy.state = Enemy::State::kAppear;
                        enemy.stateTimer = 0.0f;
                        // 出現時の実際の相対Z距離を基準にキープZを設定 (最大120m)
                        enemy.relativeZ = (std::min)(120.0f, distZ);
                        // 出現開始時の位置を記憶
                        enemy.appearStartPos = enemy.position;
                    }
                }
                else if (enemy.state == Enemy::State::kAppear) {
                    // 2. 中央へ移動: 待機位置からフォーメーション目標位置 (wanderAnchor) に向けてイージング＋カーブで合流（Star Fox風）
                    enemy.wanderAnchor.z = fighterWorldZ_ + enemy.relativeZ;

                    float kAppearDuration = 1.2f; // 1.2秒かけて合流
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
                    enemy.wanderAnchor.z = fighterWorldZ_ + enemy.relativeZ;
                    
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
                        Vector3 playerTarget = { fighterWorldPos.x, fighterWorldPos.y, fighterWorldZ_ };
                        Vector3 toPlayer = Subtract(playerTarget, enemy.position);
                        enemy.diveDirection = Normalize(toPlayer);
                        enemy.speed = 155.0f + boostForwardSpeed_; // プレイヤーの速度に合わせて特攻速度を上げる (ブースト対応)
                    }
                }
                else if (enemy.state == Enemy::State::kDive) {
                    // 4. 特攻状態: プレイヤーに向けて直線的に高速突進 (Z追従なし)
                    enemy.position = Add(enemy.position, Scale(enemy.diveDirection, enemy.speed * kDeltaTime));
                    
                    // 特攻中の回転
                    enemy.rotate.z += kDeltaTime * 18.0f;
                    enemy.rotate.x += kDeltaTime * 6.0f;
                    enemy.rotate.y += kDeltaTime * 4.0f;
                    
                    // プレイヤーを通り過ぎて後方20mに行ったら、非生存化してリポップ対象に
                    if (enemy.position.z < fighterWorldZ_ - 20.0f) {
                        enemy.isAlive = false;
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
                Matrix4x4 worldMatrix = MakeAffineMatrix(enemies_[i].scale, enemies_[i].rotate, enemies_[i].position);
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
        float cameraZ = camera_->GetTransform().translate.z;

        // ビルの画面外再配置（ボス戦中も進行感を出すため常に処理）
        {
            for (int i = 0; i < kMaxBuildings; ++i) {
                // カメラの後方(間隔分)を超えたら、遥か前方（最前方のビルペアの先）に再配置
                if (buildings_[i].position.z < cameraZ - kBuildingInterval) {
                    buildings_[i].position.z += (float)kMaxBuildings * kBuildingInterval * 0.25f;
                    
                    // 破壊されているビルを元のきれいな状態にリセット
                    int columnType = i % 4;
                    if (columnType == 0) buildings_[i].position.x = -45.0f;
                    else if (columnType == 1) buildings_[i].position.x = 45.0f;
                    else if (columnType == 2) buildings_[i].position.x = -85.0f;
                    else buildings_[i].position.x = 85.0f;
                    
                    buildings_[i].position.y = kFloorY;
                    buildings_[i].rotate = { 0.0f, 0.0f, 0.0f };
                    buildings_[i].isDestroyed = false;
                    buildings_[i].velocity = { 0.0f, 0.0f, 0.0f };
                    buildings_[i].rotationSpeed = { 0.0f, 0.0f, 0.0f };
                    buildings_[i].destroyTimer = 0.0f;

                    // 再配置した際にビルの階数を再抽選する（内側は1〜5階、外側は5〜10階）
                    if (columnType == 2 || columnType == 3) {
                        buildings_[i].floors = 5 + (randomEngine_() % 6); // 外側のビルは高め
                    } else {
                        buildings_[i].floors = 1 + (randomEngine_() % 5); // 内側のビル
                    }
                }
            }
        }

        // ── ビルの物理シミュレーションとボス衝突判定 ──
        {
            Vector3 bossPos = { 0.0f, 0.0f, 0.0f };
            bool isBossActive = (currentPhase_ == GamePhase::kBossFight && !isBossDefeatedSequence_);
            if (isBossActive) {
                float bodyBounce = std::sin(bossTime_ * 2.0f) * bossBodyBounceRange_;
                bossPos = { 0.0f, bossYOffset_ + bodyBounce, fighterWorldZ_ + bossZOffset_ };
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
                    // ボスとの衝突判定
                    if (isBossActive) {
                        float diffZ = buildings_[i].position.z - bossPos.z;
                        float diffX = buildings_[i].position.x - bossPos.x;

                        // ボスのZ位置がビルのZ位置と交差し、かつ横幅の範囲にある場合
                        if (std::abs(diffZ) < 30.0f && std::abs(diffX) < 55.0f) {
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

        // 床の画面外再配置（ボス戦中も進行感を維持するため、常に再配置する）
        // 各列（中央・右・左）ごとにZ方向のみスクロールさせ、X座標は変えない
        for (int lane = 0; lane < kNumRoadLanes; ++lane) {
            // この列の中でZが最大のタイルを探す
            float laneMaxZ = -9999.0f;
            for (int col = 0; col < kNumFloorColumns; ++col) {
                int idx = lane * kNumFloorColumns + col;
                if (floorPositions_[idx].z > laneMaxZ) {
                    laneMaxZ = floorPositions_[idx].z;
                }
            }
            // カメラ後方に出たタイルを前方へ再配置（X座標はそのまま保持）
            for (int col = 0; col < kNumFloorColumns; ++col) {
                int idx = lane * kNumFloorColumns + col;
                if (floorPositions_[idx].z < cameraZ - kFloorSizeZ) {
                    floorPositions_[idx].z = laneMaxZ + kFloorSizeZ;
                    laneMaxZ = floorPositions_[idx].z; // 複数タイルが同フレームで再配置される場合に備えて更新
                }
            }
        }

        // 各ビルの各階数（フロア）ごとにワールド行列とWVP行列を計算
        {
            int cbIndex = 0;
            for (int i = 0; i < kMaxBuildings; ++i) {
                for (int f = 0; f < buildings_[i].floors; ++f) {
                    if (cbIndex >= kMaxBuildingCBs) break;

                    // 1フロアごとの積み上げY座標を計算（等倍スケール10に対して高さを積み上げる）
                    Vector3 floorPos = buildings_[i].position;
                    floorPos.y = buildings_[i].position.y + (float)f * kFloorHeight + kFloorHeight * 0.5f; // 接地調整を含んだ積み上げY座標

                    Matrix4x4 worldMatrix = MakeAffineMatrix(buildings_[i].scale, buildings_[i].rotate, floorPos);
                    buildingTransformData_[cbIndex]->World = worldMatrix;
                    buildingTransformData_[cbIndex]->WVP = Multiply(worldMatrix, viewProjectionMatrix);
                    cbIndex++;
                }
            }
        }

        // 床(Plane)のワールド・WVP行列を計算（全列・全タイル分を更新）
        {
            // 回転後: X → Z方向（奥行き）, Y → X方向（幅）
            // kRoadDepthScale と kFloorSizeZ を一致させてZファイティングを防止
            const Vector3 roadScale  = { kRoadDepthScale, kRoadWidthScale, 1.0f };
            const Vector3 roadRotate = { 1.57079632f, 1.57079632f, 0.0f };
            for (int i = 0; i < kNumFloors; ++i) {
                Matrix4x4 worldMatrix = MakeAffineMatrix(roadScale, roadRotate, floorPositions_[i]);
                floorTransformData_[i]->World = worldMatrix;
                floorTransformData_[i]->WVP   = Multiply(worldMatrix, viewProjectionMatrix);
            }
        }

        // ── 地面の破片の更新と行列計算 ──
        {
            for (int i = 0; i < kMaxDebris; ++i) {
                if (!debris_[i].isAlive) {
                    // 非表示にするためにWVPのスケールを0にする
                    debrisTransformData_[i]->WVP = MakeIdentity4x4();
                    debrisTransformData_[i]->WVP.m[0][0] = 0.0f;
                    debrisTransformData_[i]->WVP.m[1][1] = 0.0f;
                    debrisTransformData_[i]->WVP.m[2][2] = 0.0f;
                    debrisTransformData_[i]->WVP.m[3][3] = 0.0f;
                    continue;
                }

                debris_[i].currentTime += kDeltaTime;
                if (debris_[i].currentTime >= debris_[i].lifeTime) {
                    debris_[i].isAlive = false;
                    // 同様に非表示化
                    debrisTransformData_[i]->WVP = MakeIdentity4x4();
                    debrisTransformData_[i]->WVP.m[0][0] = 0.0f;
                    debrisTransformData_[i]->WVP.m[1][1] = 0.0f;
                    debrisTransformData_[i]->WVP.m[2][2] = 0.0f;
                    debrisTransformData_[i]->WVP.m[3][3] = 0.0f;
                    continue;
                }

                // 位置の更新
                debris_[i].position.x += debris_[i].velocity.x * kDeltaTime;
                debris_[i].position.y += debris_[i].velocity.y * kDeltaTime;
                debris_[i].position.z += debris_[i].velocity.z * kDeltaTime;

                // 重力適用 (少し強め)
                debris_[i].velocity.y -= 9.8f * 4.0f * kDeltaTime;

                // 回転の更新
                debris_[i].rotate.x += debris_[i].rotationSpeed.x * kDeltaTime;
                debris_[i].rotate.y += debris_[i].rotationSpeed.y * kDeltaTime;
                debris_[i].rotate.z += debris_[i].rotationSpeed.z * kDeltaTime;

                // 接地判定 (地面kFloorYに当たったら反発)
                if (debris_[i].position.y < kFloorY) {
                    debris_[i].position.y = kFloorY;
                    debris_[i].velocity.y = -debris_[i].velocity.y * 0.35f; // 反発減衰
                    debris_[i].velocity.x *= 0.55f; // 摩擦減衰
                    debris_[i].velocity.z *= 0.55f;

                    // 速度が極小になったら物理を止める
                    if (std::abs(debris_[i].velocity.y) < 1.0f) {
                        debris_[i].velocity = { 0.0f, 0.0f, 0.0f };
                        debris_[i].rotationSpeed = { 0.0f, 0.0f, 0.0f };
                    }
                }

                // ワールド行列とWVP行列の計算 (カメラに正対するビルボードを適用し、Z軸での回転も加える)
                Matrix4x4 billboard = camera_->GetBillboardMatrix();
                Matrix4x4 rotateZ = MakeRotateZMatrix(debris_[i].rotate.z); // Z軸回転
                Matrix4x4 world = Multiply(MakeScaleMatrix(debris_[i].scale), rotateZ);
                world = Multiply(world, billboard);
                world.m[3][0] = debris_[i].position.x;
                world.m[3][1] = debris_[i].position.y;
                world.m[3][2] = debris_[i].position.z;

                debrisTransformData_[i]->World = world;
                debrisTransformData_[i]->WVP = Multiply(world, viewProjectionMatrix);
            }
        }
    }

    // 戦闘機モードの場合のジェット噴射エミッター位置の計算
    Vector3 leftJetPos = { 0.0f, 0.0f, 0.0f };
    Vector3 rightJetPos = { 0.0f, 0.0f, 0.0f };
    fighterWorldPos = { 0.0f, 0.0f, 0.0f };
    if (sceneMode_ == SceneMode::kFighter) {
        EulerTransform& camTrans = camera_->GetTransform();
        fighterWorldPos = {
            camTrans.translate.x + fighterModel_->transform.translate.x,
            camTrans.translate.y - 3.0f + fighterModel_->transform.translate.y,
            fighterWorldZ_   // ← 独立Z座標を使用
        };
        // 左右のジェットエンジンノズル（位置を少し上に調整し、機体中心からX方向に±0.8f、後方Z方向に-3.0f）
        leftJetPos  = { fighterWorldPos.x - 0.3f, fighterWorldPos.y + 0.8f, fighterWorldPos.z - 3.0f };
        rightJetPos = { fighterWorldPos.x + 0.8f, fighterWorldPos.y + 0.8f, fighterWorldPos.z - 3.0f };
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
        emitterPos_
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
                for (int i = 0; i < kNumFloors; ++i) {
                    commandList->SetGraphicsRootConstantBufferView(1, floorTransformResources_[i]->GetGPUVirtualAddress());
                    floorModel_->DrawModel(commandList, TextureManager::GetInstance()->GetSrvHandleGPU("douro.jpg"), TextureManager::GetInstance()->GetSrvHandleGPU("test.dds"));
                }
            }

            // 地面の破片(Debris)描画
            if (debrisModel_) {
                if (graphicsPipeline_ && graphicsPipeline_->GetObject3dBlendNormalPipelineState()) {
                    commandList->SetPipelineState(graphicsPipeline_->GetObject3dBlendNormalPipelineState());
                }

                // ライティング・マテリアル乗算色・環境マップを破片描画用に変更 (設定対象は debrisModel_)
                debrisModel_->SetLightingEnabled(false);
                debrisModel_->SetColor({ 1.0f, 1.0f, 1.0f, 1.0f });
                debrisModel_->SetEnvironmentCoefficient(0.0f);

                if (directionalLightResource_) commandList->SetGraphicsRootConstantBufferView(4, directionalLightResource_->GetGPUVirtualAddress());
                if (cameraResource_) commandList->SetGraphicsRootConstantBufferView(5, cameraResource_->GetGPUVirtualAddress());
                for (int i = 0; i < kMaxDebris; ++i) {
                    if (debris_[i].isAlive) {
                        commandList->SetGraphicsRootConstantBufferView(1, debrisTransformResources_[i]->GetGPUVirtualAddress());
                        debrisModel_->DrawModel(commandList, TextureManager::GetInstance()->GetSrvHandleGPU("isihahen.png"), TextureManager::GetInstance()->GetSrvHandleGPU("test.dds"));
                    }
                }

                if (graphicsPipeline_ && graphicsPipeline_->GetObject3dPipelineState()) {
                    commandList->SetPipelineState(graphicsPipeline_->GetObject3dPipelineState());
                }
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

            // ビル(Building)描画（ボス戦中も進行感を出すため描画）
            if (buildingModel_) {
                if (directionalLightResource_) commandList->SetGraphicsRootConstantBufferView(4, directionalLightResource_->GetGPUVirtualAddress());
                if (cameraResource_) commandList->SetGraphicsRootConstantBufferView(5, cameraResource_->GetGPUVirtualAddress());
                int cbIndex = 0;
                for (int i = 0; i < kMaxBuildings; ++i) {
                    for (int f = 0; f < buildings_[i].floors; ++f) {
                        if (cbIndex >= kMaxBuildingCBs) break;
                        commandList->SetGraphicsRootConstantBufferView(1, buildingTransformResources_[cbIndex]->GetGPUVirtualAddress());
                        buildingModel_->DrawModel(commandList, TextureManager::GetInstance()->GetSrvHandleGPU("building/buillding_uv.png"), TextureManager::GetInstance()->GetSrvHandleGPU("test.dds"));
                        cbIndex++;
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

                // ボスがカメラより一定以上後ろ（手前）に通り過ぎていない場合のみ描画
                if (bossWorldZ >= cameraWorldZ - 20.0f) {
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
        
        // エイミング(レティクル)の描画
        if (graphicsPipeline_ && graphicsPipeline_->GetRootSignature()) {
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
        gpuParticleManager_->Emit();
        gpuParticleManager_->UpdateCS();
        gpuParticleManager_->Draw(commandList, textureSrvHandleGPU_);
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
        Vector3 offset = { 0.0f, 0.0f, 0.0f };
        
        switch (group.formation) {
        case FormationType::kVShape:
            // V字 (先頭1体、左右斜め後ろに2体ずつ)
            if (idx == 0)      offset = { 0.0f, 0.0f, 0.0f };
            else if (idx == 1) offset = { -10.0f, 0.0f, -15.0f };
            else if (idx == 2) offset = { 10.0f, 0.0f, -15.0f };
            else if (idx == 3) offset = { -20.0f, 0.0f, -30.0f };
            else if (idx == 4) offset = { 20.0f, 0.0f, -30.0f };
            break;
            
        case FormationType::kCircle: {
            // 円形（丸）: 半径 14m の円周上に 5体を等間隔配置
            float angle = ((float)idx / kEnemiesPerGroup) * 2.0f * (float)M_PI;
            offset.x = std::cos(angle) * 14.0f;
            offset.y = std::sin(angle) * 14.0f;
            offset.z = 0.0f;
            break;
        }
            
        case FormationType::kLineX:
            // 横一列: X軸上に等間隔並び
            offset.x = ((float)idx - (kEnemiesPerGroup - 1) * 0.5f) * 12.0f; // -24m ~ +24m
            offset.y = 0.0f;
            offset.z = 0.0f;
            break;
            
        case FormationType::kSlant:
            // 斜め一列
            offset.x = ((float)idx - (kEnemiesPerGroup - 1) * 0.5f) * 12.0f;
            offset.y = ((float)idx - (kEnemiesPerGroup - 1) * 0.5f) * 6.0f;
            offset.z = ((float)idx - (kEnemiesPerGroup - 1) * 0.5f) * -10.0f;
            break;
        }
        
        enemy.localOffset = offset;
        
        // 本来並ぶべきフォーメーション上のワールド座標を記録
        enemy.wanderAnchor.x = group.centerX + offset.x;
        enemy.wanderAnchor.y = group.centerY + offset.y;
        enemy.wanderAnchor.z = group.centerZ + offset.z;
        
        // 初期状態は横側待機 (kSideWait)
        enemy.state = Enemy::State::kSideWait;
        enemy.stateTimer = 0.0f;
        enemy.rotate = { 0.0f, 0.0f, 0.0f };
        enemy.relativeZ = 220.0f;
        
        // 「ビルに隠れた状態から真ん中に現れる」を表現するため、初期X座標をビルの座標（X = -45m または +45m）に設定
        // 小隊メンバーごとに左右のビルへ散らす
        float spawnX = (idx % 2 == 0) ? -45.0f : 45.0f;
        enemy.position.x = spawnX;
        enemy.position.y = enemy.wanderAnchor.y;
        enemy.position.z = enemy.wanderAnchor.z;
    }
}

void GamePlayScene::RespawnEnemyGroup(int groupIndex, float playerZ) {
    EnemyGroup& group = enemyGroups_[groupIndex];
    
    // 新しいフォーメーション形状をランダム決定
    std::uniform_int_distribution<int> distForm(0, (int)FormationType::kCount - 1);
    group.formation = (FormationType)distForm(randomEngine_);
    
    // 中心位置を決定 (グループごとにZ出現位置の範囲を50mずつずらして波状にする)
    std::uniform_real_distribution<float> distX(-10.0f, 10.0f);
    std::uniform_real_distribution<float> distY(5.0f, 20.0f);
    
    float minZ = 160.0f + (float)groupIndex * 50.0f;
    float maxZ = 220.0f + (float)groupIndex * 50.0f;
    std::uniform_real_distribution<float> distZ(minZ, maxZ);
    
    group.centerX = distX(randomEngine_);
    group.centerY = distY(randomEngine_);
    group.centerZ = playerZ + distZ(randomEngine_);
    
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

void GamePlayScene::SpawnDebris(const Vector3& basePos) {
    for (int i = 0; i < kMaxDebris; ++i) {
        debris_[i].isAlive = true;
        // 初期位置
        debris_[i].position = basePos;
        debris_[i].position.x += ((float)(randomEngine_() % 200) / 100.0f - 1.0f) * 4.0f;
        debris_[i].position.y += ((float)(randomEngine_() % 100) / 100.0f) * 1.5f;
        debris_[i].position.z += ((float)(randomEngine_() % 200) / 100.0f - 1.0f) * 4.0f;

        // 飛び散る初速 (360度ランダムに広がる)
        float angle = ((float)(randomEngine_() % 360) * 3.14159265f) / 180.0f;
        float speed = 15.0f + (float)(randomEngine_() % 35);
        debris_[i].velocity.x = std::cos(angle) * speed * 0.7f;
        debris_[i].velocity.y = 25.0f + (float)(randomEngine_() % 30); // 上への強い初速
        debris_[i].velocity.z = std::sin(angle) * speed * 0.7f + 5.0f; // 前方へも少し流れる

        // 初期回転と回転角速度 (3軸ランダム)
        debris_[i].rotate.x = ((float)(randomEngine_() % 360) * 3.14159265f) / 180.0f;
        debris_[i].rotate.y = ((float)(randomEngine_() % 360) * 3.14159265f) / 180.0f;
        debris_[i].rotate.z = ((float)(randomEngine_() % 360) * 3.14159265f) / 180.0f;

        debris_[i].rotationSpeed.x = ((float)(randomEngine_() % 200) / 100.0f - 1.0f) * 10.0f;
        debris_[i].rotationSpeed.y = ((float)(randomEngine_() % 200) / 100.0f - 1.0f) * 10.0f;
        debris_[i].rotationSpeed.z = ((float)(randomEngine_() % 200) / 100.0f - 1.0f) * 10.0f;

        // スケール (0.4〜1.6のランダム)
        float sz = 0.4f + ((float)(randomEngine_() % 100) / 100.0f) * 1.2f;
        debris_[i].scale = { sz, sz, sz };

        debris_[i].lifeTime = 1.2f + ((float)(randomEngine_() % 100) / 100.0f) * 1.8f; // 1.2〜3.0秒
        debris_[i].currentTime = 0.0f;
    }
}
