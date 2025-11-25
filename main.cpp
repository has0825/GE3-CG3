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

// Vector3の足し算を行うヘルパー関数
Vector3 Add(const Vector3& v1, const Vector3& v2) {
    return { v1.x + v2.x, v1.y + v2.y, v1.z + v2.z };
}

static LONG WINAPI ExportDump(EXCEPTION_POINTERS* exception)
{
    SYSTEMTIME time;
    GetLocalTime(&time);
    wchar_t filePath[MAX_PATH] = { 0 };
    CreateDirectory(L"./Dumps", nullptr);
    StringCchPrintfW(filePath, MAX_PATH, L"./Dumps/%04d-%02d%02d-%02d%02d.dmp",
        time.wYear, time.wMonth, time.wDay, time.wHour,
        time.wMinute);
    HANDLE dumpFileHandle = CreateFile(filePath, GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_WRITE | FILE_SHARE_READ, 0, CREATE_ALWAYS, 0, 0);
    DWORD processId = GetCurrentProcessId();
    DWORD threadId = GetCurrentThreadId();
    MINIDUMP_EXCEPTION_INFORMATION minidumpInformation{ 0 };
    minidumpInformation.ThreadId = threadId;
    minidumpInformation.ExceptionPointers = exception;
    minidumpInformation.ClientPointers = TRUE;
    MiniDumpWriteDump(GetCurrentProcess(), processId, dumpFileHandle,
        MiniDumpNormal, &minidumpInformation, nullptr, nullptr);
    return EXCEPTION_EXECUTE_HANDLER;
}
void Log(std::ostream& os, const std::string& message)
{
    os << message << std::endl;
    OutputDebugStringA(message.c_str());
}
std::wstring ConvertString(const std::string& str)
{
    if (str.empty()) { return std::wstring(); }
    auto sizeNeeded = MultiByteToWideChar(CP_UTF8, 0, reinterpret_cast<const char*>(&str[0]), static_cast<int>(str.size()), NULL, 0);
    if (sizeNeeded == 0) { return std::wstring(); }
    std::wstring result(sizeNeeded, 0);
    MultiByteToWideChar(CP_UTF8, 0, reinterpret_cast<const char*>(&str[0]), static_cast<int>(str.size()), &result[0], sizeNeeded);
    return result;
}
std::string ConvertString(const std::wstring& str)
{
    if (str.empty()) { return std::string(); }
    auto sizeNeeded = WideCharToMultiByte(CP_UTF8, 0, str.data(), static_cast<int>(str.size()), NULL, 0, NULL, NULL);
    if (sizeNeeded == 0) { return std::string(); }
    std::string result(sizeNeeded, 0);
    WideCharToMultiByte(CP_UTF8, 0, str.data(), static_cast<int>(str.size()), result.data(), sizeNeeded, NULL, NULL);
    return result;
}
struct D3DResourceLeakChecker {
    ~D3DResourceLeakChecker()
    {
        Microsoft::WRL::ComPtr<IDXGIDebug1> debug;
        if (SUCCEEDED(DXGIGetDebugInterface1(0, IID_PPV_ARGS(&debug)))) {
            debug->ReportLiveObjects(DXGI_DEBUG_ALL, DXGI_DEBUG_RLO_ALL);
            debug->ReportLiveObjects(DXGI_DEBUG_APP, DXGI_DEBUG_RLO_ALL);
            debug->ReportLiveObjects(DXGI_DEBUG_D3D12, DXGI_DEBUG_RLO_ALL);
        }
    }
};

