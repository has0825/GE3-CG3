#include "GamePlayScene.h"
#include "WinApp.h"
#include "SceneManager.h"
#include "D3D12Util.h"

#ifdef _DEBUG
#include "externals/imgui/imgui.h"
#include "externals/imgui/imgui_impl_dx12.h"
#endif

// --- Game.cpp にあった算術ヘルパー関数 ---
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static Vector3 Add(const Vector3& v1, const Vector3& v2) {
    return { v1.x + v2.x, v1.y + v2.y, v1.z + v2.z };
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
    // シングルトン取得
    dxCommon_ = DirectXCommon::GetInstance();
    input_ = Input::GetInstance();

    // デバイス取得
    ID3D12Device* device = dxCommon_->GetDevice();
    ID3D12GraphicsCommandList* commandList = dxCommon_->GetCommandList();

    audio_ = std::make_unique<Audio>();
    audio_->Initialize();

    graphicsPipeline_ = std::make_unique<GraphicsPipeline>();
    graphicsPipeline_->Initialize(device);

    // SRVヒープもここで生成
    srvDescriptorHeap_ = CreateDescriptorHeap(device, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 128, true);
    descriptorSizeSRV_ = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    // 乱数初期化
    std::random_device seedGenerator;
    randomEngine_.seed(seedGenerator());

    // モデル生成
    particleModel_ = std::unique_ptr<Model>(Model::CreateParticleModel(device));

    // インスタンシングバッファ作成 (Particles)
    instancingResource_ = CreateBufferResource(device, sizeof(ParticleForGPU) * kNumInstances);
    instancingResource_->Map(0, nullptr, reinterpret_cast<void**>(&instancingData_));

    // インスタンシングバッファ作成 (Sprite)
    spriteInstancingResource_ = CreateBufferResource(device, sizeof(ParticleForGPU) * kSpriteInstanceCount);
    spriteInstancingResource_->Map(0, nullptr, reinterpret_cast<void**>(&spriteInstancingData_));

    // パーティクル初期設定
    particles_.resize(kNumInstances);
    for (UINT i = 0; i < kNumInstances; ++i) {
        particles_[i] = MakeNewParticle(currentEffect_, emitterPos_);
        std::uniform_real_distribution<float> distTime(0.0f, 3.0f);
        particles_[i].currentTime = distTime(randomEngine_);
    }

    // GPUデータ初期化
    for (UINT i = 0; i < kNumInstances; ++i) {
        instancingData_[i].WVP = MakeIdentity4x4();
        instancingData_[i].World = MakeIdentity4x4();
        instancingData_[i].color = particles_[i].color;
    }

    // --- テクスチャ読み込みとSRV作成 ---

    // 1. Particle Texture
    DirectX::ScratchImage mipImages = LoadTexture("resources/circle.png");
    const DirectX::TexMetadata& metadata = mipImages.GetMetadata();
    textureResource_ = CreateTextureResource(device, metadata);
    intermediateResource_ = UploadTextureData(textureResource_.Get(), mipImages, device, commandList);

    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
    srvDesc.Format = metadata.format;
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MipLevels = UINT(metadata.mipLevels);

    // Index 1を使用
    D3D12_CPU_DESCRIPTOR_HANDLE textureSrvHandleCPU = GetCPUDescriptorHandle(srvDescriptorHeap_.Get(), descriptorSizeSRV_, 1);
    textureSrvHandleGPU_ = GetGPUDescriptorHandle(srvDescriptorHeap_.Get(), descriptorSizeSRV_, 1);
    device->CreateShaderResourceView(textureResource_.Get(), &srvDesc, textureSrvHandleCPU);

    // 2. Instancing Buffer SRV
    D3D12_SHADER_RESOURCE_VIEW_DESC instancingSrvDesc{};
    instancingSrvDesc.Format = DXGI_FORMAT_UNKNOWN;
    instancingSrvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    instancingSrvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
    instancingSrvDesc.Buffer.FirstElement = 0;
    instancingSrvDesc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;
    instancingSrvDesc.Buffer.NumElements = kNumInstances;
    instancingSrvDesc.Buffer.StructureByteStride = sizeof(ParticleForGPU);

    D3D12_CPU_DESCRIPTOR_HANDLE instancingSrvHandleCPU = GetCPUDescriptorHandle(srvDescriptorHeap_.Get(), descriptorSizeSRV_, 2);
    instancingSrvHandleGPU_ = GetGPUDescriptorHandle(srvDescriptorHeap_.Get(), descriptorSizeSRV_, 2);
    device->CreateShaderResourceView(instancingResource_.Get(), &instancingSrvDesc, instancingSrvHandleCPU);

    // 3. Text Texture
    DirectX::ScratchImage textMipImages = LoadTexture("resources/text1.png");
    const DirectX::TexMetadata& textMetadata = textMipImages.GetMetadata();
    textTextureResource_ = CreateTextureResource(device, textMetadata);
    textIntermediateResource_ = UploadTextureData(textTextureResource_.Get(), textMipImages, device, commandList);

    D3D12_SHADER_RESOURCE_VIEW_DESC textSrvDesc{};
    textSrvDesc.Format = textMetadata.format;
    textSrvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    textSrvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    textSrvDesc.Texture2D.MipLevels = UINT(textMetadata.mipLevels);

    D3D12_CPU_DESCRIPTOR_HANDLE textSrvHandleCPU = GetCPUDescriptorHandle(srvDescriptorHeap_.Get(), descriptorSizeSRV_, 3);
    textSrvHandleGPU_ = GetGPUDescriptorHandle(srvDescriptorHeap_.Get(), descriptorSizeSRV_, 3);
    device->CreateShaderResourceView(textTextureResource_.Get(), &textSrvDesc, textSrvHandleCPU);

    // 4. Sprite Instancing SRV
    D3D12_SHADER_RESOURCE_VIEW_DESC spriteInstancingSrvDesc{};
    spriteInstancingSrvDesc.Format = DXGI_FORMAT_UNKNOWN;
    spriteInstancingSrvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    spriteInstancingSrvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
    spriteInstancingSrvDesc.Buffer.FirstElement = 0;
    spriteInstancingSrvDesc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;
    spriteInstancingSrvDesc.Buffer.NumElements = kSpriteInstanceCount;
    spriteInstancingSrvDesc.Buffer.StructureByteStride = sizeof(ParticleForGPU);

    D3D12_CPU_DESCRIPTOR_HANDLE spriteInstancingSrvHandleCPU = GetCPUDescriptorHandle(srvDescriptorHeap_.Get(), descriptorSizeSRV_, 4);
    spriteInstancingSrvHandleGPU_ = GetGPUDescriptorHandle(srvDescriptorHeap_.Get(), descriptorSizeSRV_, 4);
    device->CreateShaderResourceView(spriteInstancingResource_.Get(), &spriteInstancingSrvDesc, spriteInstancingSrvHandleCPU);

    // コマンド実行待ち（テクスチャ転送）
    commandList->Close();
    ID3D12CommandQueue* commandQueue = dxCommon_->GetCommandQueue();
    ID3D12CommandList* ppCommandLists[] = { commandList };
    commandQueue->ExecuteCommandLists(1, ppCommandLists);

    Microsoft::WRL::ComPtr<ID3D12Fence> fence;
    device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence));
    HANDLE fenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
    commandQueue->Signal(fence.Get(), 1);
    if (fence->GetCompletedValue() < 1) {
        fence->SetEventOnCompletion(1, fenceEvent);
        WaitForSingleObject(fenceEvent, INFINITE);
    }
    CloseHandle(fenceEvent);

    // コマンドリストリセット
    dxCommon_->GetCommandAllocator()->Reset();
    commandList->Reset(dxCommon_->GetCommandAllocator(), nullptr);

    // カメラ初期化
    camera_ = std::make_unique<Camera>(WinApp::kClientWidth, WinApp::kClientHeight);
    camera_->SetTranslate({ 0.0f, 0.0f, -15.0f });

    // 音声ロード
    bgmData_ = audio_->LoadAudio("resources/result.mp3");
    jumpSE_ = audio_->LoadAudio("resources/damage.mp3");
    audio_->PlayWave(bgmData_, true, 0.5f);
}

