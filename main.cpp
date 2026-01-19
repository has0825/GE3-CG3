#include <Windows.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <cassert>
#include <string>
#include <vector>

#include "WinApp.h"
#include "DirectXCommon.h"
#include "D3D12Util.h"
#include "Model.h"         // DataTypes.hが含まれるため構造体定義は不要
#include "ParticleManager.h"
#include "GraphicsPipeline.h"
#include "MathUtil.h"      // ★MathUtilを使用

#include "externals/imgui/imgui.h"
#include "externals/imgui/imgui_impl_dx12.h"
#include "externals/imgui/imgui_impl_win32.h"

#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxgi.lib")

// --- 文字列変換ヘルパー ---
std::wstring ConvertString(const std::string& str) {
    if (str.empty()) return std::wstring();
    int size_needed = MultiByteToWideChar(CP_UTF8, 0, &str[0], (int)str.size(), NULL, 0);
    std::wstring wstrTo(size_needed, 0);
    MultiByteToWideChar(CP_UTF8, 0, &str[0], (int)str.size(), &wstrTo[0], size_needed);
    return wstrTo;
}

std::string ConvertString(const std::wstring& str) {
    if (str.empty()) return std::string();
    int size_needed = WideCharToMultiByte(CP_UTF8, 0, &str[0], (int)str.size(), NULL, 0, NULL, NULL);
    std::string strTo(size_needed, 0);
    WideCharToMultiByte(CP_UTF8, 0, &str[0], (int)str.size(), &strTo[0], size_needed, NULL, NULL);
    return strTo;
}

