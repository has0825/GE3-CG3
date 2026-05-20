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

    instancingResource_ = CreateBufferResource(device, sizeof(ParticleForGPU) * kNumInstances);
    instancingResource_->Map(0, nullptr, reinterpret_cast<void**>(&instancingData_));

    spriteInstancingResource_ = CreateBufferResource(device, sizeof(ParticleForGPU) * kSpriteInstanceCount);
    spriteInstancingResource_->Map(0, nullptr, reinterpret_cast<void**>(&spriteInstancingData_));

    ringInstancingResource_ = CreateBufferResource(device, sizeof(ParticleForGPU) * kRingInstanceCount);
    ringInstancingResource_->Map(0, nullptr, reinterpret_cast<void**>(&ringInstancingData_));

    ringParticles_.resize(kRingInstanceCount);
    for (UINT i = 0; i < kRingInstanceCount; ++i) {
        ringParticles_[i] = MakeNewParticle(kTypeRing, {0.0f, 0.0f, 0.0f});
        ringParticles_[i].currentTime = 999.0f;
    }

    cylinderInstancingResource_ = CreateBufferResource(device, sizeof(ParticleForGPU) * kCylinderInstanceCount);
    cylinderInstancingResource_->Map(0, nullptr, reinterpret_cast<void**>(&cylinderInstancingData_));

    cylinderParticles_.resize(kCylinderInstanceCount);
    for (UINT i = 0; i < kCylinderInstanceCount; ++i) {
        cylinderParticles_[i] = MakeNewParticle(kTypeCylinder, {0.0f, 0.0f, 0.0f});
        cylinderParticles_[i].currentTime = 999.0f;
    }

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

    particles_.resize(kNumInstances);
    for (UINT i = 0; i < kNumInstances; ++i) {
        particles_[i] = MakeNewParticle(currentEffect_, emitterPos_);
        std::uniform_real_distribution<float> distTime(0.0f, 3.0f);
        particles_[i].currentTime = distTime(randomEngine_);
    }

    for (UINT i = 0; i < kNumInstances; ++i) {
        instancingData_[i].WVP = MakeIdentity4x4();
        instancingData_[i].World = MakeIdentity4x4();
        instancingData_[i].color = particles_[i].color;
        instancingData_[i].uvTransform = MakeIdentity4x4();
    }

    DirectX::ScratchImage mipImages = LoadTexture("resources/circle2.png");
    const DirectX::TexMetadata& metadata = mipImages.GetMetadata();
    textureResource_ = CreateTextureResource(device, metadata);
    intermediateResource_ = UploadTextureData(textureResource_.Get(), mipImages, device, commandList);

    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
    srvDesc.Format = metadata.format;
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MipLevels = UINT(metadata.mipLevels);

    uint32_t textureSrvIndex = SrvManager::GetInstance()->Allocate();
    D3D12_CPU_DESCRIPTOR_HANDLE textureSrvHandleCPU = SrvManager::GetInstance()->GetCPUDescriptorHandle(textureSrvIndex);
    textureSrvHandleGPU_ = SrvManager::GetInstance()->GetGPUDescriptorHandle(textureSrvIndex);
    device->CreateShaderResourceView(textureResource_.Get(), &srvDesc, textureSrvHandleCPU);

    D3D12_SHADER_RESOURCE_VIEW_DESC instancingSrvDesc{};
    instancingSrvDesc.Format = DXGI_FORMAT_UNKNOWN;
    instancingSrvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    instancingSrvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
    instancingSrvDesc.Buffer.FirstElement = 0;
    instancingSrvDesc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;
    instancingSrvDesc.Buffer.NumElements = kNumInstances;
    instancingSrvDesc.Buffer.StructureByteStride = sizeof(ParticleForGPU);

    uint32_t instancingSrvIndex = SrvManager::GetInstance()->Allocate();
    D3D12_CPU_DESCRIPTOR_HANDLE instancingSrvHandleCPU = SrvManager::GetInstance()->GetCPUDescriptorHandle(instancingSrvIndex);
    instancingSrvHandleGPU_ = SrvManager::GetInstance()->GetGPUDescriptorHandle(instancingSrvIndex);
    device->CreateShaderResourceView(instancingResource_.Get(), &instancingSrvDesc, instancingSrvHandleCPU);

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

    D3D12_SHADER_RESOURCE_VIEW_DESC ringInstancingSrvDesc{};
    ringInstancingSrvDesc.Format = DXGI_FORMAT_UNKNOWN;
    ringInstancingSrvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    ringInstancingSrvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
    ringInstancingSrvDesc.Buffer.FirstElement = 0;
    ringInstancingSrvDesc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;
    ringInstancingSrvDesc.Buffer.NumElements = kRingInstanceCount;
    ringInstancingSrvDesc.Buffer.StructureByteStride = sizeof(ParticleForGPU);

    uint32_t ringInstancingSrvIndex = SrvManager::GetInstance()->Allocate();
    D3D12_CPU_DESCRIPTOR_HANDLE ringInstancingSrvHandleCPU = SrvManager::GetInstance()->GetCPUDescriptorHandle(ringInstancingSrvIndex);
    ringInstancingSrvHandleGPU_ = SrvManager::GetInstance()->GetGPUDescriptorHandle(ringInstancingSrvIndex);
    device->CreateShaderResourceView(ringInstancingResource_.Get(), &ringInstancingSrvDesc, ringInstancingSrvHandleCPU);

    D3D12_SHADER_RESOURCE_VIEW_DESC cylinderInstancingSrvDesc{};
    cylinderInstancingSrvDesc.Format = DXGI_FORMAT_UNKNOWN;
    cylinderInstancingSrvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    cylinderInstancingSrvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
    cylinderInstancingSrvDesc.Buffer.FirstElement = 0;
    cylinderInstancingSrvDesc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;
    cylinderInstancingSrvDesc.Buffer.NumElements = kCylinderInstanceCount;
    cylinderInstancingSrvDesc.Buffer.StructureByteStride = sizeof(ParticleForGPU);

    uint32_t cylinderInstancingSrvIndex = SrvManager::GetInstance()->Allocate();
    D3D12_CPU_DESCRIPTOR_HANDLE cylinderInstancingSrvHandleCPU = SrvManager::GetInstance()->GetCPUDescriptorHandle(cylinderInstancingSrvIndex);
    cylinderInstancingSrvHandleGPU_ = SrvManager::GetInstance()->GetGPUDescriptorHandle(cylinderInstancingSrvIndex);
    device->CreateShaderResourceView(cylinderInstancingResource_.Get(), &cylinderInstancingSrvDesc, cylinderInstancingSrvHandleCPU);

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

    commandList->Close();
    ID3D12CommandQueue* commandQueue = dxCommon_->GetCommandQueue();
    ID3D12CommandList* ppCommandLists[] = { commandList };
    commandQueue->ExecuteCommandLists(1, ppCommandLists);

    Microsoft::WRL::ComPtr<ID3D12Fence> fence;
    device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence));
    HANDLE fenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
    if (fenceEvent != nullptr) {
        commandQueue->Signal(fence.Get(), 1);
        if (fence->GetCompletedValue() < 1) {
            fence->SetEventOnCompletion(1, fenceEvent);
            WaitForSingleObject(fenceEvent, INFINITE);
        }
        CloseHandle(fenceEvent);
    }

    dxCommon_->GetCommandAllocator()->Reset();
    commandList->Reset(dxCommon_->GetCommandAllocator(), nullptr);

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

    skybox_ = std::make_unique<Skybox>();
    skybox_->Initialize(device);

    playerModel_ = Model::LoadGLTF("Resources/human/walk.gltf", device);
    fighterModel_ = Model::LoadGLTF("Resources/Player2/Player.obj", device);
    if (fighterModel_) {
        fighterModel_->transform.scale = { 10.0f, 10.0f, 10.0f };
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

    // 敵用リソース生成（ウェーブ式出現：最初は5体だけ出現）

    enemies_.clear();
    for (int i = 0; i < kMaxEnemies; ++i) {
        enemyTransformResources_[i] = CreateBufferResource(device, sizeof(TransformationMatrix));
        enemyTransformResources_[i]->Map(0, nullptr, reinterpret_cast<void**>(&enemyTransformData_[i]));
        enemyTransformData_[i]->WVP = MakeIdentity4x4();
        enemyTransformData_[i]->World = MakeIdentity4x4();

        Enemy enemy;
        // Z方向に150刻みで配置 (Z = 150 から 2250 まで)
        enemy.position = {
            (float)((i % 3) - 1) * 20.0f,
            (float)(((i + 1) % 2) - 0.5f) * 10.0f,
            150.0f + (float)i * 150.0f
        };
        enemy.scale = { 2.5f, 2.5f, 2.5f };
        enemy.rotate = { 0.0f, 0.0f, 0.0f }; // モデルの向きを180度反転して修正
        // 最初のkInitialEnemies体だけをアクティブにする
        enemy.isAlive = (i < kInitialEnemies);
        enemy.radius = 3.5f;
        enemies_.push_back(enemy);
    }
    nextEnemyIndex_ = kInitialEnemies;

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

    gpuParticleManager_->SetTranslate({ -10.0f, 0.0f, 0.0f });
}