void GamePlayScene::Finalize() {
    // ★ここにあった delete graphicsPipeline_ や delete audio_ は
    // unique_ptrにより自動的に解放されるため削除（コメントアウトも残しません）

    if (audio_) {
        audio_->Finalize();
    }
}

void GamePlayScene::Update() {
    // ImGui処理 (デバッグビルドのみ)
#ifdef _DEBUG
    ImGui::SetNextWindowSize(ImVec2(500, 100));

    // ウィンドウ名
    ImGui::Begin("Sprite Control");

    // spritePos_.x と .y を操作対象にします
    ImGui::DragFloat2("Position", &spritePos_.x, 1.0f, -2000.0f, 2000.0f, "%.1f");

    ImGui::End();
#endif

    // キーボード入力
    if (input_->IsKeyPressed(DIK_1)) currentEffect_ = kTypeExplosion;
    if (input_->IsKeyPressed(DIK_2)) currentEffect_ = kTypeFountain;
    if (input_->IsKeyPressed(DIK_3)) currentEffect_ = kTypeSpiral;
    if (input_->IsKeyPressed(DIK_4)) currentEffect_ = kTypeRain;
    if (input_->IsKeyTriggered(DIK_G)) useGravity_ = !useGravity_;

    // スペースキーでSE
    if (input_->IsKeyTriggered(DIK_SPACE)) {
        audio_->PlayWave(jumpSE_, false, 1.0f);
    }

    // カメラ移動
    Vector3 moveDir = { 0.0f, 0.0f, 0.0f };
    if (input_->IsKeyPressed(DIK_W)) moveDir.z += 1.0f;
    if (input_->IsKeyPressed(DIK_S)) moveDir.z -= 1.0f;
    if (input_->IsKeyPressed(DIK_D)) moveDir.x += 1.0f;
    if (input_->IsKeyPressed(DIK_A)) moveDir.x -= 1.0f;
    if (input_->IsKeyPressed(DIK_E)) moveDir.y += 1.0f;
    if (input_->IsKeyPressed(DIK_Q)) moveDir.y -= 1.0f;

    float cameraSpeed = 5.0f;
    const float kDeltaTime = 1.0f / 60.0f;

    Camera::Transform& camTrans = camera_->GetTransform();
    if (moveDir.x != 0.0f || moveDir.y != 0.0f || moveDir.z != 0.0f) {
        Matrix4x4 cameraRotY = MakeRotateYMatrix(camTrans.rotate.y);
        Vector3 rotatedMoveDir = TransformNormal(moveDir, cameraRotY);
        rotatedMoveDir = Normalize(rotatedMoveDir);
        rotatedMoveDir = Scale(rotatedMoveDir, cameraSpeed * kDeltaTime);
        camTrans.translate = Add(camTrans.translate, rotatedMoveDir);
    }
    camera_->Update();
    Matrix4x4 viewProjectionMatrix = camera_->GetViewProjectionMatrix();

    // パーティクル更新
    for (uint32_t i = 0; i < kNumInstances; ++i) {
        if (particles_[i].currentTime >= particles_[i].lifeTime) {
            particles_[i] = MakeNewParticle(currentEffect_, emitterPos_);
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
    }

    // スプライト（テキスト）更新
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
}

void GamePlayScene::Draw() {
    ID3D12GraphicsCommandList* commandList = dxCommon_->GetCommandList();
    ID3D12DescriptorHeap* descriptorHeaps[] = { srvDescriptorHeap_.Get() };
    commandList->SetDescriptorHeaps(1, descriptorHeaps);

    commandList->SetGraphicsRootSignature(graphicsPipeline_->GetRootSignature());
    commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    BlendMode blendMode = useAdditiveBlend_ ? kBlendModeAdd : kBlendModeNormal;
    commandList->SetPipelineState(graphicsPipeline_->GetPipelineState(blendMode));
    particleModel_->Draw(commandList, kNumInstances, textureSrvHandleGPU_, instancingSrvHandleGPU_);

    commandList->SetPipelineState(graphicsPipeline_->GetPipelineState(kBlendModeNormal));
    particleModel_->Draw(commandList, kSpriteInstanceCount, textSrvHandleGPU_, spriteInstancingSrvHandleGPU_);
}

Particle GamePlayScene::MakeNewParticle(int type, const Vector3& emitterPos) {
    Particle particle;
    particle.transform.scale = { 1.0f, 1.0f, 1.0f };
    particle.transform.rotate = { 0.0f, 0.0f, 0.0f };
    particle.currentTime = 0.0f;

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
    }
    return particle;
}