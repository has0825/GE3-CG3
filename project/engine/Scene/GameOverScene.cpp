#include "GameOverScene.h"
#include "SceneManager.h"
#include "WinApp.h"
#include "TextureManager.h"
#include "MathUtil.h"
#include "DirectXCommon.h"

void GameOverScene::Initialize() {
    input_ = Input::GetInstance();
    graphicsPipeline_ = GraphicsPipeline::GetInstance();
    DirectXCommon* dxCommon = DirectXCommon::GetInstance();
    ID3D12Device* device = dxCommon->GetDevice();

    camera_ = std::make_unique<Camera>(WinApp::kClientWidth, WinApp::kClientHeight);
    camera_->SetTranslate({ 0.0f, 0.0f, -40.0f });

    // テクスチャロード
    TextureManager::GetInstance()->LoadTexture("GameOver/GameOver.png");
    TextureManager::GetInstance()->LoadTexture("test.dds"); // 環境マップ用

    // モデル読み込み
    gameOverModel_ = Model::LoadGLTF("Resources/GameOver/GameOver.obj", device);
    gameOverModel_->transform.scale = { 0.4f, 0.4f, 0.4f }; // サイズを小さく
    gameOverModel_->transform.rotate = { 0.0f, 3.14159265f, 0.0f };
    gameOverModel_->transform.translate = { 0.0f, 0.0f, 20.0f }; // 遠く、中央付近に配置

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

    cameraResource_ = CreateBufferResource(device, sizeof(CameraDataCB));
    cameraResource_->Map(0, nullptr, reinterpret_cast<void**>(&cameraDataCB_));
    cameraDataCB_->worldPosition = camera_->GetTransform().translate;
}

void GameOverScene::Finalize() {
}

void GameOverScene::Update() {
    float kDeltaTime = 1.0f / 60.0f;

    // 3Dモデルを正面で固定 (回転を停止)

    // 行列更新
    Matrix4x4 worldMatrix = MakeAffineMatrix(gameOverModel_->transform.scale, gameOverModel_->transform.rotate, gameOverModel_->transform.translate);
    Matrix4x4 viewMatrix = camera_->GetViewMatrix();
    Matrix4x4 projectionMatrix = camera_->GetProjectionMatrix();
    Matrix4x4 wvpMatrix = Multiply(worldMatrix, Multiply(viewMatrix, projectionMatrix));

    transformData_->World = worldMatrix;
    transformData_->WVP = wvpMatrix;

    cameraDataCB_->worldPosition = camera_->GetTransform().translate;

    // スペースキーでタイトルに戻る
    if (input_->IsKeyTriggered(DIK_SPACE)) {
        SceneManager::GetInstance()->ChangeScene("TITLE");
    }
}

void GameOverScene::Draw() {
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

        if (gameOverModel_) {
            gameOverModel_->DrawModel(
                commandList,
                TextureManager::GetInstance()->GetSrvHandleGPU("GameOver/GameOver.png"),
                TextureManager::GetInstance()->GetSrvHandleGPU("test.dds")
            );
        }
    }
}
