#include <Windows.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <cassert>
#include <string>
#include <vector>
#include <format>
#include <numbers> // PI用

#include "WinApp.h"
#include "DirectXCommon.h"
#include "D3D12Util.h"
#include "Model.h"
#include "GraphicsPipeline.h"
#include "MathUtil.h"
#include "DataTypes.h" 

#include "externals/imgui/imgui.h"
#include "externals/imgui/imgui_impl_dx12.h"
#include "externals/imgui/imgui_impl_win32.h"

#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxgi.lib")

// 文字列変換
std::wstring ConvertString(const std::string& str) {
    if (str.empty()) return std::wstring();
    int size_needed = MultiByteToWideChar(CP_UTF8, 0, &str[0], (int)str.size(), NULL, 0);
    std::wstring wstrTo(size_needed, 0);
    MultiByteToWideChar(CP_UTF8, 0, &str[0], (int)str.size(), &wstrTo[0], size_needed);
    return wstrTo;
}

int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {
    WinApp* winApp = WinApp::GetInstance();
    winApp->Initialize(L"CG2");

    DirectXCommon* dxCommon = DirectXCommon::GetInstance();
    dxCommon->Initialize(winApp);
    ID3D12Device* device = dxCommon->GetDevice();
    ID3D12GraphicsCommandList* commandList = dxCommon->GetCommandList();

    // パイプライン
    GraphicsPipeline* graphicsPipeline = new GraphicsPipeline();
    graphicsPipeline->Initialize(device);

    // SRV Heap
    const UINT kMaxSRVCount = 128;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> srvDescriptorHeap =
        CreateDescriptorHeap(device, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, kMaxSRVCount, true);

    D3D12_CPU_DESCRIPTOR_HANDLE srvHandleCPU = srvDescriptorHeap->GetCPUDescriptorHandleForHeapStart();
    D3D12_GPU_DESCRIPTOR_HANDLE srvHandleGPU = srvDescriptorHeap->GetGPUDescriptorHandleForHeapStart();
    UINT descriptorSize = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    // ---------------------------------------------------------
    // リソース読み込み
    // ---------------------------------------------------------

    // A. Terrain Texture (Index 0)
    std::string textureFileTerrain = "Resources/terrain/grass.png";
    DirectX::ScratchImage imgTerrain = LoadTexture(textureFileTerrain);
    const DirectX::TexMetadata& metaTerrain = imgTerrain.GetMetadata();
    Microsoft::WRL::ComPtr<ID3D12Resource> texResTerrain = CreateTextureResource(device, metaTerrain);
    Microsoft::WRL::ComPtr<ID3D12Resource> uploadResTerrain = UploadTextureData(texResTerrain.Get(), imgTerrain, device, commandList);

    D3D12_CPU_DESCRIPTOR_HANDLE terrainSrvCPU = srvHandleCPU;
    D3D12_GPU_DESCRIPTOR_HANDLE terrainSrvGPU = srvHandleGPU;
    D3D12_SHADER_RESOURCE_VIEW_DESC srvDescTerrain{};
    srvDescTerrain.Format = metaTerrain.format;
    srvDescTerrain.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDescTerrain.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDescTerrain.Texture2D.MipLevels = UINT(metaTerrain.mipLevels);
    device->CreateShaderResourceView(texResTerrain.Get(), &srvDescTerrain, terrainSrvCPU);

    srvHandleCPU.ptr += descriptorSize;
    srvHandleGPU.ptr += descriptorSize;

    // B. Ball Texture (Index 1)
    std::string textureFileBall = "Resources/monsterBall.png";
    DirectX::ScratchImage imgBall = LoadTexture(textureFileBall);
    const DirectX::TexMetadata& metaBall = imgBall.GetMetadata();
    Microsoft::WRL::ComPtr<ID3D12Resource> texResBall = CreateTextureResource(device, metaBall);
    Microsoft::WRL::ComPtr<ID3D12Resource> uploadResBall = UploadTextureData(texResBall.Get(), imgBall, device, commandList);

    D3D12_CPU_DESCRIPTOR_HANDLE ballSrvCPU = srvHandleCPU;
    D3D12_GPU_DESCRIPTOR_HANDLE ballSrvGPU = srvHandleGPU;
    D3D12_SHADER_RESOURCE_VIEW_DESC srvDescBall{};
    srvDescBall.Format = metaBall.format;
    srvDescBall.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDescBall.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDescBall.Texture2D.MipLevels = UINT(metaBall.mipLevels);
    device->CreateShaderResourceView(texResBall.Get(), &srvDescBall, ballSrvCPU);

    srvHandleCPU.ptr += descriptorSize;
    srvHandleGPU.ptr += descriptorSize;

    // C. Model
    Model* terrainModel = Model::Create("Resources/terrain", "terrain.obj", device);
    Model* sphereModel = Model::CreateSphereModel(device, 16);

    // D. Transform Buffers
    // Terrain (Index 2)
    Microsoft::WRL::ComPtr<ID3D12Resource> terrainTransformBuffer = CreateBufferResource(device, sizeof(TransformationMatrix));
    TransformationMatrix* terrainMapData = nullptr;
    terrainTransformBuffer->Map(0, nullptr, reinterpret_cast<void**>(&terrainMapData));

    D3D12_SHADER_RESOURCE_VIEW_DESC srvDescTransform{};
    srvDescTransform.Format = DXGI_FORMAT_UNKNOWN;
    srvDescTransform.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDescTransform.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
    srvDescTransform.Buffer.FirstElement = 0;
    srvDescTransform.Buffer.NumElements = 1;
    srvDescTransform.Buffer.StructureByteStride = sizeof(TransformationMatrix);
    srvDescTransform.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;

    D3D12_CPU_DESCRIPTOR_HANDLE terrainTransCPU = srvHandleCPU;
    D3D12_GPU_DESCRIPTOR_HANDLE terrainTransGPU = srvHandleGPU;
    device->CreateShaderResourceView(terrainTransformBuffer.Get(), &srvDescTransform, terrainTransCPU);

    srvHandleCPU.ptr += descriptorSize;
    srvHandleGPU.ptr += descriptorSize;

    // Sphere (Index 3)
    Microsoft::WRL::ComPtr<ID3D12Resource> sphereTransformBuffer = CreateBufferResource(device, sizeof(TransformationMatrix));
    TransformationMatrix* sphereMapData = nullptr;
    sphereTransformBuffer->Map(0, nullptr, reinterpret_cast<void**>(&sphereMapData));

    D3D12_CPU_DESCRIPTOR_HANDLE sphereTransCPU = srvHandleCPU;
    D3D12_GPU_DESCRIPTOR_HANDLE sphereTransGPU = srvHandleGPU;
    device->CreateShaderResourceView(sphereTransformBuffer.Get(), &srvDescTransform, sphereTransCPU);

    srvHandleCPU.ptr += descriptorSize;
    srvHandleGPU.ptr += descriptorSize;

    // E. Light Buffer (DirectionalLight から LightGroup に変更)
    // ---------------------------------------------------------
    // シェーダー側で struct LightGroup を受け取るように変更したため、サイズを合わせる
    Microsoft::WRL::ComPtr<ID3D12Resource> lightBuffer = CreateBufferResource(device, sizeof(LightGroup));
    LightGroup* lightData = nullptr;
    lightBuffer->Map(0, nullptr, reinterpret_cast<void**>(&lightData));

    // ライト初期値設定
    lightData->numDirectionalLights = 1;
    lightData->directionalLights[0].color = { 1.0f, 1.0f, 1.0f, 1.0f };
    lightData->directionalLights[0].direction = { 0.0f, -1.0f, 0.0f };
    lightData->directionalLights[0].intensity = 1.0f;
    lightData->numPointLights = 0;
    lightData->numSpotLights = 0;
    lightData->numAreaLights = 0;

    // F. Camera Buffer
    Microsoft::WRL::ComPtr<ID3D12Resource> cameraBuffer = CreateBufferResource(device, sizeof(CameraForGpu));
    CameraForGpu* cameraData = nullptr;
    cameraBuffer->Map(0, nullptr, reinterpret_cast<void**>(&cameraData));

    // G. Material Buffer (共通マテリアル設定)
    Microsoft::WRL::ComPtr<ID3D12Resource> materialBuffer = CreateBufferResource(device, sizeof(Material));
    Material* materialData = nullptr;
    materialBuffer->Map(0, nullptr, reinterpret_cast<void**>(&materialData));
    materialData->color = { 1.0f, 1.0f, 1.0f, 1.0f };
    materialData->enableLighting = 3; // Blinn-Phong
    materialData->shininess = 50.0f;
    materialData->uvTransform = MakeIdentity4x4();

    // ImGui
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::StyleColorsDark();
    ImGui_ImplWin32_Init(winApp->GetHwnd());
    ImGui_ImplDX12_Init(device, dxCommon->GetBackBufferCount(),
        dxCommon->GetRtvDesc().Format,
        srvDescriptorHeap.Get(),
        srvHandleCPU,
        srvHandleGPU);

    // ループ変数
    Vector3 cameraTranslate = { 0.0f, 5.0f, -15.0f };
    Vector3 cameraRotate = { 0.2f, 0.0f, 0.0f };

    while (true) {
        if (winApp->ProcessMessage()) break;

        ImGui_ImplDX12_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();

        // ---------------------------------------------------------
        // ImGui: ライト操作UI
        // ---------------------------------------------------------
        ImGui::Begin("Lighting Debug");

        // Directional Light
        if (ImGui::CollapsingHeader("Directional Light")) {
            ImGui::DragFloat3("Dir Direction", &lightData->directionalLights[0].direction.x, 0.01f, -1.0f, 1.0f);
            lightData->directionalLights[0].direction = Normalize(lightData->directionalLights[0].direction);
            ImGui::ColorEdit4("Dir Color", &lightData->directionalLights[0].color.x);
            ImGui::DragFloat("Dir Intensity", &lightData->directionalLights[0].intensity, 0.01f, 0.0f, 5.0f);
        }

        // Point Lights
        if (ImGui::CollapsingHeader("Point Lights")) {
            ImGui::SliderInt("Num Point Lights", &lightData->numPointLights, 0, kNumPointLights);
            for (int i = 0; i < lightData->numPointLights; ++i) {
                std::string label = "Point " + std::to_string(i);
                ImGui::PushID(i);
                ImGui::Text("%s", label.c_str());
                ImGui::DragFloat3("Pos", &lightData->pointLights[i].position.x, 0.1f);
                ImGui::ColorEdit4("Color", &lightData->pointLights[i].color.x);
                ImGui::DragFloat("Radius", &lightData->pointLights[i].radius, 0.1f, 0.1f, 100.0f);
                ImGui::DragFloat("Intensity", &lightData->pointLights[i].intensity, 0.01f, 0.0f, 10.0f);

                if (lightData->pointLights[i].radius <= 0) lightData->pointLights[i].radius = 10.0f;
                lightData->pointLights[i].decay = 1.0f;
                ImGui::PopID();
            }
        }

        // Spot Lights
        if (ImGui::CollapsingHeader("Spot Lights")) {
            ImGui::SliderInt("Num Spot Lights", &lightData->numSpotLights, 0, kNumSpotLights);
            for (int i = 0; i < lightData->numSpotLights; ++i) {
                std::string label = "Spot " + std::to_string(i);
                ImGui::PushID(i + 100);
                ImGui::Text("%s", label.c_str());
                ImGui::DragFloat3("Pos", &lightData->spotLights[i].position.x, 0.1f);
                ImGui::DragFloat3("Dir", &lightData->spotLights[i].direction.x, 0.01f);
                lightData->spotLights[i].direction = Normalize(lightData->spotLights[i].direction);
                ImGui::ColorEdit4("Color", &lightData->spotLights[i].color.x);
                ImGui::DragFloat("Distance", &lightData->spotLights[i].distance, 0.1f, 0.1f, 100.0f);
                ImGui::DragFloat("CosAngle", &lightData->spotLights[i].cosAngle, 0.01f, -1.0f, 1.0f);
                ImGui::DragFloat("FalloffStart", &lightData->spotLights[i].cosFalloffStart, 0.01f, -1.0f, 1.0f);

                if (lightData->spotLights[i].distance <= 0) lightData->spotLights[i].distance = 20.0f;
                lightData->spotLights[i].decay = 2.0f;
                ImGui::PopID();
            }
        }

        // Area Lights
        if (ImGui::CollapsingHeader("Area Lights")) {
            ImGui::SliderInt("Num Area Lights", &lightData->numAreaLights, 0, kNumAreaLights);
            for (int i = 0; i < lightData->numAreaLights; ++i) {
                std::string label = "Area " + std::to_string(i);
                ImGui::PushID(i + 200);
                ImGui::Text("%s", label.c_str());
                ImGui::DragFloat3("Pos", &lightData->areaLights[i].position.x, 0.1f);
                ImGui::DragFloat3("Dir", &lightData->areaLights[i].direction.x, 0.01f);
                lightData->areaLights[i].direction = Normalize(lightData->areaLights[i].direction);
                ImGui::ColorEdit4("Color", &lightData->areaLights[i].color.x);
                ImGui::DragFloat("Width", &lightData->areaLights[i].width, 0.1f);
                ImGui::DragFloat("Height", &lightData->areaLights[i].height, 0.1f);
                lightData->areaLights[i].decay = 1.0f;
                ImGui::PopID();
            }
        }

        ImGui::Separator();
        ImGui::Text("Camera Settings");
        ImGui::DragFloat3("Camera Trans", &cameraTranslate.x, 0.1f);
        ImGui::DragFloat3("Camera Rot", &cameraRotate.x, 0.01f);
        ImGui::End();

        // ---------------------------------------------------------
        // 更新処理
        // ---------------------------------------------------------

        // Update Camera
        Matrix4x4 cameraWorld = MakeAffineMatrix({ 1.0f, 1.0f, 1.0f }, cameraRotate, cameraTranslate);
        Matrix4x4 viewMatrix = Inverse(cameraWorld);
        Matrix4x4 projectionMatrix = MakePerspectiveFovMatrix(0.45f, (float)WinApp::kClientWidth / (float)WinApp::kClientHeight, 0.1f, 100.0f);
        Matrix4x4 viewProjectionMatrix = Multiply(viewMatrix, projectionMatrix);
        cameraData->worldPosition = cameraTranslate;

        // Update Terrain
        if (terrainModel) {
            terrainModel->transform.scale = { 1.0f, 1.0f, 1.0f };
            terrainModel->transform.rotate = { 0.0f, 0.0f, 0.0f };
            terrainModel->transform.translate = { 0.0f, -2.0f, 0.0f };

            Matrix4x4 worldMatrix = MakeAffineMatrix(terrainModel->transform.scale, terrainModel->transform.rotate, terrainModel->transform.translate);
            Matrix4x4 wvpMatrix = Multiply(worldMatrix, viewProjectionMatrix);
            Matrix4x4 worldInv = Inverse(worldMatrix);
            Matrix4x4 worldInvT = Transpose(worldInv);

            terrainMapData->World = worldMatrix;
            terrainMapData->WVP = wvpMatrix;
            terrainMapData->WorldInverseTranspose = worldInvT;
        }

        // Update Sphere
        if (sphereModel) {
            sphereModel->transform.scale = { 1.0f, 1.0f, 1.0f };
            // sphereModel->transform.rotate は固定またはUI制御など
            sphereModel->transform.translate = { 0.0f, 1.0f, 0.0f };

            Matrix4x4 worldMatrix = MakeAffineMatrix(sphereModel->transform.scale, sphereModel->transform.rotate, sphereModel->transform.translate);
            Matrix4x4 wvpMatrix = Multiply(worldMatrix, viewProjectionMatrix);
            Matrix4x4 worldInv = Inverse(worldMatrix);
            Matrix4x4 worldInvT = Transpose(worldInv);

            sphereMapData->World = worldMatrix;
            sphereMapData->WVP = wvpMatrix;
            sphereMapData->WorldInverseTranspose = worldInvT;
        }

        // ---------------------------------------------------------
        // 描画処理
        // ---------------------------------------------------------
        dxCommon->PreDraw();

        commandList->SetGraphicsRootSignature(graphicsPipeline->GetRootSignature());
        commandList->SetPipelineState(graphicsPipeline->GetPipelineState(kBlendModeNormal));
        commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

        ID3D12DescriptorHeap* descriptorHeaps[] = { srvDescriptorHeap.Get() };
        commandList->SetDescriptorHeaps(1, descriptorHeaps);

        // GraphicsPipeline.cpp で設定した RootParameter の順番
        // 0: b0 (Material)
        commandList->SetGraphicsRootConstantBufferView(0, materialBuffer->GetGPUVirtualAddress());

        // 1: b1 (LightGroup) - 修正箇所
        commandList->SetGraphicsRootConstantBufferView(1, lightBuffer->GetGPUVirtualAddress());

        // 2: b2 (Camera)
        commandList->SetGraphicsRootConstantBufferView(2, cameraBuffer->GetGPUVirtualAddress());

        // Terrain Draw
        if (terrainModel) {
            // 3: t0 (Texture) -> terrainSrvGPU
            commandList->SetGraphicsRootDescriptorTable(3, terrainSrvGPU);
            // 4: t1 (Transform) -> terrainTransGPU
            commandList->SetGraphicsRootDescriptorTable(4, terrainTransGPU);

            // DrawCall
            terrainModel->Draw(commandList, 1, terrainSrvGPU, terrainTransGPU);
        }

        // Sphere Draw
        if (sphereModel) {
            // 3: t0 (Texture) -> ballSrvGPU
            commandList->SetGraphicsRootDescriptorTable(3, ballSrvGPU);
            // 4: t1 (Transform) -> sphereTransGPU
            commandList->SetGraphicsRootDescriptorTable(4, sphereTransGPU);

            // DrawCall
            sphereModel->Draw(commandList, 1, ballSrvGPU, sphereTransGPU);
        }

        ImGui::Render();
        ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), commandList);

        dxCommon->PostDraw();
    }

    ImGui_ImplDX12_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();

    delete terrainModel;
    delete sphereModel;
    delete graphicsPipeline;

    dxCommon->Finalize();
    winApp->Finalize();

    return 0;
}