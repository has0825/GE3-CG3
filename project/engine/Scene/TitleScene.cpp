#include "TitleScene.h"
#include "SceneManager.h"
#include "WinApp.h"
#include "TextureManager.h"
#include "MathUtil.h"
#include "DirectXCommon.h"

void TitleScene::Initialize() {
    // 前のシーンのリソースを即座に解放
    SceneManager::GetInstance()->ClearPreviousScene();

    inputDelay_ = 24; // 起動時等の誤トリガーを防ぐため、開始時はディレイを設ける

    input_ = Input::GetInstance();
    graphicsPipeline_ = GraphicsPipeline::GetInstance();
    DirectXCommon* dxCommon = DirectXCommon::GetInstance();
    ID3D12Device* device = dxCommon->GetDevice();

    // TextureManagerの初期化 (起動時に最初のシーンとなるため必須)
    TextureManager::GetInstance()->Initialize(device, "Resources/");

    camera_ = std::make_unique<Camera>(WinApp::kClientWidth, WinApp::kClientHeight);
    camera_->SetTranslate({ 0.0f, 0.0f, -40.0f });

    // テクスチャロード
    TextureManager::GetInstance()->LoadTexture("Title/white1x1.png");
    TextureManager::GetInstance()->LoadTexture("test.dds"); // 環境マップ用
    TextureManager::GetInstance()->LoadTexture("cobblestone_street_night_2k.dds"); // 背景用

    // モデル読み込み
    titleModel_ = Model::LoadGLTF("Resources/Title/Title.obj", device);
    titleModel_->transform.scale = { 1.2f, 1.2f, 1.2f }; // サイズを大きく
    titleModel_->transform.rotate = { 0.0f, 0.0f, 0.0f }; // 反転を直すための回転を解除
    titleModel_->transform.translate = { 0.0f, 0.0f, 20.0f }; // 遠く、中央付近に配置

    // Skybox初期化
    skybox_ = std::make_unique<Skybox>();
    skybox_->Initialize(device);

    // 定数バッファの作成
    transformResource_ = CreateBufferResource(device, sizeof(TransformationMatrix));
    transformResource_->Map(0, nullptr, reinterpret_cast<void**>(&transformData_));
    transformData_->WVP = MakeIdentity4x4();
    transformData_->World = MakeIdentity4x4();

    directionalLightResource_ = CreateBufferResource(device, sizeof(DirectionalLight));
    directionalLightResource_->Map(0, nullptr, reinterpret_cast<void**>(&directionalLightData_));
    directionalLightData_->color = { 1.0f, 1.0f, 1.0f, 1.0f };
    directionalLightData_->direction = Normalize({ 0.0f, -1.0f, 1.0f });
    directionalLightData_->intensity = 1.8f; // タイトル文字がはっきり見えるように強度を上げる

    cameraResource_ = CreateBufferResource(device, sizeof(CameraDataCB));
    cameraResource_->Map(0, nullptr, reinterpret_cast<void**>(&cameraDataCB_));
    cameraDataCB_->worldPosition = camera_->GetTransform().translate;
}

void TitleScene::Finalize() {
}

void TitleScene::Update() {
    // 3Dモデルは正面で固定 (回転はなし)

    // 行列更新
    Matrix4x4 worldMatrix = MakeAffineMatrix(titleModel_->transform.scale, titleModel_->transform.rotate, titleModel_->transform.translate);
    Matrix4x4 viewMatrix = camera_->GetViewMatrix();
    Matrix4x4 projectionMatrix = camera_->GetProjectionMatrix();
    Matrix4x4 wvpMatrix = Multiply(worldMatrix, Multiply(viewMatrix, projectionMatrix));

    transformData_->World = worldMatrix;
    transformData_->WVP = wvpMatrix;

    cameraDataCB_->worldPosition = camera_->GetTransform().translate;

    if (inputDelay_ > 0) {
        inputDelay_--;
    } else {
        // スペースキーでゲームプレイシーンへ遷移
        if (input_->IsKeyTriggered(DIK_SPACE)) {
            SceneManager::GetInstance()->ChangeScene("GAMEPLAY");
        }
    }
}

void TitleScene::Draw() {
    ID3D12GraphicsCommandList* commandList = DirectXCommon::GetInstance()->GetCommandList();

    // デスクリプタヒープのセット
    ID3D12DescriptorHeap* modelHeaps[] = { TextureManager::GetInstance()->GetSrvHeap() };
    commandList->SetDescriptorHeaps(1, modelHeaps);

    // Skyboxの描画
    if (skybox_) {
        if (graphicsPipeline_ && graphicsPipeline_->GetSkyboxPipelineState() && graphicsPipeline_->GetRootSignature()) {
            commandList->SetPipelineState(graphicsPipeline_->GetSkyboxPipelineState());
            commandList->SetGraphicsRootSignature(graphicsPipeline_->GetRootSignature());

            // カメラのビュー行列を取得し、平行移動成分をゼロにする
            Matrix4x4 viewMatrix = camera_->GetViewMatrix();
            viewMatrix.m[3][0] = 0.0f;
            viewMatrix.m[3][1] = 0.0f;
            viewMatrix.m[3][2] = 0.0f;

            Matrix4x4 projectionMatrix = camera_->GetProjectionMatrix();
            Matrix4x4 wvpMatrix = Multiply(viewMatrix, projectionMatrix);

            std::string skyboxTexName = "cobblestone_street_night_2k.dds";
            // 安全対策: 指定されたテクスチャがキューブマップでない場合は、有効なキューブマップにフォールバックする
            if (!TextureManager::GetInstance()->GetMetaData(skyboxTexName).IsCubemap()) {
                skyboxTexName = "test.dds";
            }
            D3D12_GPU_DESCRIPTOR_HANDLE skyboxSrvHandle = TextureManager::GetInstance()->GetSrvHandleGPU(skyboxTexName);

            skybox_->Draw(commandList, wvpMatrix, skyboxSrvHandle);
        }
    }

    if (graphicsPipeline_ && graphicsPipeline_->GetObject3dPipelineState() && graphicsPipeline_->GetObject3dRootSignature()) {
        commandList->SetPipelineState(graphicsPipeline_->GetObject3dPipelineState());
        commandList->SetGraphicsRootSignature(graphicsPipeline_->GetObject3dRootSignature());

        // 行列、ライト、カメラの定数バッファをセット
        if (transformResource_) commandList->SetGraphicsRootConstantBufferView(1, transformResource_->GetGPUVirtualAddress());
        if (directionalLightResource_) commandList->SetGraphicsRootConstantBufferView(4, directionalLightResource_->GetGPUVirtualAddress());
        if (cameraResource_) commandList->SetGraphicsRootConstantBufferView(5, cameraResource_->GetGPUVirtualAddress());

        if (titleModel_) {
            titleModel_->DrawModel(
                commandList,
                TextureManager::GetInstance()->GetSrvHandleGPU("Title/white1x1.png"),
                TextureManager::GetInstance()->GetSrvHandleGPU("test.dds")
            );
        }
    }
}