// --- Windowsアプリのエントリーポイント ---
int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {

    // 1. COMライブラリの初期化
    HRESULT hr = CoInitializeEx(0, COINIT_MULTITHREADED);
    assert(SUCCEEDED(hr));

    // WinApp初期化
    WinApp* winApp = WinApp::GetInstance();
    winApp->Initialize(L"CG2 Class - Phong / Blinn-Phong & Non-Uniform Scale");

    // DirectXCommon初期化
    DirectXCommon* dxCommon = DirectXCommon::GetInstance();
    dxCommon->Initialize(winApp);

    // デスクリプタヒープの作成
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> srvDescriptorHeap =
        CreateDescriptorHeap(dxCommon->GetDevice(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 128, true);

    // パイプライン初期化
    GraphicsPipeline* graphicsPipeline = new GraphicsPipeline();
    graphicsPipeline->Initialize(dxCommon->GetDevice());

    // パーティクルマネージャ初期化
    ParticleManager* particleManager = new ParticleManager();
    particleManager->Initialize(dxCommon->GetDevice());

    // --- モデル作成 ---
    // Resources/ball/ball.obj を想定
    Model* sphereModel = Model::Create("Resources/ball", "ball.obj", dxCommon->GetDevice());

    // 安全装置: モデル読み込み失敗時は終了
    if (!sphereModel) {
        MessageBoxA(NULL, "Model Loading Failed! Check 'Resources/ball/ball.obj'", "Error", MB_OK | MB_ICONERROR);
        return -1;
    }

    Model* particleModel = Model::CreateParticleModel(dxCommon->GetDevice());

    // --- テクスチャ読み込み ---
    DirectX::ScratchImage monsterBallImage = LoadTexture("Resources/monsterBall.png");
    const DirectX::TexMetadata& metadata = monsterBallImage.GetMetadata();

    if (metadata.width == 0) {
        MessageBoxA(NULL, "Texture loading failed! Check 'Resources/monsterBall.png'", "Error", MB_OK | MB_ICONERROR);
        return -1;
    }

    Microsoft::WRL::ComPtr<ID3D12Resource> textureResource = CreateTextureResource(dxCommon->GetDevice(), metadata);
    Microsoft::WRL::ComPtr<ID3D12Resource> intermediateResource = UploadTextureData(textureResource.Get(), monsterBallImage, dxCommon->GetDevice(), dxCommon->GetCommandList());

    // SRV作成
    uint32_t descriptorSize = dxCommon->GetDevice()->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    D3D12_CPU_DESCRIPTOR_HANDLE textureSrvHandleCPU = GetCPUDescriptorHandle(srvDescriptorHeap.Get(), descriptorSize, 0);
    D3D12_GPU_DESCRIPTOR_HANDLE textureSrvHandleGPU = GetGPUDescriptorHandle(srvDescriptorHeap.Get(), descriptorSize, 0);

    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
    srvDesc.Format = metadata.format;
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MipLevels = UINT(metadata.mipLevels);
    dxCommon->GetDevice()->CreateShaderResourceView(textureResource.Get(), &srvDesc, textureSrvHandleCPU);

    // --- ImGui初期化 ---
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::StyleColorsDark();
    ImGui_ImplWin32_Init(winApp->GetHwnd());
    ImGui_ImplDX12_Init(
        dxCommon->GetDevice(),
        dxCommon->GetBackBufferCount(),
        dxCommon->GetRtvDesc().Format,
        srvDescriptorHeap.Get(),
        GetCPUDescriptorHandle(srvDescriptorHeap.Get(), descriptorSize, 1),
        GetGPUDescriptorHandle(srvDescriptorHeap.Get(), descriptorSize, 1)
    );

    // --- 各種バッファ初期値 ---
    Material materialData;
    materialData.color = { 1.0f, 1.0f, 1.0f, 1.0f };
    materialData.enableLighting = 2; // 初期値: Phong
    materialData.uvTransform = MakeIdentity4x4();
    materialData.shininess = 50.0f;  // 初期光沢度

    DirectionalLight lightData;
    lightData.color = { 1.0f, 1.0f, 1.0f, 1.0f };
    lightData.direction = { 0.0f, -1.0f, 0.0f };
    lightData.intensity = 1.0f;

    // カメラ用バッファ
    Microsoft::WRL::ComPtr<ID3D12Resource> cameraBuffer = CreateBufferResource(dxCommon->GetDevice(), sizeof(CameraForGpu));
    CameraForGpu* cameraMap = nullptr;
    cameraBuffer->Map(0, nullptr, reinterpret_cast<void**>(&cameraMap));
    cameraMap->worldPosition = { 0.0f, 0.0f, -10.0f };

    // トランスフォーム初期値
    Transform sphereTransform = { {1.0f, 1.0f, 1.0f}, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f} };
    Vector3 cameraTranslate = { 0.0f, 0.0f, -10.0f };
    Vector3 cameraRotate = { 0.0f, 0.0f, 0.0f };

    // インスタンシング用バッファ（球体用）
    Microsoft::WRL::ComPtr<ID3D12Resource> sphereInstBuffer = CreateBufferResource(dxCommon->GetDevice(), sizeof(TransformationMatrix));
    TransformationMatrix* sphereMapBuffer = nullptr;
    sphereInstBuffer->Map(0, nullptr, reinterpret_cast<void**>(&sphereMapBuffer));

    // インスタンシング用SRV作成 (register(t1) に対応)
    D3D12_CPU_DESCRIPTOR_HANDLE sphereSrvHandleCPU = GetCPUDescriptorHandle(srvDescriptorHeap.Get(), descriptorSize, 2);
    D3D12_GPU_DESCRIPTOR_HANDLE sphereSrvHandleGPU = GetGPUDescriptorHandle(srvDescriptorHeap.Get(), descriptorSize, 2);
    D3D12_SHADER_RESOURCE_VIEW_DESC instSrvDesc{};
    instSrvDesc.Format = DXGI_FORMAT_UNKNOWN;
    instSrvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    instSrvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
    instSrvDesc.Buffer.NumElements = 1;
    instSrvDesc.Buffer.StructureByteStride = sizeof(TransformationMatrix);
    dxCommon->GetDevice()->CreateShaderResourceView(sphereInstBuffer.Get(), &instSrvDesc, sphereSrvHandleCPU);

    // --- メインループ ---
    while (true) {
        if (winApp->ProcessMessage()) break;

        // ImGuiフレーム開始
        ImGui_ImplDX12_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();

        ImGui::Begin("Settings");
        if (ImGui::CollapsingHeader("Sphere Transform")) {
            ImGui::DragFloat3("Scale", &sphereTransform.scale.x, 0.01f);
            ImGui::DragFloat3("Rotate", &sphereTransform.rotate.x, 0.01f);
            ImGui::DragFloat3("Translate", &sphereTransform.translate.x, 0.01f);
        }
        if (ImGui::CollapsingHeader("Lighting")) {
            const char* items[] = { "None", "Lambert", "Phong", "Blinn-Phong" };
            ImGui::Combo("Model", &materialData.enableLighting, items, IM_ARRAYSIZE(items));
            ImGui::DragFloat("Shininess", &materialData.shininess, 0.1f, 1.0f, 256.0f); // shininess
            ImGui::ColorEdit3("Light Color", &lightData.color.x);
            ImGui::DragFloat3("Light Dir", &lightData.direction.x, 0.01f);
            lightData.direction = Normalize(lightData.direction);
        }
        ImGui::End();

        // 行列計算
        Matrix4x4 cameraMatrix = MakeAffineMatrix({ 1,1,1 }, cameraRotate, cameraTranslate);
        Matrix4x4 viewMatrix = Inverse(cameraMatrix);
        Matrix4x4 projectionMatrix = MakePerspectiveFovMatrix(0.45f, (float)WinApp::kClientWidth / WinApp::kClientHeight, 0.1f, 100.0f);
        Matrix4x4 viewProjectionMatrix = Multiply(viewMatrix, projectionMatrix);

        // カメラ座標更新 (Shaderの b2 レジスタ用)
        cameraMap->worldPosition = { cameraMatrix.m[3][0], cameraMatrix.m[3][1], cameraMatrix.m[3][2] };

        // 球体行列更新
        if (sphereModel) {
            sphereModel->transform = sphereTransform;
            Matrix4x4 sphereWorldMatrix = MakeAffineMatrix(sphereTransform.scale, sphereTransform.rotate, sphereTransform.translate);

            // ★非均一スケール対応: 法線用行列 (Worldの逆行列の転置行列)
            Matrix4x4 worldInverse = Inverse(sphereWorldMatrix);
            Matrix4x4 worldInverseTranspose = Transpose(worldInverse);

            // バッファに書き込み
            sphereMapBuffer->WVP = Multiply(sphereWorldMatrix, viewProjectionMatrix);
            sphereMapBuffer->World = sphereWorldMatrix;
            sphereMapBuffer->WorldInverseTranspose = worldInverseTranspose; // ★追加メンバへ代入

            // マテリアル更新
            if (sphereModel->materialData) {
                sphereModel->materialData->color = materialData.color;
                sphereModel->materialData->enableLighting = materialData.enableLighting;
                sphereModel->materialData->shininess = materialData.shininess; // ★追加メンバへ代入
                sphereModel->materialData->uvTransform = materialData.uvTransform;
            }
            sphereModel->Update();
        }

        // ライト更新
        static Microsoft::WRL::ComPtr<ID3D12Resource> lightBuffer = CreateBufferResource(dxCommon->GetDevice(), sizeof(DirectionalLight));
        DirectionalLight* lightMap = nullptr;
        lightBuffer->Map(0, nullptr, reinterpret_cast<void**>(&lightMap));
        *lightMap = lightData;
        lightBuffer->Unmap(0, nullptr);

        particleManager->Update(viewProjectionMatrix);

        // 描画開始
        dxCommon->PreDraw();
        ID3D12GraphicsCommandList* commandList = dxCommon->GetCommandList();

        commandList->SetGraphicsRootSignature(graphicsPipeline->GetRootSignature());
        ID3D12DescriptorHeap* descriptorHeaps[] = { srvDescriptorHeap.Get() };
        commandList->SetDescriptorHeaps(1, descriptorHeaps);
        commandList->SetPipelineState(graphicsPipeline->GetPipelineState(kBlendModeNormal));
        commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

        // 定数バッファセット (b1: Light, b2: Camera)
        commandList->SetGraphicsRootConstantBufferView(1, lightBuffer->GetGPUVirtualAddress());
        commandList->SetGraphicsRootConstantBufferView(2, cameraBuffer->GetGPUVirtualAddress());

        // 球体描画
        // t0: Texture, t1: InstancingData (sphereSrvHandleGPU)
        if (sphereModel) {
            sphereModel->Draw(commandList, 1, textureSrvHandleGPU, sphereSrvHandleGPU);
        }

        // パーティクル描画
        particleModel->Draw(commandList, kNumMaxInstance, textureSrvHandleGPU, particleManager->GetSRVHandleGPU());

        // ImGui描画
        ImGui::Render();
        ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), commandList);

        dxCommon->PostDraw();
    }

    ImGui_ImplDX12_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();

    // 解放
    delete sphereModel;
    delete particleModel;
    delete particleManager;
    delete graphicsPipeline;

    dxCommon->Finalize();
    winApp->Finalize();

    CoUninitialize();

    return 0;
}