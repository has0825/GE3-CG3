#include "GamePlayScene.h"
#include "WinApp.h"
#include "SceneManager.h"
#include "D3D12Util.h"
#include "MathUtil.h"
#include "DataTypes.h"
#include "TextureManager.h"
#include "SrvManager.h"
#include <algorithm>

#ifdef USE_IMGUI
#include "externals/imgui/imgui.h"
#include "externals/imgui/imgui_impl_dx12.h"
#endif

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

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
    dxCommon_ = DirectXCommon::GetInstance();
    input_ = Input::GetInstance();
    ID3D12Device* device = dxCommon_->GetDevice();
    ID3D12GraphicsCommandList* commandList = dxCommon_->GetCommandList();

    audio_ = std::make_unique<Audio>();


    audio_->Initialize();

    graphicsPipeline_ = GraphicsPipeline::GetInstance();
    graphicsPipeline_->Initialize(device);

    srvDescriptorHeap_ = SrvManager::GetInstance()->GetDescriptorHeap();
    descriptorSizeSRV_ = SrvManager::GetInstance()->GetDescriptorSize();

    std::random_device seedGenerator;
    randomEngine_.seed(seedGenerator());

    camera_ = std::make_unique<Camera>(WinApp::kClientWidth, WinApp::kClientHeight);
    camera_->SetTranslate({ 0.0f, 0.0f, -15.0f });

    particleModel_ = std::unique_ptr<Model>(Model::CreateParticleModel(device));
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
    TextureManager::GetInstance()->LoadTexture("human/white.png");
    TextureManager::GetInstance()->LoadTexture("aiming.png");
    TextureManager::GetInstance()->LoadTexture("Player2/Player_basecolor.JPEG");
    TextureManager::GetInstance()->LoadTexture("Player/player.png");
    TextureManager::GetInstance()->LoadTexture("cobblestone_street_night_2k.dds");
    TextureManager::GetInstance()->LoadTexture("rostock_laage_airport_4k.dds");

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
        enemy.scale = { 2.5f, 2.5f, 2.5f };
        enemy.rotate = { 0.0f, 0.0f, 0.0f }; // モデルの向きを180度反転して修正
        enemy.isAlive = true;
        enemy.radius = 3.5f;
        enemies_.push_back(enemy);
    }

    // 初期フォーメーション位置の適用
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
        bool isLeft = (i % 2 == 0);
        building.position.x = isLeft ? -45.0f : 45.0f;
        
        // 積み重ねる階数を 1〜5 階でランダム設定
        building.floors = 1 + (randomEngine_() % 5); 
        building.scale = { 10.0f, 10.0f, 10.0f }; // ビル1階分の等倍スケール
        building.rotate = { 0.0f, 0.0f, 0.0f };
        
        int pairIndex = i / 2;
        building.position.z = 50.0f + (float)pairIndex * kBuildingInterval;
        // 積み重ねるため、個々のY座標は Update 内で計算されるため、基準値のみ設定
        building.position.y = -20.0f; 

        buildings_.push_back(building);
    }

    // ── 床(Plane)の初期化とバッファ生成 ──
    floorModel_ = std::unique_ptr<Model>(Model::LoadGLTF("Resources/plane.obj", device));
    TextureManager::GetInstance()->LoadTexture("gradationLine.png");

    float kFloorSizeZ = 200.0f;
    for (int i = 0; i < kNumFloors; ++i) {
        floorTransformResources_[i] = CreateBufferResource(device, sizeof(TransformationMatrix));
        floorTransformResources_[i]->Map(0, nullptr, reinterpret_cast<void**>(&floorTransformData_[i]));
        floorTransformData_[i]->WVP = MakeIdentity4x4();
        floorTransformData_[i]->World = MakeIdentity4x4();

        floorPositions_[i] = { 0.0f, -20.0f, (float)i * kFloorSizeZ };
    }

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
}

void GamePlayScene::Finalize() {
    if (audio_) {
        audio_->Finalize();
    }
}

void GamePlayScene::Update() {
    float kDeltaTime = 1.0f / 60.0f;

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
        }
        dissolveParamData_->threshold = transitionThreshold_;
    }

