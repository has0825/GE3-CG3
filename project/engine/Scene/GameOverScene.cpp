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

    // クリアカラーを黒に設定
    dxCommon->SetClearColor(0.0f, 0.0f, 0.0f, 1.0f);

    camera_ = std::make_unique<Camera>(WinApp::kClientWidth, WinApp::kClientHeight);
    camera_->SetTranslate({ 0.0f, 0.0f, -40.0f });

    // テクスチャロード
    TextureManager::GetInstance()->LoadTexture("GameOver/GameOver.png");
    TextureManager::GetInstance()->LoadTexture("Player2/Player_basecolor.JPEG");
    TextureManager::GetInstance()->LoadTexture("douro.jpg");
    TextureManager::GetInstance()->LoadTexture("test.dds"); // 環境マップ用

    // モデル読み込み
    gameOverModel_ = Model::LoadGLTF("Resources/GameOver/GameOver.obj", device);
    if (gameOverModel_) {
        gameOverModel_->transform.scale = { 0.4f, 0.4f, 0.4f }; // サイズを小さく
        gameOverModel_->transform.rotate = { 0.0f, 3.14159265f, 0.0f };
        gameOverModel_->transform.translate = { 0.0f, 4.0f, 20.0f }; // 少し上に配置
        gameOverModel_->SetLightingEnabled(false); // ライティングを無効にして暗いライト環境下でも明るく見せる
    }

    playerModel_ = Model::LoadGLTF("Resources/Player2/Player.obj", device);
    if (playerModel_) {
        playerModel_->transform.scale = { 6.0f, 6.0f, 6.0f };
        // 逆さま（ひっくり返って）地面に墜落している演出
        playerModel_->transform.rotate = { 0.2f, 0.5f, 3.14159265f + 0.3f };
        playerModel_->transform.translate = { 0.0f, -3.8f, 28.0f }; // 少し上に上げて床とのめり込みを防止 (Z=28)
        playerModel_->SetEnvironmentCoefficient(0.0f); // 環境反射を無効にしてライトの影を際立たせる
    }

    // 床モデル作成 (上面平面、法線を真上に設定してスポットライトを綺麗に受ける)
    ModelData floorModelData;
    floorModelData.vertices = {
        { {-30.0f, 0.0f, -30.0f, 1.0f}, {0.0f, 1.0f}, {0.0f, 1.0f, 0.0f} },
        { {-30.0f, 0.0f,  30.0f, 1.0f}, {0.0f, 0.0f}, {0.0f, 1.0f, 0.0f} },
        { { 30.0f, 0.0f, -30.0f, 1.0f}, {1.0f, 1.0f}, {0.0f, 1.0f, 0.0f} },
        { { 30.0f, 0.0f,  30.0f, 1.0f}, {1.0f, 0.0f}, {0.0f, 1.0f, 0.0f} },
    };
    floorModelData.indices = {
        0, 1, 2,
        2, 1, 3
    };
    floorModel_ = std::make_unique<Model>();
    floorModel_->Initialize(floorModelData, device);
    floorModel_->transform.scale = { 1.0f, 1.0f, 1.0f };
    floorModel_->transform.translate = { 0.0f, -5.0f, 28.0f }; // プレイヤーの足元 (Z=28)
    floorModel_->SetEnvironmentCoefficient(0.0f);

    // 定数バッファの作成
    transformResource_ = CreateBufferResource(device, sizeof(TransformationMatrix));
    transformResource_->Map(0, nullptr, reinterpret_cast<void**>(&transformData_));
    transformData_->WVP = MakeIdentity4x4();
    transformData_->World = MakeIdentity4x4();

    playerTransformResource_ = CreateBufferResource(device, sizeof(TransformationMatrix));
    playerTransformResource_->Map(0, nullptr, reinterpret_cast<void**>(&playerTransformData_));
    playerTransformData_->WVP = MakeIdentity4x4();
    playerTransformData_->World = MakeIdentity4x4();

    floorTransformResource_ = CreateBufferResource(device, sizeof(TransformationMatrix));
    floorTransformResource_->Map(0, nullptr, reinterpret_cast<void**>(&floorTransformData_));
    floorTransformData_->WVP = MakeIdentity4x4();
    floorTransformData_->World = MakeIdentity4x4();

    directionalLightResource_ = CreateBufferResource(device, sizeof(DirectionalLight));
    directionalLightResource_->Map(0, nullptr, reinterpret_cast<void**>(&directionalLightData_));
    // 文字を非常にかすかに見せるための極小の平行光源
    directionalLightData_->color = { 1.0f, 1.0f, 1.0f, 1.0f };
    directionalLightData_->direction = Normalize({ 0.0f, -1.0f, 1.0f });
    directionalLightData_->intensity = 0.05f;

    // スポットライトの設定（プレイヤーの少し上から真下に照射）
    directionalLightData_->enableSpotLight = 1;
    directionalLightData_->spotLightPos = { 0.0f, 6.0f, 28.0f }; // プレイヤーの上部 (Z=28)
    directionalLightData_->spotLightRange = 25.0f;
    directionalLightData_->spotLightDir = { 0.0f, -1.0f, 0.0f }; // 真下に向けて照射
    directionalLightData_->spotLightCosAngle = std::cos(15.0f * 3.14159265f / 180.0f); // コーン角度を15度に狭め、円形を明瞭にする
    directionalLightData_->spotLightColor = { 1.0f, 1.0f, 1.0f };
    directionalLightData_->spotLightIntensity = 8.0f; // 強度を上げてスポットライトを強調

    cameraResource_ = CreateBufferResource(device, sizeof(CameraDataCB));
    cameraResource_->Map(0, nullptr, reinterpret_cast<void**>(&cameraDataCB_));
    cameraDataCB_->worldPosition = camera_->GetTransform().translate;
}