void GamePlayScene::Finalize() {
    if (audio_) {
        audio_->Finalize();
    }
}

void GamePlayScene::Update() {
#ifdef USE_IMGUI
    ImGui::SetNextWindowSize(ImVec2(500, 200));
    ImGui::Begin("GamePlay Control");
    ImGui::Checkbox("Show SimpleSkin", &showSimpleSkin_);
    ImGui::Checkbox("Show AnimatedCube", &showAnimatedCube_);
    ImGui::Checkbox("Show Particles", &showParticles_);
    ImGui::Checkbox("Show Skybox", &showSkybox_);
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
        int hitCount = 8;
        for (uint32_t i = 0; i < kNumInstances && hitCount > 0; ++i) {
            // 死んでいるパーティクルを再利用
            if (particles_[i].currentTime >= particles_[i].lifeTime) {
                particles_[i] = MakeNewParticle(kTypeHit, emitterPos_);
                hitCount--;
            }
        }
        
        int ringCount = 1;
        for (uint32_t i = 0; i < kRingInstanceCount && ringCount > 0; ++i) {
            if (ringParticles_[i].currentTime >= ringParticles_[i].lifeTime) {
                ringParticles_[i] = MakeNewParticle(kTypeRing, emitterPos_);
                ringCount--;
            }
        }
    }

    if (input_->IsKeyTriggered(DIK_6)) {
        int cylinderCount = 1;
        for (uint32_t i = 0; i < kCylinderInstanceCount && cylinderCount > 0; ++i) {
            if (cylinderParticles_[i].currentTime >= cylinderParticles_[i].lifeTime) {
                cylinderParticles_[i] = MakeNewParticle(kTypeCylinder, emitterPos_);
                cylinderCount--;
            }
        }
    }

    if (input_->IsKeyTriggered(DIK_G)) useGravity_ = !useGravity_;

    if (input_->IsKeyTriggered(DIK_SPACE)) {
        audio_->PlayWave(jumpSE_, false, 1.0f);
    }

    const float kDeltaTime = 1.0f / 60.0f;
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
        // 1. カメラは常に前進
        float forwardSpeed = 30.0f;
        camTrans.translate.z += forwardSpeed * kDeltaTime;
        
        // 2. 自機のローカル移動（カメラからの相対位置）
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
            
            // 画面外に出ないように制限 (距離が遠くなったため範囲を広げて調整)
            fighterModel_->transform.translate.x = std::clamp(fighterModel_->transform.translate.x, -35.0f, 35.0f);
            fighterModel_->transform.translate.y = std::clamp(fighterModel_->transform.translate.y, -25.0f, 25.0f);
            
            // スターフォックス風の緩やかなカメラ追従 (X, Y 補間)
            float cameraLag = 0.08f;
            float targetCamX = fighterModel_->transform.translate.x * 0.5f;
            float targetCamY = fighterModel_->transform.translate.y * 0.5f;
            camTrans.translate.x = std::lerp(camTrans.translate.x, targetCamX, cameraLag);
            camTrans.translate.y = std::lerp(camTrans.translate.y, targetCamY, cameraLag);

            // ワールド座標の計算 (Star Fox風にカメラから離す)
            Vector3 fighterWorldPos = {
                camTrans.translate.x + fighterModel_->transform.translate.x,
                camTrans.translate.y - 3.0f + fighterModel_->transform.translate.y,
                camTrans.translate.z + 65.0f
            };
            
            // 機体の傾き（ロール、ピッチ）
            float targetRoll = inputDir.x * -0.6f;
            float targetPitch = inputDir.y * 0.4f;
            playerRotationRoll_ = std::lerp(playerRotationRoll_, targetRoll, 0.1f);
            playerRotationPitch_ = std::lerp(playerRotationPitch_, targetPitch, 0.1f);
            
            fighterModel_->transform.rotate.z = playerRotationRoll_;
            fighterModel_->transform.rotate.x = playerRotationPitch_;
            
            fighterTransformData_->World = MakeAffineMatrix(fighterModel_->transform.scale, fighterModel_->transform.rotate, fighterWorldPos);
            
            // --- 弾の発射と更新 ---
            Vector3 defaultReticlePos = { fighterWorldPos.x, fighterWorldPos.y, fighterWorldPos.z + 120.0f }; // デフォルトは遠くにレティクル
            
            // ★ エイムアシスト機能（徐々に吸い付くlerpバージョン）★
            float bestDist2D = 30.0f; // 吸い付く判定の緩さ（これよりXY平面の距離が近ければロックオン）
            Enemy* lockedEnemy = nullptr;
            for (auto& enemy : enemies_) {
                if (!enemy.isAlive) continue;
                // 自機より奥にいる敵だけを狙う
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
            // lerpで徐々に目標位置に近づける（0.05f = 約3秒で完全ロックオン）
            float aimLerpSpeed = 0.05f;
            aimReticlePos_.x = std::lerp(aimReticlePos_.x, targetReticlePos.x, aimLerpSpeed);
            aimReticlePos_.y = std::lerp(aimReticlePos_.y, targetReticlePos.y, aimLerpSpeed);
            aimReticlePos_.z = std::lerp(aimReticlePos_.z, targetReticlePos.z, aimLerpSpeed);
            Vector3 reticlePos = aimReticlePos_;

            if (input_->IsKeyTriggered(DIK_SPACE)) {
                // 左翼と右翼から発射 (高さを少し上に調整して拡大モデルの翼にピッタリ合わせる)
                // 近すぎると弾が当たらない対策として、発射位置を少し内側に寄せ、Z座標を自機本体と同じ位置からにする
                Vector3 leftWing = { fighterWorldPos.x - 2.5f, fighterWorldPos.y + 0.8f, fighterWorldPos.z };
                Vector3 rightWing = { fighterWorldPos.x + 2.5f, fighterWorldPos.y + 0.8f, fighterWorldPos.z };
                
                Vector3 dirLeft = { reticlePos.x - leftWing.x, reticlePos.y - leftWing.y, reticlePos.z - leftWing.z };
                dirLeft = Normalize(dirLeft);
                Vector3 dirRight = { reticlePos.x - rightWing.x, reticlePos.y - rightWing.y, reticlePos.z - rightWing.z };
                dirRight = Normalize(dirRight);
                float bulletSpeed = 150.0f; // 前進速度が上がったため弾速も少しアップ！
                
                for (auto& b : playerBullets_) {
                    if (b.currentTime >= b.lifeTime) {
                        b.position = leftWing;
                        b.velocity = Scale(dirLeft, bulletSpeed);
                        b.lifeTime = 2.0f;
                        b.currentTime = 0.0f;
                        break;
                    }
                }
                for (auto& b : playerBullets_) {
                    if (b.currentTime >= b.lifeTime) {
                        b.position = rightWing;
                        b.velocity = Scale(dirRight, bulletSpeed);
                        b.lifeTime = 2.0f;
                        b.currentTime = 0.0f;
                        break;
                    }
                }
                audio_->PlayWave(jumpSE_, false, 1.0f);
            }
            
            // エイミング(レティクル)の更新 (距離があるため大きめに描画)
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

                    // ★ 次の敵を1体出現させる（ウェーブ式） ★
                    if (nextEnemyIndex_ < kMaxEnemies) {
                        enemies_[nextEnemyIndex_].isAlive = true;
                        nextEnemyIndex_++;
                    }

                    // ★★★ 超ド派手爆破エフェクト！ ★★★
                    // 1. 火花スパーク (kTypeHit) 40発
                    int sparkCount = 40;
                    for (uint32_t p = 0; p < kNumInstances && sparkCount > 0; ++p) {
                        if (particles_[p].currentTime >= particles_[p].lifeTime) {
                            particles_[p] = MakeNewParticle(kTypeHit, enemy.position);
                            sparkCount--;
                        }
                    }

                    // 2. 衝撃波リング (kTypeRing) 2枚
                    int ringCount = 2;
                    for (uint32_t r = 0; r < kRingInstanceCount && ringCount > 0; ++r) {
                        if (ringParticles_[r].currentTime >= ringParticles_[r].lifeTime) {
                            ringParticles_[r] = MakeNewParticle(kTypeRing, enemy.position);
                            ringCount--;
                        }
                    }

                    // 3. 閃光シリンダー (kTypeCylinder) 1本
                    int cylinderCount = 1;
                    for (uint32_t c = 0; c < kCylinderInstanceCount && cylinderCount > 0; ++c) {
                        if (cylinderParticles_[c].currentTime >= cylinderParticles_[c].lifeTime) {
                            cylinderParticles_[c] = MakeNewParticle(kTypeCylinder, enemy.position);
                            cylinderCount--;
                        }
                    }

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
    for (int i = 0; i < kMaxEnemies; ++i) {
        if (enemies_[i].isAlive) {
            // カメラ（プレイヤー）より後ろに行った敵は画面外 → 死亡扱い
            if (enemies_[i].position.z < camTransForEnemy.translate.z - 20.0f) {
                enemies_[i].isAlive = false;
                // 次の敵を出現させる
                if (nextEnemyIndex_ < kMaxEnemies) {
                    enemies_[nextEnemyIndex_].isAlive = true;
                    nextEnemyIndex_++;
                }
                enemyTransformData_[i]->World = MakeIdentity4x4();
                enemyTransformData_[i]->WVP = MakeIdentity4x4();
                continue;
            }

            // ゆっくりY軸回転させて3D感を演出（回転しないようにコメントアウト）
            // enemies_[i].rotate.y += 1.0f * kDeltaTime;

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

    // 戦闘機モードの場合のジェット噴射エミッター位置の計算
    Vector3 leftJetPos = { 0.0f, 0.0f, 0.0f };
    Vector3 rightJetPos = { 0.0f, 0.0f, 0.0f };
    if (sceneMode_ == SceneMode::kFighter) {
        EulerTransform& camTrans = camera_->GetTransform();
        Vector3 fighterWorldPos = {
            camTrans.translate.x + fighterModel_->transform.translate.x,
            camTrans.translate.y - 3.0f + fighterModel_->transform.translate.y,
            camTrans.translate.z + 65.0f
        };
        // 左右のジェットエンジンノズル（位置を少し上に調整し、機体中心からX方向に±0.8f、後方Z方向に-3.0f）
        leftJetPos = { fighterWorldPos.x - 0.3f, fighterWorldPos.y + 0.8f, fighterWorldPos.z - 3.0f }; // 左ジェットを右寄りに調整
        rightJetPos = { fighterWorldPos.x + 0.8f, fighterWorldPos.y + 0.8f, fighterWorldPos.z - 3.0f };
    }

    for (uint32_t i = 0; i < kNumInstances; ++i) {
        if (particles_[i].currentTime >= particles_[i].lifeTime) {
            if (sceneMode_ == SceneMode::kFighter) {
                // 偶数は左、奇数は右のエンジンノズルから噴射
                Vector3 jetPos = (i % 2 == 0) ? leftJetPos : rightJetPos;
                particles_[i] = MakeNewParticle(kTypeJetExhaust, jetPos);
            } else {
                particles_[i] = MakeNewParticle(currentEffect_, emitterPos_);
            }
        }
        if (useGravity_) {
            particles_[i].velocity.y -= 9.8f * kDeltaTime * 0.5f;
        }
        particles_[i].transform.translate.x += particles_[i].velocity.x * kDeltaTime;
        particles_[i].transform.translate.y += particles_[i].velocity.y * kDeltaTime;
        particles_[i].transform.translate.z += particles_[i].velocity.z * kDeltaTime;
        particles_[i].currentTime += kDeltaTime;
        float alpha = 1.0f - (particles_[i].currentTime / particles_[i].lifeTime);
        particles_[i].color.w = alpha;

        Matrix4x4 worldMatrix = MakeAffineMatrix(particles_[i].transform.scale, particles_[i].transform.rotate, particles_[i].transform.translate);
        instancingData_[i].World = worldMatrix;
        instancingData_[i].WVP = Multiply(worldMatrix, viewProjectionMatrix);
        instancingData_[i].color = particles_[i].color;
        instancingData_[i].uvTransform = particles_[i].uvTransform;
    }

    // リングの更新処理
    for (uint32_t i = 0; i < kRingInstanceCount; ++i) {
        if (ringParticles_[i].currentTime < ringParticles_[i].lifeTime) {
            ringParticles_[i].currentTime += kDeltaTime;
            float alpha = 1.0f - (ringParticles_[i].currentTime / ringParticles_[i].lifeTime);
            ringParticles_[i].color.w = alpha;
            
            // 拡大アニメーション
            ringParticles_[i].transform.scale.x += 15.0f * kDeltaTime;
            ringParticles_[i].transform.scale.y += 15.0f * kDeltaTime;

            // UVスクロール（V方向に時間とともに移動）
            float uvScrollV = ringParticles_[i].currentTime * -2.0f; 
            ringParticles_[i].uvTransform = MakeAffineMatrix(Vector3{1.0f, 1.0f, 1.0f}, Vector3{0.0f, 0.0f, 0.0f}, Vector3{0.0f, uvScrollV, 0.0f});

            Matrix4x4 worldMatrix = MakeAffineMatrix(ringParticles_[i].transform.scale, ringParticles_[i].transform.rotate, ringParticles_[i].transform.translate);
            ringInstancingData_[i].World = worldMatrix;
            ringInstancingData_[i].WVP = Multiply(worldMatrix, viewProjectionMatrix);
            ringInstancingData_[i].color = ringParticles_[i].color;
            ringInstancingData_[i].uvTransform = ringParticles_[i].uvTransform;
        } else {
            ringInstancingData_[i].color.w = 0.0f;
        }
    }

    // Cylinderの更新処理
    for (uint32_t i = 0; i < kCylinderInstanceCount; ++i) {
        if (cylinderParticles_[i].currentTime < cylinderParticles_[i].lifeTime) {
            cylinderParticles_[i].currentTime += kDeltaTime;
            float alpha = 1.0f - (cylinderParticles_[i].currentTime / cylinderParticles_[i].lifeTime);
            cylinderParticles_[i].color.w = alpha;
            
            // UVスクロール（横方向に移動）と、V方向のFlipを組み合わせる
            float uvScrollU = cylinderParticles_[i].currentTime * 1.0f; // 横方向のスクロール
            // Flip v: v = -v + 1 => scaleY = -1, transY = 1
            cylinderParticles_[i].uvTransform = MakeAffineMatrix(Vector3{1.0f, -1.0f, 1.0f}, Vector3{0.0f, 0.0f, 0.0f}, Vector3{uvScrollU, 1.0f, 0.0f});

            Matrix4x4 worldMatrix = MakeAffineMatrix(cylinderParticles_[i].transform.scale, cylinderParticles_[i].transform.rotate, cylinderParticles_[i].transform.translate);
            cylinderInstancingData_[i].World = worldMatrix;
            cylinderInstancingData_[i].WVP = Multiply(worldMatrix, viewProjectionMatrix);
            cylinderInstancingData_[i].color = cylinderParticles_[i].color;
            cylinderInstancingData_[i].uvTransform = cylinderParticles_[i].uvTransform;
        } else {
            cylinderInstancingData_[i].color.w = 0.0f;
        }
    }

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
    const char* items[] = { "None", "Grayscale", "Sepia", "Vignette", "BoxFilter", "Outline" };
    int currentItem = static_cast<int>(activePostProcess_);
    if (ImGui::Combo("Effect", &currentItem, items, IM_ARRAYSIZE(items))) {
        activePostProcess_ = static_cast<PostProcessType>(currentItem);
    }
    if (activePostProcess_ == kVignette) {
        ImGui::SliderFloat("Vignette Scale", &vignetteParamData_->scale, 0.0f, 32.0f);
        ImGui::SliderFloat("Vignette Power", &vignetteParamData_->power, 0.0f, 5.0f);
    }
    if (activePostProcess_ == kBoxFilter) {
        ImGui::SliderInt("Kernel Size (k)", &boxFilterParamData_->kernelSize, 1, 10);
    }
    ImGui::End();
#endif
}

void GamePlayScene::Draw() {
    ID3D12GraphicsCommandList* commandList = dxCommon_->GetCommandList();

    // --- 1. RenderTextureへの描画開始 ---
    postProcess_->PreDraw();

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
            if (fighterModel_ && showEnemies_) {
                for (int i = 0; i < kMaxEnemies; ++i) {
                    if (enemies_[i].isAlive) {
                        commandList->SetGraphicsRootConstantBufferView(1, enemyTransformResources_[i]->GetGPUVirtualAddress());
                        fighterModel_->DrawModel(commandList, TextureManager::GetInstance()->GetSrvHandleGPU("Player2/Player_basecolor.JPEG"), TextureManager::GetInstance()->GetSrvHandleGPU("test.dds"));
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
        ID3D12DescriptorHeap* descriptorHeaps[] = { srvDescriptorHeap_.Get() };
        commandList->SetDescriptorHeaps(1, descriptorHeaps);

        if (graphicsPipeline_ && graphicsPipeline_->GetRootSignature()) {
            commandList->SetGraphicsRootSignature(graphicsPipeline_->GetRootSignature());
            commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

            BlendMode blendMode = useAdditiveBlend_ ? kBlendModeAdd : kBlendModeNormal;
            if (graphicsPipeline_->GetPipelineState(blendMode)) {
            commandList->SetPipelineState(graphicsPipeline_->GetPipelineState(blendMode));
            if (particleModel_) {
                particleModel_->Draw(commandList, kNumInstances, textureSrvHandleGPU_, instancingSrvHandleGPU_);
            }
            if (ringModel_) {
                ringModel_->Draw(commandList, kRingInstanceCount, gradationSrvHandleGPU_, ringInstancingSrvHandleGPU_);
            }
            if (cylinderModel_) {
                cylinderModel_->Draw(commandList, kCylinderInstanceCount, gradationSrvHandleGPU_, cylinderInstancingSrvHandleGPU_);
            }
        }

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
        pso = graphicsPipeline_->GetLuminanceOutlinePipelineState();
        break;
    }

    // Fullscreenパイプラインで描画
    commandList->SetPipelineState(pso);
    commandList->SetGraphicsRootSignature(graphicsPipeline_->GetFullscreenRootSignature());
    SrvManager::GetInstance()->SetGraphicsRootDescriptorTable(0, postProcess_->GetSrvIndex());
    if (activePostProcess_ == kVignette && vignetteParamResource_) {
        commandList->SetGraphicsRootConstantBufferView(1, vignetteParamResource_->GetGPUVirtualAddress());
    } else if (activePostProcess_ == kBoxFilter && boxFilterParamResource_) {
        commandList->SetGraphicsRootConstantBufferView(1, boxFilterParamResource_->GetGPUVirtualAddress());
    }
    commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    commandList->DrawInstanced(3, 1, 0, 0);
}

Particle GamePlayScene::MakeNewParticle(int type, const Vector3& emitterPos) {
    Particle particle;
    particle.transform.scale = { 1.0f, 1.0f, 1.0f };
    particle.transform.rotate = { 0.0f, 0.0f, 0.0f };
    particle.currentTime = 0.0f;
    particle.uvTransform = MakeIdentity4x4();

    std::uniform_real_distribution<float> distPos(-1.0f, 1.0f);
    std::uniform_real_distribution<float> distVel(-1.0f, 1.0f);
    std::uniform_real_distribution<float> distColor(0.0f, 1.0f);
    std::uniform_real_distribution<float> distLife(1.0f, 3.0f);

    switch (type) {
    case kTypeExplosion:
    default:
        particle.transform.translate = {
            emitterPos.x + distPos(randomEngine_) * 0.5f,
            emitterPos.y + distPos(randomEngine_) * 0.5f,
            emitterPos.z + distPos(randomEngine_) * 0.5f
        };
        particle.velocity = { distVel(randomEngine_), distVel(randomEngine_), distVel(randomEngine_) };
        particle.lifeTime = distLife(randomEngine_);
        particle.color = { distColor(randomEngine_), distColor(randomEngine_), distColor(randomEngine_), 1.0f };
        break;
    case kTypeFountain:
        particle.transform.translate = {
            emitterPos.x + distPos(randomEngine_) * 0.2f,
            emitterPos.y,
            emitterPos.z + distPos(randomEngine_) * 0.2f
        };
        particle.velocity = { distVel(randomEngine_) * 0.5f, 2.0f + std::abs(distVel(randomEngine_)), distVel(randomEngine_) * 0.5f };
        particle.lifeTime = 2.0f;
        particle.color = { 0.2f, 0.5f, 1.0f, 1.0f };
        break;
    case kTypeSpiral:
    {
        float angle = distPos(randomEngine_) * (float)M_PI;
        float radius = 1.5f;
        particle.transform.translate = {
            emitterPos.x + std::cos(angle) * radius,
            emitterPos.y,
            emitterPos.z + std::sin(angle) * radius
        };
        particle.velocity = { 0.0f, 1.0f, 0.0f };
        particle.lifeTime = 3.0f;
        particle.color = { distColor(randomEngine_), distColor(randomEngine_), distColor(randomEngine_), 1.0f };
    }
    break;
    case kTypeRain:
        particle.transform.translate = {
            emitterPos.x + distPos(randomEngine_) * 5.0f,
            emitterPos.y + 5.0f,
            emitterPos.z + distPos(randomEngine_) * 5.0f
        };
        particle.velocity = { 0.0f, -3.0f, 0.0f };
        particle.lifeTime = 3.0f;
        particle.color = { 0.8f, 0.8f, 1.0f, 1.0f };
        particle.transform.scale = { 0.2f, 1.0f, 0.2f };
        break;
    case kTypeHit:
    {
        // ★超ド派手スパーク★ 大きく・速く・鮮やかに！
        std::uniform_real_distribution<float> distScale(0.5f, 2.0f);
        std::uniform_real_distribution<float> distRotate(-(float)M_PI, (float)M_PI);
        std::uniform_real_distribution<float> distSparkVel(-15.0f, 15.0f);

        float sc = distScale(randomEngine_);
        particle.transform.scale = { sc * 0.5f, sc * 0.5f, sc * 0.5f };
        particle.transform.rotate = { distRotate(randomEngine_), distRotate(randomEngine_), distRotate(randomEngine_) };

        // 爆発中心からランダムにばらつかせる
        particle.transform.translate = {
            emitterPos.x + distPos(randomEngine_) * 3.0f,
            emitterPos.y + distPos(randomEngine_) * 3.0f,
            emitterPos.z + distPos(randomEngine_) * 3.0f
        };

        // 超高速で全方向に飛び散る！
        particle.velocity = {
            distSparkVel(randomEngine_),
            distSparkVel(randomEngine_),
            distSparkVel(randomEngine_)
        };

        // 火花色：白熱コア → 黄金 → 深紅 → 深いオレンジのグラデーション
        float colorSelect = distColor(randomEngine_);
        if (colorSelect < 0.2f) {
            particle.color = { 1.0f, 1.0f, 0.9f, 1.0f }; // 白熱コア（超高温部）
        } else if (colorSelect < 0.45f) {
            particle.color = { 1.0f, 0.9f, 0.2f, 1.0f }; // 黄金の輝き
        } else if (colorSelect < 0.7f) {
            particle.color = { 1.0f, 0.5f, 0.0f, 1.0f }; // 鮮やかオレンジ
        } else if (colorSelect < 0.85f) {
            particle.color = { 1.0f, 0.2f, 0.0f, 1.0f }; // 深紅の炎
        } else {
            particle.color = { 0.8f, 0.1f, 0.0f, 1.0f }; // ダークレッド（残り火）
        }
        particle.lifeTime = 0.3f + distColor(randomEngine_) * 0.7f;
        break;
    }
    case kTypeRing:
    {
        // ★衝撃波リング★ 大きく広がる！
        std::uniform_real_distribution<float> distRingScale(0.3f, 1.0f);
        float ringInitScale = distRingScale(randomEngine_);
        particle.transform.scale = { ringInitScale, ringInitScale, 1.0f };
        // カメラの向きに合わせてリングをビルボード化
        EulerTransform& camTrans = camera_->GetTransform();
        std::uniform_real_distribution<float> distRingRot(-(float)M_PI * 0.3f, (float)M_PI * 0.3f);
        particle.transform.rotate = {
            camTrans.rotate.x + distRingRot(randomEngine_),
            camTrans.rotate.y + distRingRot(randomEngine_),
            distRingRot(randomEngine_)
        };

        // 爆発位置に配置
        particle.transform.translate = emitterPos;

        particle.velocity = { 0.0f, 0.0f, 0.0f };
        // 各リングでランダムに色を変えて、より豪華に
        float ringColor = distColor(randomEngine_);
        if (ringColor < 0.5f) {
            particle.color = { 1.0f, 0.7f, 0.1f, 1.0f }; // ゴールデンオレンジ
        } else {
            particle.color = { 1.0f, 0.3f, 0.05f, 1.0f }; // ディープレッド
        }
        particle.lifeTime = 0.5f + distColor(randomEngine_) * 0.5f;
        break;
    }
    case kTypeCylinder:
    {
        // ★閃光シリンダー★ ランダム方向に回転して噴き出す！
        std::uniform_real_distribution<float> distCylScale(1.5f, 4.0f);
        std::uniform_real_distribution<float> distCylRot(-(float)M_PI, (float)M_PI);
        std::uniform_real_distribution<float> distCylVel(-5.0f, 5.0f);
        float cylSc = distCylScale(randomEngine_);
        particle.transform.scale = { cylSc, cylSc, cylSc };
        particle.transform.rotate = {
            distCylRot(randomEngine_),
            distCylRot(randomEngine_),
            distCylRot(randomEngine_)
        };

        // 爆発位置に配置
        particle.transform.translate = emitterPos;

        // ランダムな方向に上昇・拡散する閃光柱
        particle.velocity = {
            distCylVel(randomEngine_),
            2.0f + std::abs(distCylVel(randomEngine_)),
            distCylVel(randomEngine_)
        };
        float cylColor = distColor(randomEngine_);
        if (cylColor < 0.5f) {
            particle.color = { 1.0f, 0.5f, 0.05f, 1.0f }; // 炎のオレンジ
        } else {
            particle.color = { 1.0f, 0.8f, 0.3f, 1.0f }; // 黄金色の閃光
        }
        particle.lifeTime = 0.6f + distColor(randomEngine_) * 0.6f;
        break;
    }
    case kTypeJetExhaust:
    {
        // ジェット噴射の演出
        // エミッター位置の周辺にランダムに少しばらつかせる
        std::uniform_real_distribution<float> distSpread(-0.1f, 0.1f);
        particle.transform.translate = {
            emitterPos.x + distSpread(randomEngine_),
            emitterPos.y + distSpread(randomEngine_),
            emitterPos.z + distSpread(randomEngine_)
        };

        // 速度: カメラ/戦闘機の進行速度(z軸+30.0f)より後ろに行くように設定
        // z速度を+5.0f〜+15.0f程度にすることで、機体の後ろへと綺麗に流れる（相対的に後方へ動く）
        std::uniform_real_distribution<float> distVelZ(5.0f, 15.0f);
        std::uniform_real_distribution<float> distVelSpread(-0.8f, 0.8f);
        particle.velocity = {
            distVelSpread(randomEngine_),
            distVelSpread(randomEngine_),
            distVelZ(randomEngine_)
        };

        // サイズ: 徐々に小さく見せるために、初期サイズをほどよい大きさに
        std::uniform_real_distribution<float> distScale(0.3f, 0.6f);
        float sc = distScale(randomEngine_);
        particle.transform.scale = { sc, sc, sc };

        // 寿命: 短めにして、噴射口のすぐ後ろで消えるようにする
        std::uniform_real_distribution<float> distLife(0.2f, 0.5f);
        particle.lifeTime = distLife(randomEngine_);

        // 色: レッド/オレンジ/ホワイトの美しい超高温プラズマ・バーナー炎のグラデーション
        std::uniform_real_distribution<float> distColor(0.0f, 1.0f);
        float colorSelect = distColor(randomEngine_);
        if (colorSelect < 0.5f) {
            particle.color = { 1.0f, 0.1f, 0.0f, 1.0f }; // 熱いレッド
        } else if (colorSelect < 0.8f) {
            particle.color = { 1.0f, 0.5f, 0.0f, 1.0f }; // 鮮やかなオレンジ
        } else {
            particle.color = { 1.0f, 1.0f, 0.8f, 1.0f }; // ホワイト（コアの超高温部）
        }
        break;
    }
    }
    return particle;
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
