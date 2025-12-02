#define NOMINMAX
#include <Windows.h>
#include <d3d12.h>
#include <string>

#include "WinApp.h"
#include "DirectXCommon.h"
#include "GraphicsPipeline.h"
#include "Input.h"
#include "TextureManager.h"
#include "ModelManager.h"
#include "ParticleManager.h" // ParticleManager内で形状生成するのでModelやSpriteは不要
#include "Camera.h"
#include "MathUtil.h"
#include "D3D12Util.h" 

#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "dxguid.lib")
#pragma comment(lib, "dxcompiler.lib")

int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {
    WinApp* winApp = WinApp::GetInstance();
    winApp->Initialize();
    DirectXCommon* dxCommon = DirectXCommon::GetInstance();
    dxCommon->Initialize(winApp);
    Input* input = Input::GetInstance();
    input->Initialize(winApp);

    ID3D12Device* device = dxCommon->GetDevice();
    ID3D12GraphicsCommandList* commandList = dxCommon->GetCommandList();

    GraphicsPipeline* graphicsPipeline = new GraphicsPipeline();
    graphicsPipeline->Initialize(device);

    TextureManager::GetInstance()->Initialize(device);

    // パーティクルマネージャ初期化
    // ParticleManager内で四角形の頂点データを作成するため、外部モデル(plane.obj)は不要になりました
    ParticleManager* particleManager = new ParticleManager();
    particleManager->Initialize(device, "Resources/circle.png");

    Camera* camera = new Camera(WinApp::kClientWidth, WinApp::kClientHeight);
    camera->SetTranslate({ 0.0f, 2.0f, -10.0f });

    ParticleType currentEffect = ParticleType::kExplosion;

    while (!winApp->IsEndRequested()) {
        winApp->ProcessMessage();
        input->Update();

        if (input->IsKeyTriggered(DIK_ESCAPE)) break;

        // カメラ操作
        Transform& camTrans = camera->GetTransform();
        if (input->IsKeyPressed(DIK_UP))    camTrans.translate.y += 0.1f;
        if (input->IsKeyPressed(DIK_DOWN))  camTrans.translate.y -= 0.1f;
        if (input->IsKeyPressed(DIK_RIGHT)) camTrans.translate.x += 0.1f;
        if (input->IsKeyPressed(DIK_LEFT))  camTrans.translate.x -= 0.1f;
        if (input->IsKeyPressed(DIK_W))     camTrans.translate.z += 0.1f;
        if (input->IsKeyPressed(DIK_S))     camTrans.translate.z -= 0.1f;
        camera->Update();

        // エフェクト切替
        if (input->IsKeyTriggered(DIK_1)) currentEffect = ParticleType::kExplosion;
        if (input->IsKeyTriggered(DIK_2)) currentEffect = ParticleType::kFountain;
        if (input->IsKeyTriggered(DIK_3)) currentEffect = ParticleType::kSpiral;
        if (input->IsKeyTriggered(DIK_4)) currentEffect = ParticleType::kRain;

        // ★スペースキーを押している間、パーティクル発生
        if (input->IsKeyPressed(DIK_SPACE)) {
            particleManager->Emit(currentEffect, { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f });
        }

        particleManager->Update(camera->GetViewProjectionMatrix());

        // 描画
        dxCommon->PreDraw();

        commandList->SetGraphicsRootSignature(graphicsPipeline->GetRootSignature());
        commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

        // 加算合成で描画
        commandList->SetPipelineState(graphicsPipeline->GetPipelineState(kBlendModeAdd));

        // パーティクル描画 (頂点セット、SRVセット、ドローコール全てParticleManager内で行う)
        particleManager->Draw(commandList);

        dxCommon->PostDraw();
    }

    delete particleManager;
    delete camera;
    delete graphicsPipeline;

    dxCommon->Finalize();
    winApp->Finalize();

    return 0;
}