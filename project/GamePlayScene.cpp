#include "GamePlayScene.h"
#include "WinApp.h"
#include "SceneManager.h"
#include "D3D12Util.h"
#include "MathUtil.h"
#include "DataTypes.h"
#include "TextureManager.h"
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

    graphicsPipeline_ = std::make_unique<GraphicsPipeline>();
    graphicsPipeline_->Initialize(device);

    srvDescriptorHeap_ = CreateDescriptorHeap(device, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 128, true);
    descriptorSizeSRV_ = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

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

    D3D12_CPU_DESCRIPTOR_HANDLE textureSrvHandleCPU = GetCPUDescriptorHandle(srvDescriptorHeap_.Get(), descriptorSizeSRV_, 1);
    textureSrvHandleGPU_ = GetGPUDescriptorHandle(srvDescriptorHeap_.Get(), descriptorSizeSRV_, 1);
    device->CreateShaderResourceView(textureResource_.Get(), &srvDesc, textureSrvHandleCPU);

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

    D3D12_SHADER_RESOURCE_VIEW_DESC ringInstancingSrvDesc{};
    ringInstancingSrvDesc.Format = DXGI_FORMAT_UNKNOWN;
    ringInstancingSrvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    ringInstancingSrvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
    ringInstancingSrvDesc.Buffer.FirstElement = 0;
    ringInstancingSrvDesc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;
    ringInstancingSrvDesc.Buffer.NumElements = kRingInstanceCount;
    ringInstancingSrvDesc.Buffer.StructureByteStride = sizeof(ParticleForGPU);

    D3D12_CPU_DESCRIPTOR_HANDLE ringInstancingSrvHandleCPU = GetCPUDescriptorHandle(srvDescriptorHeap_.Get(), descriptorSizeSRV_, 5);
    ringInstancingSrvHandleGPU_ = GetGPUDescriptorHandle(srvDescriptorHeap_.Get(), descriptorSizeSRV_, 5);
    device->CreateShaderResourceView(ringInstancingResource_.Get(), &ringInstancingSrvDesc, ringInstancingSrvHandleCPU);

    D3D12_SHADER_RESOURCE_VIEW_DESC cylinderInstancingSrvDesc{};
    cylinderInstancingSrvDesc.Format = DXGI_FORMAT_UNKNOWN;
    cylinderInstancingSrvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    cylinderInstancingSrvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
    cylinderInstancingSrvDesc.Buffer.FirstElement = 0;
    cylinderInstancingSrvDesc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;
    cylinderInstancingSrvDesc.Buffer.NumElements = kCylinderInstanceCount;
    cylinderInstancingSrvDesc.Buffer.StructureByteStride = sizeof(ParticleForGPU);

    D3D12_CPU_DESCRIPTOR_HANDLE cylinderInstancingSrvHandleCPU = GetCPUDescriptorHandle(srvDescriptorHeap_.Get(), descriptorSizeSRV_, 6);
    cylinderInstancingSrvHandleGPU_ = GetGPUDescriptorHandle(srvDescriptorHeap_.Get(), descriptorSizeSRV_, 6);
    device->CreateShaderResourceView(cylinderInstancingResource_.Get(), &cylinderInstancingSrvDesc, cylinderInstancingSrvHandleCPU);

    commandList->Close();
    ID3D12CommandQueue* commandQueue = dxCommon_->GetCommandQueue();
    ID3D12CommandList* ppCommandLists[] = { commandList };
    commandQueue->ExecuteCommandLists(1, ppCommandLists);

    Microsoft::WRL::ComPtr<ID3D12Fence> fence;
    device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence));
    HANDLE fenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
    assert(fenceEvent != nullptr);
    commandQueue->Signal(fence.Get(), 1);
    if (fence->GetCompletedValue() < 1) {
        fence->SetEventOnCompletion(1, fenceEvent);
        WaitForSingleObject(fenceEvent, INFINITE);
    }
    CloseHandle(fenceEvent);

    dxCommon_->GetCommandAllocator()->Reset();
    commandList->Reset(dxCommon_->GetCommandAllocator(), nullptr);

    bgmData_ = audio_->LoadAudio("resources/result.mp3");
    jumpSE_ = audio_->LoadAudio("resources/damage.mp3");
    audio_->PlayWave(bgmData_, true, 0.5f);

    TextureManager::GetInstance()->Initialize(device, "resources/");
    TextureManager::GetInstance()->LoadTexture("test.dds");
    TextureManager::GetInstance()->LoadTexture("circle2.png");
    TextureManager::GetInstance()->LoadTexture("gradationLine.png");
    TextureManager::GetInstance()->LoadTexture("human/white.png");

    skybox_ = std::make_unique<Skybox>();
    skybox_->Initialize(device);

    playerModel_ = Model::LoadGLTF("Resources/human/walk.gltf", device);

    // 【重要】モデルのスケール・座標設定
    // ※もし画面に見えない場合は、ここ(scale)を 10.0f や 100.0f など大きくしてみてください。
    playerModel_->transform.scale = { 10.0f, 10.0f, 10.0f };
    playerModel_->transform.translate = { 0.0f, 0.0f, 0.0f };
    playerModel_->transform.rotate = { 0.0f, 0.0f, 0.0f };
    playerModel_->SetEnvironmentCoefficient(modelEnvCoefficient_);

    isCursorLocked_ = false;
}