void GameOverScene::Finalize() {
}

void GameOverScene::Update() {
    float kDeltaTime = 1.0f / 60.0f;

    // 行列更新
    Matrix4x4 worldMatrix = MakeAffineMatrix(gameOverModel_->transform.scale, gameOverModel_->transform.rotate, gameOverModel_->transform.translate);
    Matrix4x4 viewMatrix = camera_->GetViewMatrix();
    Matrix4x4 projectionMatrix = camera_->GetProjectionMatrix();
    Matrix4x4 wvpMatrix = Multiply(worldMatrix, Multiply(viewMatrix, projectionMatrix));

    transformData_->World = worldMatrix;
    transformData_->WVP = wvpMatrix;

    // プレイヤーの行列更新
    if (playerModel_ && playerTransformData_) {
        Matrix4x4 playerWorldMatrix = MakeAffineMatrix(playerModel_->transform.scale, playerModel_->transform.rotate, playerModel_->transform.translate);
        Matrix4x4 playerWvpMatrix = Multiply(playerWorldMatrix, Multiply(viewMatrix, projectionMatrix));
        playerTransformData_->World = playerWorldMatrix;
        playerTransformData_->WVP = playerWvpMatrix;
    }

    // 床の行列更新
    if (floorModel_ && floorTransformData_) {
        Matrix4x4 floorWorldMatrix = MakeAffineMatrix(floorModel_->transform.scale, floorModel_->transform.rotate, floorModel_->transform.translate);
        Matrix4x4 floorWvpMatrix = Multiply(floorWorldMatrix, Multiply(viewMatrix, projectionMatrix));
        floorTransformData_->World = floorWorldMatrix;
        floorTransformData_->WVP = floorWvpMatrix;
    }

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

        // ライト、カメラの定数バッファをセット
        if (directionalLightResource_) commandList->SetGraphicsRootConstantBufferView(4, directionalLightResource_->GetGPUVirtualAddress());
        if (cameraResource_) commandList->SetGraphicsRootConstantBufferView(5, cameraResource_->GetGPUVirtualAddress());

        // GameOverロゴを描画
        if (transformResource_) commandList->SetGraphicsRootConstantBufferView(1, transformResource_->GetGPUVirtualAddress());
        if (gameOverModel_) {
            gameOverModel_->DrawModel(
                commandList,
                TextureManager::GetInstance()->GetSrvHandleGPU("GameOver/GameOver.png"),
                TextureManager::GetInstance()->GetSrvHandleGPU("test.dds")
            );
        }

        // プレイヤーモデルを描画
        if (playerTransformResource_) commandList->SetGraphicsRootConstantBufferView(1, playerTransformResource_->GetGPUVirtualAddress());
        if (playerModel_) {
            playerModel_->DrawModel(
                commandList,
                TextureManager::GetInstance()->GetSrvHandleGPU("Player2/Player_basecolor.JPEG"),
                TextureManager::GetInstance()->GetSrvHandleGPU("test.dds")
            );
        }

        // 床モデルを描画 (床にスポットライトが当たって明るい円ができる)
        if (floorTransformResource_) commandList->SetGraphicsRootConstantBufferView(1, floorTransformResource_->GetGPUVirtualAddress());
        if (floorModel_) {
            floorModel_->DrawModel(
                commandList,
                TextureManager::GetInstance()->GetSrvHandleGPU("douro.jpg"),
                TextureManager::GetInstance()->GetSrvHandleGPU("test.dds")
            );
        }
    }
}
