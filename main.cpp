#define _USE_MATH_DEFINES
#include <cassert>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>
#include <wrl/client.h>
#include <Windows.h>
#include <objbase.h>
#include <d3d12.h>
#include <dbghelp.h>
#include <dxcapi.h>
#include <dxgi1_6.h>
#include <dxgidebug.h>
#include <strsafe.h>
#include <random>

#include "externals/DirectXTex/DirectXTex.h"
#include "externals/DirectXTex/d3dx12.h"
#include "externals/imgui/imgui.h"
#include "externals/imgui/imgui_impl_dx12.h"
#include "externals/imgui/imgui_impl_win32.h"
#include <xaudio2.h>

#pragma comment(lib, "xaudio2.lib")
#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "dbghelp.lib")
#pragma comment(lib, "dxguid.lib")
#pragma comment(lib, "dxcompiler.lib")

#include "WinApp.h"
#include "DirectXCommon.h"
#include "GraphicsPipeline.h"
#include "D3D12Util.h"
#include "Model.h"
#include "MathUtil.h"
#include "DataTypes.h"

// --- パーティクル用構造体 ---

// GPUに送るデータ
struct ParticleForGPU {
    Matrix4x4 WVP;
    Matrix4x4 World;
    Vector4 color;
};

// CPUで管理する個々のパーティクル情報
struct Particle {
    Transform transform;
    Vector3 velocity;
    Vector4 color;
    float lifeTime;
    float currentTime;
};

// --- ヘルパー関数 (省略) ---
Vector3 Add(const Vector3& v1, const Vector3& v2) { return { v1.x + v2.x, v1.y + v2.y, v1.z + v2.z }; }
static LONG WINAPI ExportDump(EXCEPTION_POINTERS* exception) { return EXCEPTION_EXECUTE_HANDLER; }
void Log(std::ostream& os, const std::string& message) { os << message << std::endl; OutputDebugStringA(message.c_str()); }
std::wstring ConvertString(const std::string& str) {
    if (str.empty()) { return std::wstring(); }
    auto sizeNeeded = MultiByteToWideChar(CP_UTF8, 0, reinterpret_cast<const char*>(&str[0]), static_cast<int>(str.size()), NULL, 0);
    if (sizeNeeded == 0) { return std::wstring(); }
    std::wstring result(sizeNeeded, 0);
    MultiByteToWideChar(CP_UTF8, 0, reinterpret_cast<const char*>(&str[0]), static_cast<int>(str.size()), &result[0], sizeNeeded);
    return result;
}
std::string ConvertString(const std::wstring& str) {
    if (str.empty()) { return std::string(); }
    auto sizeNeeded = WideCharToMultiByte(CP_UTF8, 0, str.data(), static_cast<int>(str.size()), NULL, 0, NULL, NULL);
    if (sizeNeeded == 0) { return std::string(); }
    std::string result(sizeNeeded, 0);
    WideCharToMultiByte(CP_UTF8, 0, str.data(), static_cast<int>(str.size()), result.data(), sizeNeeded, NULL, NULL);
    return result;
}
struct D3DResourceLeakChecker {
    ~D3DResourceLeakChecker() {
        Microsoft::WRL::ComPtr<IDXGIDebug1> debug;
        if (SUCCEEDED(DXGIGetDebugInterface1(0, IID_PPV_ARGS(&debug)))) {
            debug->ReportLiveObjects(DXGI_DEBUG_ALL, DXGI_DEBUG_RLO_ALL);
            debug->ReportLiveObjects(DXGI_DEBUG_APP, DXGI_DEBUG_RLO_ALL);
            debug->ReportLiveObjects(DXGI_DEBUG_D3D12, DXGI_DEBUG_RLO_ALL);
        }
    }
};

// --- パーティクル生成関数 ---
Particle MakeNewParticle(std::mt19937& randomEngine, const Vector3& translate) {
    std::uniform_real_distribution<float> distributionPos(-1.0f, 1.0f);
    std::uniform_real_distribution<float> distributionVel(-1.0f, 1.0f);
    std::uniform_real_distribution<float> distributionColor(0.0f, 1.0f);
    std::uniform_real_distribution<float> distributionTime(1.0f, 3.0f);

    Particle particle;
    // 位置：指定された基準位置の周りにランダムに配置
    particle.transform.scale = { 1.0f, 1.0f, 1.0f };
    particle.transform.rotate = { 0.0f, 0.0f, 0.0f };
    particle.transform.translate = {
        translate.x + distributionPos(randomEngine),
        translate.y + distributionPos(randomEngine),
        translate.z + distributionPos(randomEngine)
    };

    // 速度：全方向にランダム
    particle.velocity = {
        distributionVel(randomEngine),
        distributionVel(randomEngine),
        distributionVel(randomEngine)
    };

    // 色：ランダム（RGB）、Alphaは1.0
    particle.color = {
        distributionColor(randomEngine),
        distributionColor(randomEngine),
        distributionColor(randomEngine),
        1.0f
    };

    // 寿命：1.0秒〜3.0秒の間
    particle.lifeTime = distributionTime(randomEngine);
    particle.currentTime = 0.0f;

    return particle;
}