void GamePlayScene::Finalize() {
    if (audio_) {
        audio_->Finalize();
    }
}

void GamePlayScene::Update() {
#ifdef USE_IMGUI
    ImGui::SetNextWindowSize(ImVec2(500, 100));
    ImGui::Begin("Sprite Control");
    ImGui::DragFloat2("Position", &spritePos_.x, 1.0f, -2000.0f, 2000.0f, "%.1f");
    if (ImGui::SliderFloat("Model Reflection", &modelEnvCoefficient_, 0.0f, 1.0f)) {
        if (playerModel_) playerModel_->SetEnvironmentCoefficient(modelEnvCoefficient_);
    }
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
    HWND hwnd = WinApp::GetInstance()->GetHwnd();

    if (input_->IsMousePressed(0)) {
        if (!isCursorLocked_) {
            ShowCursor(FALSE);
            isCursorLocked_ = true;
        }
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
        if (isCursorLocked_) {
            ShowCursor(TRUE);
            isCursorLocked_ = false;
        }
    }

    if (moveDir.x != 0.0f || moveDir.y != 0.0f || moveDir.z != 0.0f) {
        Matrix4x4 cameraRotY = MakeRotateYMatrix(camTrans.rotate.y);
        Vector3 rotatedMoveDir = TransformNormal(moveDir, cameraRotY);
        rotatedMoveDir = Normalize(rotatedMoveDir);
        rotatedMoveDir = Scale(rotatedMoveDir, cameraSpeed * kDeltaTime);
        camTrans.translate = Add(camTrans.translate, rotatedMoveDir);
    }
    camera_->Update();
    Matrix4x4 viewProjectionMatrix = camera_->GetViewProjectionMatrix();

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
            ringParticles_[i].uvTransform = MakeAffineMatrix({1.0f, 1.0f, 1.0f}, {0.0f, 0.0f, 0.0f}, {0.0f, uvScrollV, 0.0f});

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
            cylinderParticles_[i].uvTransform = MakeAffineMatrix({1.0f, -1.0f, 1.0f}, {0.0f, 0.0f, 0.0f}, {uvScrollU, 1.0f, 0.0f});

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
}

