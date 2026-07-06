#include "GameOverScene.h"
#include "SceneManager.h"
#include "WinApp.h"
#include "TextureManager.h"
#include "MathUtil.h"
#include "DirectXCommon.h"

void GameOverScene::Initialize() {
    // 前のシーンのリソースを即座に解放
    SceneManager::GetInstance()->ClearPreviousScene();

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
    TextureManager::GetInstance()->LoadTexture("Restart.png");
    TextureManager::GetInstance()->LoadTexture("Titlehe.png");
    TextureManager::GetInstance()->LoadTexture("white1x1.png");

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

    float baseScale = 1.2f; // 距離Z=20に合わせてサイズを調整

    // UIモデルのロード (plane.obj)
    titleheModel_ = Model::LoadGLTF("Resources/plane.obj", device);
    if (titleheModel_) {
        float w = (float)TextureManager::GetInstance()->GetMetaData("Titlehe.png").width;
        float h = (float)TextureManager::GetInstance()->GetMetaData("Titlehe.png").height;
        float aspect = 3.0f; // デフォルトアスペクト比でNaNを防ぐ
        if (h > 0.0f && w > 0.0f) {
            aspect = w / h;
        }
        titleheModel_->transform.scale = { baseScale * aspect, baseScale, 1.0f };
        titleheModel_->transform.rotate = { 0.0f, 0.0f, 0.0f }; // カリングを防ぐため回転なし (法線がカメラ側を向くようにする)
        titleheModel_->transform.translate = { 7.0f, -4.0f, 20.0f }; // 右側に配置（床Z=-2〜58の上、中央キャラクターと被らないように離す、間隔を広げ少し下げる）
        titleheModel_->SetLightingEnabled(false);
        titleheModel_->SetEnvironmentCoefficient(0.0f);
    }

    restartModel_ = Model::LoadGLTF("Resources/plane.obj", device);
    if (restartModel_) {
        float w = (float)TextureManager::GetInstance()->GetMetaData("Restart.png").width;
        float h = (float)TextureManager::GetInstance()->GetMetaData("Restart.png").height;
        float aspect = 3.0f; // デフォルトアスペクト比でNaNを防ぐ
        if (h > 0.0f && w > 0.0f) {
            aspect = w / h;
        }
        restartModel_->transform.scale = { baseScale * aspect, baseScale, 1.0f };
        restartModel_->transform.rotate = { 0.0f, 0.0f, 0.0f }; // カリングを防ぐため回転なし
        restartModel_->transform.translate = { -7.0f, -4.0f, 20.0f }; // 左側に配置（床Z=-2〜58の上、中央キャラクターと被らないように離す、間隔を広げ少し下げる）
        restartModel_->SetLightingEnabled(false);
        restartModel_->SetEnvironmentCoefficient(0.0f);
    }

    // 定数バッファの作成
    titleheTransformResource_ = CreateBufferResource(device, sizeof(TransformationMatrix));
    titleheTransformResource_->Map(0, nullptr, reinterpret_cast<void**>(&titleheTransformData_));
    titleheTransformData_->WVP = MakeIdentity4x4();
    titleheTransformData_->World = MakeIdentity4x4();

    restartTransformResource_ = CreateBufferResource(device, sizeof(TransformationMatrix));
    restartTransformResource_->Map(0, nullptr, reinterpret_cast<void**>(&restartTransformData_));
    restartTransformData_->WVP = MakeIdentity4x4();
    restartTransformData_->World = MakeIdentity4x4();

    // 白フラッシュ用スプライトの初期化
    whiteFlashSprite_ = Sprite::Create("white1x1.png", { 0.0f, 0.0f });
    if (whiteFlashSprite_) {
        whiteFlashSprite_->transform.scale = { (float)WinApp::kClientWidth, (float)WinApp::kClientHeight, 1.0f };
        whiteFlashSprite_->transform.translate = { -(float)WinApp::kClientWidth / 2.0f, -(float)WinApp::kClientHeight / 2.0f, 0.0f };
        whiteFlashSprite_->SetColor({ 1.0f, 1.0f, 1.0f, 0.0f }); // 初期値は完全に透明
    }
}

void GameOverScene::Finalize() {
}