int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {
    D3DResourceLeakChecker leakChecker;
    // (デバッグレイヤー省略)

    WinApp* winApp = WinApp::GetInstance();
    winApp->Initialize();
    DirectXCommon* dxCommon = DirectXCommon::GetInstance();
    dxCommon->Initialize(winApp);
    // (XAudio2, 例外ハンドラ省略)

    ID3D12Device* device = dxCommon->GetDevice();
    GraphicsPipeline* graphicsPipeline = new GraphicsPipeline();
    graphicsPipeline->Initialize(device);
    ID3D12GraphicsCommandList* commandList = dxCommon->GetCommandList();

    // パーティクルモデル(四角形)を生成
    Model* particleModel = Model::CreateParticleModel(device);

    const UINT kNumInstances = 100;

    // StructuredBufferのリソース作成
    Microsoft::WRL::ComPtr<ID3D12Resource> instancingResource =
        CreateBufferResource(device, sizeof(ParticleForGPU) * kNumInstances);
    ParticleForGPU* instancingData = nullptr;
    instancingResource->Map(0, nullptr, reinterpret_cast<void**>(&instancingData));

    // ランダムエンジン初期化
    std::random_device seedGenerator;
    std::mt19937 randomEngine(seedGenerator());

    // ★変更: エミッター（発生地点）の位置を定義。奥（Zプラス方向）に配置。
    Vector3 emitterPos = { 0.0f, 0.0f, 15.0f };

    // CPU側のパーティクル管理用配列初期化
    std::vector<Particle> particles(kNumInstances);
    for (UINT i = 0; i < kNumInstances; ++i) {
        // 初期化時もエミッター位置を中心に生成
        particles[i] = MakeNewParticle(randomEngine, emitterPos);
    }

    // Descriptor Heap
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> srvDescriptorHeap =
        CreateDescriptorHeap(device, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 128, true);
    const uint32_t descriptorSizeSRV = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    // ★変更: テクスチャ読み込みを circle.png に変更
    // ※ resourcesフォルダに circle.png が必要です
    DirectX::ScratchImage mipImages = LoadTexture("resources/circle.png");

    const DirectX::TexMetadata& metadata = mipImages.GetMetadata();
    Microsoft::WRL::ComPtr<ID3D12Resource> textureResource = CreateTextureResource(device, metadata);
    Microsoft::WRL::ComPtr<ID3D12Resource> intermediateResource = UploadTextureData(textureResource.Get(), mipImages, device, commandList);

    // テクスチャ転送コマンド実行と待機 (省略)
    commandList->Close();
    ID3D12CommandQueue* commandQueue = dxCommon->GetCommandQueue();
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

    // Texture SRV 作成 (Index 1)
    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
    srvDesc.Format = metadata.format;
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MipLevels = UINT(metadata.mipLevels);
    D3D12_CPU_DESCRIPTOR_HANDLE textureSrvHandleCPU = GetCPUDescriptorHandle(srvDescriptorHeap.Get(), descriptorSizeSRV, 1);
    D3D12_GPU_DESCRIPTOR_HANDLE textureSrvHandleGPU = GetGPUDescriptorHandle(srvDescriptorHeap.Get(), descriptorSizeSRV, 1);
    device->CreateShaderResourceView(textureResource.Get(), &srvDesc, textureSrvHandleCPU);

    // Instancing StructuredBuffer SRV 作成 (Index 2)
    D3D12_SHADER_RESOURCE_VIEW_DESC instancingSrvDesc{};
    instancingSrvDesc.Format = DXGI_FORMAT_UNKNOWN;
    instancingSrvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    instancingSrvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
    instancingSrvDesc.Buffer.FirstElement = 0;
    instancingSrvDesc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;
    instancingSrvDesc.Buffer.NumElements = kNumInstances;
    instancingSrvDesc.Buffer.StructureByteStride = sizeof(ParticleForGPU);
    const uint32_t kInstancingSrvIndex = 2;
    D3D12_CPU_DESCRIPTOR_HANDLE instancingSrvHandleCPU = GetCPUDescriptorHandle(srvDescriptorHeap.Get(), descriptorSizeSRV, kInstancingSrvIndex);
    D3D12_GPU_DESCRIPTOR_HANDLE instancingSrvHandleGPU = GetGPUDescriptorHandle(srvDescriptorHeap.Get(), descriptorSizeSRV, kInstancingSrvIndex);
    device->CreateShaderResourceView(instancingResource.Get(), &instancingSrvDesc, instancingSrvHandleCPU);

    // カメラ位置 (Z = -10.0f)
    Transform cameraTransform{ { 1.0f, 1.0f, 1.0f }, { 0.2f, 0.0f, 0.0f }, { 0.0f, 0.0f, -10.0f } };

    const float kDeltaTime = 1.0f / 60.0f;
    bool useBlendAdd = true;

    // ImGui初期化 (省略)
    IMGUI_CHECKVERSION(); ImGui::CreateContext(); ImGui::StyleColorsClassic(); ImGui_ImplWin32_Init(winApp->GetHwnd());
    ImGui_ImplDX12_Init(device, dxCommon->GetBackBufferCount(), dxCommon->GetRtvDesc().Format, srvDescriptorHeap.Get(), srvDescriptorHeap->GetCPUDescriptorHandleForHeapStart(), srvDescriptorHeap->GetGPUDescriptorHandleForHeapStart());

    while (!winApp->IsEndRequested()) {
        winApp->ProcessMessage();

        ImGui_ImplDX12_NewFrame(); ImGui_ImplWin32_NewFrame(); ImGui::NewFrame();
        ImGui::Begin("Settings");
        // ImGuiで発生地点を調整可能にしておく
        ImGui::DragFloat3("Emitter Pos", &emitterPos.x, 0.1f);
        ImGui::Checkbox("Use Add Blend", &useBlendAdd);
        ImGui::End();

        // カメラ更新
        Matrix4x4 cameraMatrix = MakeAffineMatrix(cameraTransform.scale, cameraTransform.rotate, cameraTransform.translate);
        Matrix4x4 viewMatrix = Inverse(cameraMatrix);
        Matrix4x4 projectionMatrix = MakePerspectiveFovMatrix(0.45f, (float)winApp->kClientWidth / (float)winApp->kClientHeight, 0.1f, 100.0f);
        Matrix4x4 viewProjectionMatrix = Multiply(viewMatrix, projectionMatrix);

        // --- パーティクル更新ループ ---
        for (uint32_t i = 0; i < kNumInstances; ++i) {

            // 1. 寿命チェックとRespawn (エミッター位置で再生成)
            if (particles[i].currentTime >= particles[i].lifeTime) {
                particles[i] = MakeNewParticle(randomEngine, emitterPos);
            }

            // 2. 移動
            particles[i].transform.translate.x += particles[i].velocity.x * kDeltaTime;
            particles[i].transform.translate.y += particles[i].velocity.y * kDeltaTime;
            particles[i].transform.translate.z += particles[i].velocity.z * kDeltaTime;

            // 3. 時間経過
            particles[i].currentTime += kDeltaTime;

            // 4. アルファ値計算
            float alpha = 1.0f - (particles[i].currentTime / particles[i].lifeTime);
            particles[i].color.w = alpha;

            // 5. 行列計算 (簡易ビルボード: 回転なし)
            Matrix4x4 worldMatrix = MakeAffineMatrix(
                particles[i].transform.scale,
                { 0,0,0 },
                particles[i].transform.translate
            );

            // 6. GPUへ転送
            instancingData[i].World = worldMatrix;
            instancingData[i].WVP = Multiply(worldMatrix, viewProjectionMatrix);
            instancingData[i].color = particles[i].color;
        }

        dxCommon->PreDraw();
        commandList->SetGraphicsRootSignature(graphicsPipeline->GetRootSignature());
        ID3D12DescriptorHeap* descriptorHeaps[] = { srvDescriptorHeap.Get() };
        commandList->SetDescriptorHeaps(1, descriptorHeaps);
        commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

        // ブレンドモード設定 (加算 or 通常)
        commandList->SetPipelineState(graphicsPipeline->GetPipelineState(useBlendAdd ? kBlendModeAdd : kBlendModeNormal));

        // Draw呼び出し
        particleModel->Draw(commandList, kNumInstances, textureSrvHandleGPU, instancingSrvHandleGPU);

        ImGui::Render();
        ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), commandList);
        dxCommon->PostDraw();
    }

    // 終了処理 (省略)
    ImGui_ImplDX12_Shutdown(); ImGui_ImplWin32_Shutdown(); ImGui::DestroyContext();
    delete particleModel; delete graphicsPipeline;
    dxCommon->Finalize(); CoUninitialize(); winApp->Finalize();
    return 0;
}