void GamePlayScene::Draw() {
    ID3D12GraphicsCommandList* commandList = dxCommon_->GetCommandList();

    // 1. キャラクターモデルの描画
    if (playerModel_) {
        ID3D12DescriptorHeap* modelHeaps[] = { TextureManager::GetInstance()->GetSrvHeap() };
        commandList->SetDescriptorHeaps(1, modelHeaps);

        // パイプラインが生成されているか念のためチェック
        if (graphicsPipeline_ && graphicsPipeline_->GetObject3dPipelineState() && graphicsPipeline_->GetObject3dRootSignature()) {
            commandList->SetPipelineState(graphicsPipeline_->GetObject3dPipelineState());
            commandList->SetGraphicsRootSignature(graphicsPipeline_->GetObject3dRootSignature());

            // 行列、ライト、カメラの定数バッファをセット
            if (transformResource_) {
                commandList->SetGraphicsRootConstantBufferView(1, transformResource_->GetGPUVirtualAddress());
            }
            if (directionalLightResource_) {
                commandList->SetGraphicsRootConstantBufferView(4, directionalLightResource_->GetGPUVirtualAddress());
            }
            if (cameraResource_) {
                commandList->SetGraphicsRootConstantBufferView(5, cameraResource_->GetGPUVirtualAddress());
            }

            playerModel_->DrawModel(
                commandList,
                TextureManager::GetInstance()->GetSrvHandleGPU("human/white.png"),
                TextureManager::GetInstance()->GetSrvHandleGPU("test.dds")
            );
        }
    }

    // 2. スカイボックスの描画
    if (skybox_ && graphicsPipeline_ && graphicsPipeline_->GetSkyboxPipelineState() && camera_) {
        ID3D12DescriptorHeap* skyboxHeaps[] = { TextureManager::GetInstance()->GetSrvHeap() };
        commandList->SetDescriptorHeaps(1, skyboxHeaps);
        commandList->SetPipelineState(graphicsPipeline_->GetSkyboxPipelineState());

        Matrix4x4 skyboxWorld = MakeAffineMatrix({ 1000.0f, 1000.0f, 1000.0f }, { 0.0f, 0.0f, 0.0f }, camera_->GetTransform().translate);
        Matrix4x4 skyboxWVP = Multiply(skyboxWorld, camera_->GetViewProjectionMatrix());

        skybox_->Draw(commandList, skyboxWVP, TextureManager::GetInstance()->GetSrvHandleGPU("test.dds"));
    }

    // 3. パーティクルの描画（半透明なので最後に描画）
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
                ringModel_->Draw(commandList, kRingInstanceCount, TextureManager::GetInstance()->GetSrvHandleGPU("gradationLine.png"), ringInstancingSrvHandleGPU_);
            }
            if (cylinderModel_) {
                // シリンダーはポータル風に加算合成で描画する
                cylinderModel_->Draw(commandList, kCylinderInstanceCount, TextureManager::GetInstance()->GetSrvHandleGPU("gradationLine.png"), cylinderInstancingSrvHandleGPU_);
            }
        }

        if (graphicsPipeline_->GetPipelineState(kBlendModeNormal)) {
            commandList->SetPipelineState(graphicsPipeline_->GetPipelineState(kBlendModeNormal));
            if (particleModel_) {
                particleModel_->Draw(commandList, kSpriteInstanceCount, textSrvHandleGPU_, spriteInstancingSrvHandleGPU_);
            }
        }
    }
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
        // ヒットエフェクト用のスケールランダム値
        std::uniform_real_distribution<float> distScale(0.4f, 1.5f);
        std::uniform_real_distribution<float> distRotate(-(float)M_PI, (float)M_PI);

        particle.transform.scale = { 0.1f, distScale(randomEngine_) * 2.0f, 1.0f };

        // カメラの情報を取得
        Camera::Transform& camTrans = camera_->GetTransform();

        // エフェクトの平面がカメラを向くように、カメラのXY回転を適用し、Z回転をランダムにする
        particle.transform.rotate = { camTrans.rotate.x, camTrans.rotate.y, distRotate(randomEngine_) };

        // カメラの前方向ベクトルを計算
        Matrix4x4 rotX = MakeRotateXMatrix(camTrans.rotate.x);
        Matrix4x4 rotY = MakeRotateYMatrix(camTrans.rotate.y);
        Matrix4x4 camRot = Multiply(rotX, rotY);
        Vector3 forward = TransformNormal({ 0.0f, 0.0f, 1.0f }, camRot);

        // 画面を覆いすぎないようカメラから 5.0f 前方に発生させる
        particle.transform.translate = Add(camTrans.translate, Scale(forward, 5.0f));

        particle.velocity = { 0.0f, 0.0f, 0.0f };
        particle.color = { 1.0f, 1.0f, 1.0f, 1.0f };
        // 短い時間で消滅させる
        particle.lifeTime = 0.5f;
        break;
    }
    case kTypeRing:
    {
        particle.transform.scale = { 0.1f, 0.1f, 1.0f };
        Camera::Transform& camTrans = camera_->GetTransform();
        particle.transform.rotate = { camTrans.rotate.x, camTrans.rotate.y, 0.0f };

        Matrix4x4 rotX = MakeRotateXMatrix(camTrans.rotate.x);
        Matrix4x4 rotY = MakeRotateYMatrix(camTrans.rotate.y);
        Matrix4x4 camRot = Multiply(rotX, rotY);
        Vector3 forward = TransformNormal({ 0.0f, 0.0f, 1.0f }, camRot);

        particle.transform.translate = Add(camTrans.translate, Scale(forward, 5.0f));

        particle.velocity = { 0.0f, 0.0f, 0.0f };
        particle.color = { 1.0f, 1.0f, 1.0f, 1.0f };
        particle.lifeTime = 0.5f;
        break;
    }
    case kTypeCylinder:
    {
        particle.transform.scale = { 1.0f, 1.0f, 1.0f };
        Camera::Transform& camTrans = camera_->GetTransform();
        particle.transform.rotate = { 0.0f, 0.0f, 0.0f };

        Matrix4x4 rotX = MakeRotateXMatrix(camTrans.rotate.x);
        Matrix4x4 rotY = MakeRotateYMatrix(camTrans.rotate.y);
        Matrix4x4 camRot = Multiply(rotX, rotY);
        Vector3 forward = TransformNormal({ 0.0f, 0.0f, 1.0f }, camRot);

        particle.transform.translate = Add(camTrans.translate, Scale(forward, 5.0f));

        particle.velocity = { 0.0f, 0.0f, 0.0f };
        particle.color = { 0.2f, 0.4f, 1.0f, 1.0f }; // 青っぽいポータル色
        particle.lifeTime = 2.0f; // 少し長め
        break;
    }
    }
    return particle;
}