void GameOverScene::Update() {
    float kDeltaTime = 1.0f / 60.0f;
    DirectXCommon* dxCommon = DirectXCommon::GetInstance();

    if (transitionPhase_ == TransitionPhase::kNone) {
        // 左右キーまたはA/Dキーで選択の切り替え
        if (input_->IsKeyTriggered(DIK_LEFT) || input_->IsKeyTriggered(DIK_A)) {
            selection_ = Selection::kRestart;
        }
        if (input_->IsKeyTriggered(DIK_RIGHT) || input_->IsKeyTriggered(DIK_D)) {
            selection_ = Selection::kTitlehe;
        }

        // 選択状態に応じてモデルの色（明暗）を変更
        if (selection_ == Selection::kRestart) {
            if (restartModel_) restartModel_->SetColor({ 1.0f, 1.0f, 1.0f, 1.0f });
            if (titleheModel_) titleheModel_->SetColor({ 0.3f, 0.3f, 0.3f, 0.7f });
        } else {
            if (restartModel_) restartModel_->SetColor({ 0.3f, 0.3f, 0.3f, 0.7f });
            if (titleheModel_) titleheModel_->SetColor({ 1.0f, 1.0f, 1.0f, 1.0f });
        }

        // ライトパラメータを初期状態に維持
        directionalLightData_->spotLightPos = { 0.0f, 6.0f, 28.0f };
        directionalLightData_->spotLightDir = { 0.0f, -1.0f, 0.0f };
        directionalLightData_->spotLightCosAngle = std::cos(15.0f * 3.14159265f / 180.0f);
        directionalLightData_->spotLightIntensity = 8.0f;
        directionalLightData_->intensity = 0.05f;
        dxCommon->SetClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        if (gameOverModel_) {
            gameOverModel_->SetColor({ 1.0f, 1.0f, 1.0f, 1.0f });
        }
        if (whiteFlashSprite_) {
            whiteFlashSprite_->SetColor({ 1.0f, 1.0f, 1.0f, 0.0f });
        }

        // スペースまたはエンターで決定
        if (input_->IsKeyTriggered(DIK_SPACE) || input_->IsKeyTriggered(DIK_RETURN)) {
            if (selection_ == Selection::kRestart) {
                transitionPhase_ = TransitionPhase::kToRestart;
                transitionTimer_ = 0.0f;
            } else {
                transitionPhase_ = TransitionPhase::kToTitle;
                transitionTimer_ = 0.0f;
            }
        }
    } else {
        // 遷移演出の更新
        transitionTimer_ += kDeltaTime;
        float t = (std::min)(transitionTimer_ / kTransitionTime, 1.0f);

        if (transitionPhase_ == TransitionPhase::kToTitle) {
            // スポットライトが徐々にライトを狭めて消してタイトル画面に行く演出
            // コーン角度を 15度 -> 0度へ
            float angle = 15.0f * (1.0f - t);
            directionalLightData_->spotLightCosAngle = std::cos(angle * 3.14159265f / 180.0f);
            // スポットライト強度を 8.0f -> 0.0fへ
            directionalLightData_->spotLightIntensity = 8.0f * (1.0f - t);
            // 極小の平行光源の強度も0へ
            directionalLightData_->intensity = 0.05f * (1.0f - t);

            // 文字モデルの色も徐々に暗くする
            if (gameOverModel_) {
                gameOverModel_->SetColor({ 1.0f - t, 1.0f - t, 1.0f - t, 1.0f });
            }

            if (t >= 1.0f) {
                SceneManager::GetInstance()->ChangeScene("TITLE");
            }
        } else if (transitionPhase_ == TransitionPhase::kToRestart) {
            // 全体の1.0秒のうち、前半0.5秒でスポットライトがカメラの正面に移動しつつ、カメラのほうを向く
            if (t <= 0.5f) {
                float t2 = t / 0.5f;
                Vector3 startPos = { 0.0f, 6.0f, 28.0f };
                Vector3 endPos = { 0.0f, 0.0f, -30.0f }; // カメラの手前10の距離
                directionalLightData_->spotLightPos = {
                    startPos.x + (endPos.x - startPos.x) * t2,
                    startPos.y + (endPos.y - startPos.y) * t2,
                    startPos.z + (endPos.z - startPos.z) * t2
                };

                Vector3 cameraPos = camera_->GetTransform().translate;
                Vector3 spotLightPos = directionalLightData_->spotLightPos;
                Vector3 targetDir = { cameraPos.x - spotLightPos.x, cameraPos.y - spotLightPos.y, cameraPos.z - spotLightPos.z };
                
                float len = std::sqrt(targetDir.x * targetDir.x + targetDir.y * targetDir.y + targetDir.z * targetDir.z);
                if (len > 0.0f) {
                    targetDir.x /= len;
                    targetDir.y /= len;
                    targetDir.z /= len;
                } else {
                    targetDir = { 0.0f, 0.0f, -1.0f };
                }

                Vector3 startDir = { 0.0f, -1.0f, 0.0f }; // 真下
                Vector3 currentDir = {
                    startDir.x + (targetDir.x - startDir.x) * t2,
                    startDir.y + (targetDir.y - startDir.y) * t2,
                    startDir.z + (targetDir.z - startDir.z) * t2
                };
                directionalLightData_->spotLightDir = Normalize(currentDir);

                // コーン角度を広げる 15度 -> 180度
                float angle = 15.0f + 165.0f * t2;
                directionalLightData_->spotLightCosAngle = std::cos(angle * 3.14159265f / 180.0f);

                // 強度を急増させる 8.0f -> 300.0f
                directionalLightData_->spotLightIntensity = 8.0f + 292.0f * t2;

                // 画面クリアカラーは黒のまま、フェード板は透明
                dxCommon->SetClearColor(0.0f, 0.0f, 0.0f, 1.0f);
                if (whiteFlashSprite_) {
                    whiteFlashSprite_->SetColor({ 1.0f, 1.0f, 1.0f, 0.0f });
                }
            } else {
                // 後半：スポットライトがカメラの正面に到達。そこから0.2秒で画面全体を徐々に真っ白にする
                float t3 = (std::min)((t - 0.5f) / 0.2f, 1.0f);

                // パラメータを最大状態に維持
                directionalLightData_->spotLightPos = { 0.0f, 0.0f, -30.0f };
                directionalLightData_->spotLightDir = { 0.0f, 0.0f, -1.0f };
                directionalLightData_->spotLightCosAngle = std::cos(180.0f * 3.14159265f / 180.0f);
                directionalLightData_->spotLightIntensity = 300.0f;

                // フェード板の不透明度を徐々に上げて画面全体を真っ白にする
                if (whiteFlashSprite_) {
                    whiteFlashSprite_->SetColor({ 1.0f, 1.0f, 1.0f, t3 });
                }
            }

            // 完全に真っ白（t3が1.0に達するt=0.7）になったら即座にゲームスタート（シーン遷移）
            if (t >= 0.7f) {
                SceneManager::GetInstance()->ChangeScene("GAMEPLAY");
            }
        }
    }

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

    // UIモデルの行列更新
    if (titleheModel_ && titleheTransformData_) {
        Matrix4x4 titleheWorldMatrix = MakeAffineMatrix(titleheModel_->transform.scale, titleheModel_->transform.rotate, titleheModel_->transform.translate);
        Matrix4x4 titleheWvpMatrix = Multiply(titleheWorldMatrix, Multiply(viewMatrix, projectionMatrix));
        titleheTransformData_->World = titleheWorldMatrix;
        titleheTransformData_->WVP = titleheWvpMatrix;
    }

    if (restartModel_ && restartTransformData_) {
        Matrix4x4 restartWorldMatrix = MakeAffineMatrix(restartModel_->transform.scale, restartModel_->transform.rotate, restartModel_->transform.translate);
        Matrix4x4 restartWvpMatrix = Multiply(restartWorldMatrix, Multiply(viewMatrix, projectionMatrix));
        restartTransformData_->World = restartWorldMatrix;
        restartTransformData_->WVP = restartWvpMatrix;
    }

    // 白フラッシュスプライトの更新
    if (whiteFlashSprite_) {
        whiteFlashSprite_->Update();
    }

    cameraDataCB_->worldPosition = camera_->GetTransform().translate;
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

        // ── 【修正】UIモデルを描画する直前でアルファブレンド用パイプラインステートに切り替える ──
        if (graphicsPipeline_ && graphicsPipeline_->GetObject3dBlendNormalPipelineState()) {
            commandList->SetPipelineState(graphicsPipeline_->GetObject3dBlendNormalPipelineState());
        }

        // Restart UIモデルを描画
        if (restartTransformResource_) commandList->SetGraphicsRootConstantBufferView(1, restartTransformResource_->GetGPUVirtualAddress());
        if (restartModel_) {
            restartModel_->DrawModel(
                commandList,
                TextureManager::GetInstance()->GetSrvHandleGPU("Restart.png"),
                TextureManager::GetInstance()->GetSrvHandleGPU("test.dds")
            );
        }

        // Titlehe UIモデルを描画
        if (titleheTransformResource_) commandList->SetGraphicsRootConstantBufferView(1, titleheTransformResource_->GetGPUVirtualAddress());
        if (titleheModel_) {
            titleheModel_->DrawModel(
                commandList,
                TextureManager::GetInstance()->GetSrvHandleGPU("Titlehe.png"),
                TextureManager::GetInstance()->GetSrvHandleGPU("test.dds")
            );
        }

        // 白フラッシュ用スプライトの描画 (画面全体を覆う)
        if (whiteFlashSprite_) {
            if (graphicsPipeline_ && graphicsPipeline_->GetSpriteRootSignature() && graphicsPipeline_->GetSpritePipelineState()) {
                commandList->SetGraphicsRootSignature(graphicsPipeline_->GetSpriteRootSignature());
                commandList->SetPipelineState(graphicsPipeline_->GetSpritePipelineState());

                float halfClientW = WinApp::kClientWidth / 2.0f;
                float halfClientH = WinApp::kClientHeight / 2.0f;
                Matrix4x4 projectionSprite = MakeOrthographicMatrix(-halfClientW, halfClientH, halfClientW, -halfClientH, 0.0f, 100.0f);
                Matrix4x4 viewProjSprite = Multiply(MakeIdentity4x4(), projectionSprite);
                whiteFlashSprite_->Draw(commandList, viewProjSprite);
            }
        }
    }
}