#ifdef USE_IMGUI
    ImGui::SetNextWindowSize(ImVec2(500, 200));
    ImGui::Begin("GamePlay Control");
    ImGui::Checkbox("Show SimpleSkin", &showSimpleSkin_);
    ImGui::Checkbox("Show AnimatedCube", &showAnimatedCube_);
    ImGui::Checkbox("Show Particles", &showParticles_);
    ImGui::Checkbox("Show Skybox", &showSkybox_);
    if (showSkybox_) {
        const char* skyboxItems[] = { "Cobblestone Street (Night)", "Rostock Airport (Day)" };
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
    
    ImGui::Text("TAB Key: Switch Scene Mode");
    ImGui::Text("Current Mode: %s", (sceneMode_ == SceneMode::kMouse ? "Mouse" : (sceneMode_ == SceneMode::kCamera ? "Camera" : "Fighter")));
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

    if (sceneMode_ == SceneMode::kCamera) {
        // カメラ操作モード: マウス移動で回転（カーソルロック）
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
        
        Vector3 moveDir = { 0.0f, 0.0f, 0.0f };
        if (input_->IsKeyPressed(DIK_W)) moveDir.z += 1.0f;
        if (input_->IsKeyPressed(DIK_S)) moveDir.z -= 1.0f;
        if (input_->IsKeyPressed(DIK_D)) moveDir.x += 1.0f;
        if (input_->IsKeyPressed(DIK_A)) moveDir.x -= 1.0f;
        if (input_->IsKeyPressed(DIK_E)) moveDir.y += 1.0f;
        if (input_->IsKeyPressed(DIK_Q)) moveDir.y -= 1.0f;
        
        if (moveDir.x != 0.0f || moveDir.y != 0.0f || moveDir.z != 0.0f) {
            float cameraSpeed = 5.0f;
            Matrix4x4 cameraRotY = MakeRotateYMatrix(camTrans.rotate.y);
            Vector3 rotatedMoveDir = TransformNormal(moveDir, cameraRotY);
            rotatedMoveDir = Normalize(rotatedMoveDir);
            rotatedMoveDir = Scale(rotatedMoveDir, cameraSpeed * kDeltaTime);
            camTrans.translate = Add(camTrans.translate, rotatedMoveDir);
        }
    } else if (sceneMode_ == SceneMode::kFighter) {
        // --- 戦闘機（レールシューター）モード ---

        // ── ブースト処理（LSHIFTで発動、バレルロール終了と同時に自動で戻る） ────────────────
        if (input_->IsKeyTriggered(DIK_LSHIFT)) {
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
        fighterWorldZ_ += boostForwardSpeed_ * kDeltaTime;

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
            if (input_->IsKeyPressed(DIK_W)) inputDir.y += 1.0f;
            if (input_->IsKeyPressed(DIK_S)) inputDir.y -= 1.0f;
            if (input_->IsKeyPressed(DIK_A)) inputDir.x -= 1.0f;
            if (input_->IsKeyPressed(DIK_D)) inputDir.x += 1.0f;

            if (inputDir.x != 0 || inputDir.y != 0) {
                inputDir = Normalize(inputDir);
                fighterModel_->transform.translate.x += inputDir.x * 25.0f * kDeltaTime;
                fighterModel_->transform.translate.y += inputDir.y * 20.0f * kDeltaTime;
            }

            fighterModel_->transform.translate.x = std::clamp(fighterModel_->transform.translate.x, -35.0f, 35.0f);
            fighterModel_->transform.translate.y = std::clamp(fighterModel_->transform.translate.y, -25.0f, 25.0f);

            // スターフォックス風のカメラX/Y追従
            float cameraLag = 0.08f;
            float targetCamX = fighterModel_->transform.translate.x * 0.5f;
            float targetCamY = fighterModel_->transform.translate.y * 0.5f;
            camTrans.translate.x = std::lerp(camTrans.translate.x, targetCamX, cameraLag);
            camTrans.translate.y = std::lerp(camTrans.translate.y, targetCamY, cameraLag);

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
                barrelRollTimer_ += kDeltaTime;
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

            if (input_->IsKeyTriggered(DIK_SPACE)) {
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
        }
    }

    
    // 弾の更新（全モード共通）と敵との衝突判定
    for (int i = 0; i < kMaxBullets; ++i) {
        if (playerBullets_[i].currentTime < playerBullets_[i].lifeTime) {
            playerBullets_[i].position = Add(playerBullets_[i].position, Scale(playerBullets_[i].velocity, kDeltaTime));
            playerBullets_[i].currentTime += kDeltaTime;

            // 敵との当たり判定 (球衝突判定)
            for (auto& enemy : enemies_) {
                if (!enemy.isAlive) continue;

                Vector3 diff = Subtract(playerBullets_[i].position, enemy.position);
                float dist = Length(diff);
                // 弾の当たり判定半径を広げて（0.5f -> 2.5f）、すり抜けや近距離で当たらない現象を緩和
                if (dist <= (enemy.radius + 2.5f)) {
                    // 弾を消去
                    playerBullets_[i].currentTime = playerBullets_[i].lifeTime;

                    // 敵を撃破
                    enemy.isAlive = false;
                    Vector3 deathPos = enemy.position; // 爆発エフェクト発生位置を記録

                    // 所属小隊が全滅したか判定
                    int g = enemy.groupIndex;
                    bool anyAlive = false;
                    for (int idx = 0; idx < kEnemiesPerGroup; ++idx) {
                        if (enemies_[g * kEnemiesPerGroup + idx].isAlive) {
                            anyAlive = true;
                            break;
                        }
                    }
                    // 全滅していたら、プレイヤーの前方に新しい陣形で小隊ごとリポップ！
                    if (!anyAlive) {
                        RespawnEnemyGroup(g, fighterWorldZ_);
                    }

                    // ★★★ 超ド派手爆破エフェクト！ ★★★
                    particleManager_->EmitHit(deathPos);
                    particleManager_->EmitRing(deathPos);
                    particleManager_->EmitCylinder(deathPos);

                    // 爆破音を再生
                    audio_->PlayWave(jumpSE_, false, 1.5f);
                    break;
                }
            }
        }
    }
    camera_->Update();
    Matrix4x4 viewProjectionMatrix = camera_->GetViewProjectionMatrix();

    // 敵キャラのワールド行列・WVPの更新
    EulerTransform& camTransForEnemy = camera_->GetTransform();

    // 各グループの画面外判定とリポップチェック
    for (int g = 0; g < kNumGroups; ++g) {
        // グループの中心Zがカメラの後方50mより後ろになったら、画面外とみなして小隊ごと前方へ再配置
        if (enemyGroups_[g].centerZ < camTransForEnemy.translate.z - 50.0f) {
            RespawnEnemyGroup(g, fighterWorldZ_);
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

    // 戦闘機とエイミングのWVPの更新
    if (sceneMode_ == SceneMode::kFighter) {
        if (fighterModel_ && fighterTransformData_) {
            fighterTransformData_->WVP = Multiply(fighterTransformData_->World, viewProjectionMatrix);
        }
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
    if (sceneMode_ == SceneMode::kFighter) {
        float cameraZ = camera_->GetTransform().translate.z;
        float kBuildingInterval = 80.0f;
        float kFloorHeight = 10.0f; // 1階あたりのY軸高さの差分（ビルモデルの大きさに合わせて調整）

        // ビルの画面外再配置
        for (int i = 0; i < kMaxBuildings; ++i) {
            // カメラの後方80mを超えたら、遥か前方（最前方のビルペアの先）に再配置
            if (buildings_[i].position.z < cameraZ - 80.0f) {
                buildings_[i].position.z += kMaxBuildings * kBuildingInterval * 0.5f; // 16棟 / 2 = 8ペア分前方に送る (640m)
                
                // 再配置した際にビルの階数(1〜5階)を再抽選する
                buildings_[i].floors = 1 + (randomEngine_() % 5);
            }
        }

        // 床の画面外再配置
        float kFloorSizeZ = 200.0f;
        for (int i = 0; i < kNumFloors; ++i) {
            // カメラの後方200mを超えたら、最前方へ移動
            if (floorPositions_[i].z < cameraZ - 200.0f) {
                // 最も前方にある床のZ座標を見つける
                float maxZ = -9999.0f;
                for (int j = 0; j < kNumFloors; ++j) {
                    if (floorPositions_[j].z > maxZ) {
                        maxZ = floorPositions_[j].z;
                    }
                }
                floorPositions_[i].z = maxZ + kFloorSizeZ;
            }
        }

        // 各ビルの各階数（フロア）ごとにワールド行列とWVP行列を計算
        int cbIndex = 0;
        for (int i = 0; i < kMaxBuildings; ++i) {
            for (int f = 0; f < buildings_[i].floors; ++f) {
                if (cbIndex >= kMaxBuildingCBs) break;

                // 1フロアごとの積み上げY座標を計算（等倍スケール10に対して高さを積み上げる）
                Vector3 floorPos = buildings_[i].position;
                floorPos.y = -20.0f + (float)f * kFloorHeight + kFloorHeight * 0.5f; // 接地調整を含んだ積み上げY座標

                Matrix4x4 worldMatrix = MakeAffineMatrix(buildings_[i].scale, buildings_[i].rotate, floorPos);
                buildingTransformData_[cbIndex]->World = worldMatrix;
                buildingTransformData_[cbIndex]->WVP = Multiply(worldMatrix, viewProjectionMatrix);
                cbIndex++;
            }
        }

        // 床(Plane)のワールド・WVP行列を計算
        for (int i = 0; i < kNumFloors; ++i) {
            // plane.obj のサイズに合わせてXとZを200倍スケールにして広大な床にする
            Matrix4x4 worldMatrix = MakeAffineMatrix(Vector3{ 200.0f, 1.0f, 200.0f }, Vector3{ 0.0f, 0.0f, 0.0f }, floorPositions_[i]);
            floorTransformData_[i]->World = worldMatrix;
            floorTransformData_[i]->WVP = Multiply(worldMatrix, viewProjectionMatrix);
        }
    }

    // 戦闘機モードの場合のジェット噴射エミッター位置の計算
    Vector3 leftJetPos = { 0.0f, 0.0f, 0.0f };
    Vector3 rightJetPos = { 0.0f, 0.0f, 0.0f };
    Vector3 fighterWorldPos = { 0.0f, 0.0f, 0.0f };
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
        (sceneMode_ == SceneMode::kFighter),
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

            std::string skyboxTexName = (skyboxType_ == 0) ? "cobblestone_street_night_2k.dds" : "rostock_laage_airport_4k.dds";
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

    // --- 戦闘機モードの描画 ---
    if (sceneMode_ == SceneMode::kFighter) {
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
                    floorModel_->DrawModel(commandList, TextureManager::GetInstance()->GetSrvHandleGPU("gradationLine.png"), TextureManager::GetInstance()->GetSrvHandleGPU("test.dds"));
                }
            }

            // ビル(Building)描画
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

            // 敵の描画（プレイヤーと同じ戦闘機モデルを使用）
            if (enemyModel_ && showEnemies_) {
                for (int i = 0; i < kMaxEnemies; ++i) {
                    if (enemies_[i].isAlive) {
                        commandList->SetGraphicsRootConstantBufferView(1, enemyTransformResources_[i]->GetGPUVirtualAddress());
                        enemyModel_->DrawModel(commandList, TextureManager::GetInstance()->GetSrvHandleGPU("Player/player.png"), TextureManager::GetInstance()->GetSrvHandleGPU("test.dds"));
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
        enemy.position.x = group.centerX + offset.x;
        enemy.position.y = group.centerY + offset.y;
        enemy.position.z = group.centerZ + offset.z;
    }
}

void GamePlayScene::RespawnEnemyGroup(int groupIndex, float playerZ) {
    EnemyGroup& group = enemyGroups_[groupIndex];
    
    // 新しいフォーメーション形状をランダム決定
    std::uniform_int_distribution<int> distForm(0, (int)FormationType::kCount - 1);
    group.formation = (FormationType)distForm(randomEngine_);
    
    // 中心位置を決定 (プレイヤーの前方 450m〜750m の位置)
    std::uniform_real_distribution<float> distX(-10.0f, 10.0f);
    std::uniform_real_distribution<float> distY(-5.0f, 10.0f);
    std::uniform_real_distribution<float> distZ(450.0f, 750.0f);
    
    group.centerX = distX(randomEngine_);
    group.centerY = distY(randomEngine_);
    group.centerZ = playerZ + distZ(randomEngine_);
    
    // 小隊メンバー全員を生存状態（Alive）にして再配置
    for (int idx = 0; idx < kEnemiesPerGroup; ++idx) {
        int enemyIdx = groupIndex * kEnemiesPerGroup + idx;
        enemies_[enemyIdx].isAlive = true;
    }
    
    ApplyGroupFormation(groupIndex);
}
