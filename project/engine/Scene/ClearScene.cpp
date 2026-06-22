#include "ClearScene.h"
#include "SceneManager.h"
#include "WinApp.h"
#include "TextureManager.h"
#include "MathUtil.h"
#include "DirectXCommon.h"
#include <algorithm>
#include <cstdlib>

void ClearScene::Initialize() {
    input_ = Input::GetInstance();
    graphicsPipeline_ = GraphicsPipeline::GetInstance();
    DirectXCommon* dxCommon = DirectXCommon::GetInstance();
    ID3D12Device* device = dxCommon->GetDevice();

    // クリアカラーのリセット (デフォルトの青緑系に戻す)
    dxCommon->SetClearColor(0.1f, 0.25f, 0.5f, 1.0f);

    camera_ = std::make_unique<Camera>(WinApp::kClientWidth, WinApp::kClientHeight);
    camera_->SetTranslate({ 0.0f, 0.0f, -40.0f });

    // テクスチャロード
    TextureManager::GetInstance()->LoadTexture("Clear/Clear.png");
    TextureManager::GetInstance()->LoadTexture("test.dds"); // 環境マップ用
    TextureManager::GetInstance()->LoadTexture("circle2.png");
    TextureManager::GetInstance()->LoadTexture("gradationLine.png");
    TextureManager::GetInstance()->LoadTexture("text1.png");

    textureSrvHandle_ = TextureManager::GetInstance()->GetSrvHandleGPU("circle2.png");
    gradationSrvHandle_ = TextureManager::GetInstance()->GetSrvHandleGPU("gradationLine.png");
    textSrvHandle_ = TextureManager::GetInstance()->GetSrvHandleGPU("text1.png");

    // パーティクルマネージャーとモデルの初期化
    particleManager_ = std::make_unique<ParticleManager>();
    particleManager_->Initialize(device);

    particleModel_ = std::unique_ptr<Model>(Model::CreateParticleModel(device));
    ringModel_ = std::unique_ptr<Model>(Model::CreateRingModel(device));
    cylinderModel_ = std::unique_ptr<Model>(Model::CreateCylinderModel(device));

    // モデル読み込み
    clearModel_ = Model::LoadGLTF("Resources/Clear/Clear.obj", device);
    clearModel_->transform.scale = { 0.4f, 0.4f, 0.4f }; // サイズを小さく
    clearModel_->transform.rotate = { 0.0f, 3.14159265f, 0.0f };
    clearModel_->transform.translate = { 0.0f, 0.0f, 20.0f }; // 遠く、中央付近に配置

    // 定数バッファの作成
    transformResource_ = CreateBufferResource(device, sizeof(TransformationMatrix));
    transformResource_->Map(0, nullptr, reinterpret_cast<void**>(&transformData_));
    transformData_->WVP = MakeIdentity4x4();
    transformData_->World = MakeIdentity4x4();

    directionalLightResource_ = CreateBufferResource(device, sizeof(DirectionalLight));
    directionalLightResource_->Map(0, nullptr, reinterpret_cast<void**>(&directionalLightData_));
    directionalLightData_->color = { 1.0f, 1.0f, 1.0f, 1.0f };
    directionalLightData_->direction = Normalize({ 0.0f, -1.0f, 1.0f });
    directionalLightData_->intensity = 1.0f;
    directionalLightData_->enableSpotLight = 0; // スポットライト無効

    cameraResource_ = CreateBufferResource(device, sizeof(CameraDataCB));
    cameraResource_->Map(0, nullptr, reinterpret_cast<void**>(&cameraDataCB_));
    cameraDataCB_->worldPosition = camera_->GetTransform().translate;
}

void ClearScene::Finalize() {
}