int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int)
{
    D3DResourceLeakChecker leakChecker;

    // --- デバッグレイヤー有効化 ---
    Microsoft::WRL::ComPtr<ID3D12Debug1> debugController;
    if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debugController)))) {
        debugController->EnableDebugLayer();
        debugController->SetEnableGPUBasedValidation(TRUE);
    }

    WinApp* winApp = WinApp::GetInstance();
    winApp->Initialize();

    DirectXCommon* dxCommon = DirectXCommon::GetInstance();
    dxCommon->Initialize(winApp);

    CoInitializeEx(0, COINIT_MULTITHREADED);
    SetUnhandledExceptionFilter(ExportDump);

    Microsoft::WRL::ComPtr<IXAudio2> xAudio2;
    IXAudio2MasteringVoice* masterVoice;
    XAudio2Create(&xAudio2, 0, XAUDIO2_DEFAULT_PROCESSOR);
    xAudio2->CreateMasteringVoice(&masterVoice);

    ID3D12Device* device = dxCommon->GetDevice();
    GraphicsPipeline* graphicsPipeline = new GraphicsPipeline();
    graphicsPipeline->Initialize(device);

    ID3D12GraphicsCommandList* commandList = dxCommon->GetCommandList();

    // パーティクルモデル(四角形)を生成
    Model* particleModel = Model::CreateParticleModel(device);

    const UINT kNumInstances = 10;

    // StructuredBufferのリソース作成
    Microsoft::WRL::ComPtr<ID3D12Resource> instancingResource =
        CreateBufferResource(device, sizeof(TransformationMatrix) * kNumInstances);

    TransformationMatrix* instancingData = nullptr;
    instancingResource->Map(0, nullptr, reinterpret_cast<void**>(&instancingData));

    // 初期化
    for (UINT i = 0; i < kNumInstances; ++i) {
        instancingData[i].WVP = MakeIdentity4x4();
        instancingData[i].World = MakeIdentity4x4();
    }

    // Descriptor Heap (Texture x 1 + Instancing x 1)
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> srvDescriptorHeap =
        CreateDescriptorHeap(device, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 128, true);

    const uint32_t descriptorSizeSRV = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    // テクスチャ読み込み
    DirectX::ScratchImage mipImages = LoadTexture("resources/uvChecker.png");
    const DirectX::TexMetadata& metadata = mipImages.GetMetadata();
    Microsoft::WRL::ComPtr<ID3D12Resource> textureResource = CreateTextureResource(device, metadata);

    // ★ 中間リソースを変数で受け取る
    Microsoft::WRL::ComPtr<ID3D12Resource> intermediateResource =
        UploadTextureData(textureResource.Get(), mipImages, device, commandList);

    // ★ ここで一度コマンドを実行して転送を完了させる

    // 1. コマンドリストを閉じる
    commandList->Close();

    // 2. 実行
    ID3D12CommandQueue* commandQueue = dxCommon->GetCommandQueue();
    ID3D12CommandList* ppCommandLists[] = { commandList };
    commandQueue->ExecuteCommandLists(1, ppCommandLists);

    // 3. GPUの完了を待つ (Fence)
    Microsoft::WRL::ComPtr<ID3D12Fence> fence;
    device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence));
    HANDLE fenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);

    // シグナル発行
    commandQueue->Signal(fence.Get(), 1);

    // 完了待ち
    if (fence->GetCompletedValue() < 1) {
        fence->SetEventOnCompletion(1, fenceEvent);
        WaitForSingleObject(fenceEvent, INFINITE);
    }
    CloseHandle(fenceEvent);

    // ★★★ 転送完了！ ★★★

    // Texture SRV (Index 1)
    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
    srvDesc.Format = metadata.format;
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MipLevels = UINT(metadata.mipLevels);

    D3D12_CPU_DESCRIPTOR_HANDLE textureSrvHandleCPU = GetCPUDescriptorHandle(srvDescriptorHeap.Get(), descriptorSizeSRV, 1);
    D3D12_GPU_DESCRIPTOR_HANDLE textureSrvHandleGPU = GetGPUDescriptorHandle(srvDescriptorHeap.Get(), descriptorSizeSRV, 1);
    device->CreateShaderResourceView(textureResource.Get(), &srvDesc, textureSrvHandleCPU);

    // Instancing StructuredBuffer SRV (Index 2)
    D3D12_SHADER_RESOURCE_VIEW_DESC instancingSrvDesc{};
    instancingSrvDesc.Format = DXGI_FORMAT_UNKNOWN;
    instancingSrvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    instancingSrvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
    instancingSrvDesc.Buffer.FirstElement = 0;
    instancingSrvDesc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;
    instancingSrvDesc.Buffer.NumElements = kNumInstances;
    instancingSrvDesc.Buffer.StructureByteStride = sizeof(TransformationMatrix);

    const uint32_t kInstancingSrvIndex = 2;
    D3D12_CPU_DESCRIPTOR_HANDLE instancingSrvHandleCPU = GetCPUDescriptorHandle(srvDescriptorHeap.Get(), descriptorSizeSRV, kInstancingSrvIndex);
    D3D12_GPU_DESCRIPTOR_HANDLE instancingSrvHandleGPU = GetGPUDescriptorHandle(srvDescriptorHeap.Get(), descriptorSizeSRV, kInstancingSrvIndex);

    device->CreateShaderResourceView(instancingResource.Get(), &instancingSrvDesc, instancingSrvHandleCPU);

    // Transform配列の準備
    std::vector<Transform> transforms(kNumInstances);
    for (uint32_t i = 0; i < kNumInstances; ++i) {
        transforms[i].scale = { 1.0f, 1.0f, 1.0f };
        transforms[i].rotate = { 0.0f, 0.0f, 0.0f };
        transforms[i].translate = { i * 0.1f, i * 0.1f, i * 0.1f };
    }

    Transform cameraTransform{ { 1.0f, 1.0f, 1.0f }, { 0.2f, 0.0f, 0.0f }, { 0.0f, 0.0f, -5.0f } };

    // ★ 追加: パーティクル全体の基準位置（Root Translate）
    // 初期値: 下(-2.0) かつ 奥(10.0)
    Vector3 rootTranslate = { 0.0f, -2.0f, 10.0f };

    // 各パーティクルのズレ幅
    Vector3 instanceOffset = { 0.1f, 0.1f, 0.1f };

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::StyleColorsClassic();
    ImGui_ImplWin32_Init(winApp->GetHwnd());
    ImGui_ImplDX12_Init(device, dxCommon->GetBackBufferCount(), dxCommon->GetRtvDesc().Format,
        srvDescriptorHeap.Get(),
        srvDescriptorHeap->GetCPUDescriptorHandleForHeapStart(),
        srvDescriptorHeap->GetGPUDescriptorHandleForHeapStart());

    while (!winApp->IsEndRequested()) {
        winApp->ProcessMessage();

        ImGui_ImplDX12_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();
        ImGui::Begin("Settings");

        // ★ 追加: ImGuiで基準位置を動かせるようにする
        ImGui::DragFloat3("Root Translate", &rootTranslate.x, 0.1f);
        ImGui::DragFloat3("Instance Offset", &instanceOffset.x, 0.01f);

        ImGui::End();

        // 更新処理
        Matrix4x4 cameraMatrix = MakeAffineMatrix(cameraTransform.scale, cameraTransform.rotate, cameraTransform.translate);
        Matrix4x4 viewMatrix = Inverse(cameraMatrix);
        Matrix4x4 projectionMatrix = MakePerspectiveFovMatrix(0.45f, (float)winApp->kClientWidth / (float)winApp->kClientHeight, 0.1f, 100.0f);
        Matrix4x4 viewProjectionMatrix = Multiply(viewMatrix, projectionMatrix);

        for (uint32_t i = 0; i < kNumInstances; ++i) {
            // ★ 修正: 基準位置(rootTranslate) + ズレ(offset * i) で計算
            transforms[i].translate = {
                rootTranslate.x + i * instanceOffset.x,
                rootTranslate.y + i * instanceOffset.y,
                rootTranslate.z + i * instanceOffset.z
            };

            Matrix4x4 worldMatrix = MakeAffineMatrix(transforms[i].scale, transforms[i].rotate, transforms[i].translate);
            instancingData[i].World = worldMatrix;
            instancingData[i].WVP = Multiply(worldMatrix, viewProjectionMatrix);
        }

        dxCommon->PreDraw();

        commandList->SetGraphicsRootSignature(graphicsPipeline->GetRootSignature());
        ID3D12DescriptorHeap* descriptorHeaps[] = { srvDescriptorHeap.Get() };
        commandList->SetDescriptorHeaps(1, descriptorHeaps);
        commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

        commandList->SetPipelineState(graphicsPipeline->GetPipelineState(kBlendModeNormal));

        // Draw呼び出し (Texture:Index 1, Instancing:Index 2)
        particleModel->Draw(
            commandList,
            kNumInstances,
            textureSrvHandleGPU,
            instancingSrvHandleGPU
        );

        ImGui::Render();
        ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), commandList);
        dxCommon->PostDraw();
    }

    ImGui_ImplDX12_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();

    delete particleModel;
    delete graphicsPipeline;

    dxCommon->Finalize();
    CoUninitialize();
    winApp->Finalize();

    return 0;
}