void ClearScene::Update() {
    float kDeltaTime = 1.0f / 60.0f;

    // ── 打ち上げ花火の生成・更新 ──
    fireworkSpawnTimer_ -= kDeltaTime;
    if (fireworkSpawnTimer_ <= 0.0f) {
        // 0.4秒〜1.0秒に1回打ち上げる
        fireworkSpawnTimer_ = 0.4f + static_cast<float>(rand()) / RAND_MAX * 0.6f;

        ActiveFirework fw;
        // 画面下部のランダムな位置から
        fw.position = {
            -20.0f + static_cast<float>(rand()) / RAND_MAX * 40.0f,
            -15.0f,
            10.0f + static_cast<float>(rand()) / RAND_MAX * 20.0f
        };
        // 上方へのランダムな速度
        fw.velocity = {
            -3.0f + static_cast<float>(rand()) / RAND_MAX * 6.0f,
            18.0f + static_cast<float>(rand()) / RAND_MAX * 8.0f,
            -1.0f + static_cast<float>(rand()) / RAND_MAX * 2.0f
        };
        fw.timer = 0.0f;
        // 1.0秒〜1.5秒後に爆発
        fw.maxTime = 1.0f + static_cast<float>(rand()) / RAND_MAX * 0.5f;

        // ランダムな色 (RGB)
        float r = static_cast<float>(rand()) / RAND_MAX;
        float g = static_cast<float>(rand()) / RAND_MAX;
        float b = static_cast<float>(rand()) / RAND_MAX;
        // 明るさを確保するため、一番強い成分を1.0にする
        float maxVal = (std::max)({ r, g, b });
        if (maxVal > 0.0f) {
            fw.color = { r / maxVal, g / maxVal, b / maxVal };
        } else {
            fw.color = { 1.0f, 1.0f, 1.0f };
        }

        activeFireworks_.push_back(fw);
    }

    // 花火の更新
    for (auto it = activeFireworks_.begin(); it != activeFireworks_.end(); ) {
        it->position.x += it->velocity.x * kDeltaTime;
        it->position.y += it->velocity.y * kDeltaTime;
        it->position.z += it->velocity.z * kDeltaTime;
        
        // 少し重力を受けて減速
        it->velocity.y -= 9.8f * kDeltaTime;

        // 軌跡の火の粉を放出
        if (particleManager_) {
            particleManager_->EmitFireworkTrail(it->position, it->color);
        }

        it->timer += kDeltaTime;
        if (it->timer >= it->maxTime) {
            // 頂点で爆発！
            if (particleManager_) {
                particleManager_->EmitFirework(it->position, it->color);
            }
            it = activeFireworks_.erase(it);
        } else {
            ++it;
        }
    }

    // パーティクルマネージャーの更新
    if (particleManager_) {
        Matrix4x4 viewProj = Multiply(camera_->GetViewMatrix(), camera_->GetProjectionMatrix());
        particleManager_->Update(
            viewProj,
            camera_->GetBillboardMatrix(),
            kDeltaTime,
            camera_->GetTransform().translate.z,
            { 0,0,0 },
            false,
            false,
            0,
            { 0,0,0 }
        );
    }

    // 3Dモデルを正面で固定 (回転を停止)

    // 行列更新
    Matrix4x4 worldMatrix = MakeAffineMatrix(clearModel_->transform.scale, clearModel_->transform.rotate, clearModel_->transform.translate);
    Matrix4x4 viewMatrix = camera_->GetViewMatrix();
    Matrix4x4 projectionMatrix = camera_->GetProjectionMatrix();
    Matrix4x4 wvpMatrix = Multiply(worldMatrix, Multiply(viewMatrix, projectionMatrix));

    transformData_->World = worldMatrix;
    transformData_->WVP = wvpMatrix;

    cameraDataCB_->worldPosition = camera_->GetTransform().translate;

    // スペースキーでタイトルに戻る
    if (input_->IsKeyTriggered(DIK_SPACE)) {
        if (particleManager_) {
            particleManager_->Clear();
        }
        activeFireworks_.clear();
        SceneManager::GetInstance()->ChangeScene("TITLE");
    }
}

void ClearScene::Draw() {
    ID3D12GraphicsCommandList* commandList = DirectXCommon::GetInstance()->GetCommandList();

    // デスクリプタヒープのセット
    ID3D12DescriptorHeap* modelHeaps[] = { TextureManager::GetInstance()->GetSrvHeap() };
    commandList->SetDescriptorHeaps(1, modelHeaps);

    if (graphicsPipeline_ && graphicsPipeline_->GetObject3dPipelineState() && graphicsPipeline_->GetObject3dRootSignature()) {
        commandList->SetPipelineState(graphicsPipeline_->GetObject3dPipelineState());
        commandList->SetGraphicsRootSignature(graphicsPipeline_->GetObject3dRootSignature());

        // 行列、ライト、カメラの定数バッファをセット
        if (transformResource_) commandList->SetGraphicsRootConstantBufferView(1, transformResource_->GetGPUVirtualAddress());
        if (directionalLightResource_) commandList->SetGraphicsRootConstantBufferView(4, directionalLightResource_->GetGPUVirtualAddress());
        if (cameraResource_) commandList->SetGraphicsRootConstantBufferView(5, cameraResource_->GetGPUVirtualAddress());

        if (clearModel_) {
            clearModel_->DrawModel(
                commandList,
                TextureManager::GetInstance()->GetSrvHandleGPU("Clear/Clear.png"),
                TextureManager::GetInstance()->GetSrvHandleGPU("test.dds")
            );
        }

        // ── パーティクルの描画 ──
        if (particleManager_ && graphicsPipeline_->GetRootSignature()) {
            commandList->SetGraphicsRootSignature(graphicsPipeline_->GetRootSignature());
            commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

            BlendMode blendMode = kBlendModeAdd;
            if (graphicsPipeline_->GetPipelineState(blendMode)) {
                commandList->SetPipelineState(graphicsPipeline_->GetPipelineState(blendMode));
                
                particleManager_->Draw(
                    commandList,
                    particleModel_.get(),
                    ringModel_.get(),
                    cylinderModel_.get(),
                    textureSrvHandle_,
                    gradationSrvHandle_,
                    textSrvHandle_
                );
            }
        }
    }
}
