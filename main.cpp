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
#include <cmath>

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

// --- 数値計算関数 ---
static float Length(const Vector3& v) {
    return std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
}

static Vector3 Normalize(const Vector3& v) {
    float len = Length(v);
    if (len == 0.0f) return { 0.0f, 0.0f, 0.0f };
    return { v.x / len, v.y / len, v.z / len };
}

static Vector3 Cross(const Vector3& v1, const Vector3& v2) {
    return {
        v1.y * v2.z - v1.z * v2.y,
        v1.z * v2.x - v1.x * v2.z,
        v1.x * v2.y - v1.y * v2.x
    };
}

static Vector4 HSVtoRGB(float h, float s, float v, float alpha) {
    float c = v * s;
    float x = c * (1 - std::abs(std::fmod(h / 60.0f, 2) - 1));
    float m = v - c;
    float r = 0, g = 0, b = 0;
    if (0 <= h && h < 60) { r = c; g = x; b = 0; } else if (60 <= h && h < 120) { r = x; g = c; b = 0; } else if (120 <= h && h < 180) { r = 0; g = c; b = x; } else if (180 <= h && h < 240) { r = 0; g = x; b = c; } else if (240 <= h && h < 300) { r = x; g = 0; b = c; } else if (300 <= h && h < 360) { r = c; g = 0; b = x; }
    return { r + m, g + m, b + m, alpha };
}

// --- システム関数 ---
static LONG WINAPI ExportDump(EXCEPTION_POINTERS* exception) { return EXCEPTION_EXECUTE_HANDLER; }
void Log(std::ostream& os, const std::string& message) { os << message << std::endl; OutputDebugStringA(message.c_str()); }
std::wstring ConvertString(const std::string& str) {
    if (str.empty()) return std::wstring();
    int sizeNeeded = MultiByteToWideChar(CP_UTF8, 0, &str[0], (int)str.size(), NULL, 0);
    std::wstring result(sizeNeeded, 0);
    MultiByteToWideChar(CP_UTF8, 0, &str[0], (int)str.size(), &result[0], sizeNeeded);
    return result;
}
std::string ConvertString(const std::wstring& str) {
    if (str.empty()) return std::string();
    int sizeNeeded = WideCharToMultiByte(CP_UTF8, 0, str.data(), (int)str.size(), NULL, 0, NULL, NULL);
    std::string result(sizeNeeded, 0);
    WideCharToMultiByte(CP_UTF8, 0, str.data(), (int)str.size(), result.data(), sizeNeeded, NULL, NULL);
    return result;
}
struct D3DResourceLeakChecker {
    ~D3DResourceLeakChecker() {
        Microsoft::WRL::ComPtr<IDXGIDebug1> debug;
        if (SUCCEEDED(DXGIGetDebugInterface1(0, IID_PPV_ARGS(&debug)))) {
            debug->ReportLiveObjects(DXGI_DEBUG_ALL, DXGI_DEBUG_RLO_ALL);
            debug->ReportLiveObjects(DXGI_DEBUG_D3D12, DXGI_DEBUG_RLO_ALL);
        }
    }
};

// --- 構造体 ---
struct ParticleForGPU {
    Matrix4x4 WVP;
    Matrix4x4 World;
    Vector4 color;
};

struct Particle {
    Transform transform;
    Vector3 velocity;
    Vector3 acceleration;
    Vector4 color;
    float lifeTime;
    float currentTime;
};

struct AccelerationField {
    Vector3 position;
    float strength;
    float rotation;
};

// --- パーティクル生成 ---
Particle MakeNewParticle(std::mt19937& randomEngine, const Vector3& centerPos, float radius) {
    std::uniform_real_distribution<float> distVal(-1.0f, 1.0f);
    std::uniform_real_distribution<float> distColor(0.5f, 1.0f);
    std::uniform_real_distribution<float> distTime(0.5f, 1.5f);

    Particle particle;

    // ランダム配置
    Vector3 dir = { distVal(randomEngine), distVal(randomEngine), distVal(randomEngine) };
    dir = Normalize(dir);
    float r = std::abs(distVal(randomEngine)) * radius;

    // サイズ
    particle.transform.scale = { 0.1f, 0.1f, 0.1f };
    particle.transform.rotate = { 0.0f, 0.0f, 0.0f };

    particle.transform.translate = {
        centerPos.x + dir.x * r,
        centerPos.y + dir.y * r,
        centerPos.z + dir.z * r
    };

    // ★修正: ここにあった余計な位置変更コードを完全に削除しました

    particle.velocity = { 0.0f, 0.0f, 0.0f };
    particle.acceleration = { 0.0f, 0.0f, 0.0f };

    particle.color = HSVtoRGB(std::uniform_real_distribution<float>(180.0f, 240.0f)(randomEngine), 1.0f, distColor(randomEngine), 1.0f);
    particle.lifeTime = distTime(randomEngine);
    particle.currentTime = 0.0f;

    return particle;
}

// --- Main ---
int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int)
{
    D3DResourceLeakChecker leakChecker;
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

    Model* particleModel = Model::CreateParticleModel(device);

    const UINT kNumInstances = 3000;

    Microsoft::WRL::ComPtr<ID3D12Resource> instancingResource =
        CreateBufferResource(device, sizeof(ParticleForGPU) * kNumInstances);
    ParticleForGPU* instancingData = nullptr;
    instancingResource->Map(0, nullptr, reinterpret_cast<void**>(&instancingData));

    std::random_device seedGenerator;
    std::mt19937 randomEngine(seedGenerator());

    Vector3 emitterPos = { 0.0f, 0.0f, 0.0f };
    float emitterRadius = 1.0f;

    AccelerationField field;
    field.position = emitterPos;
    field.strength = 30.0f;
    field.rotation = 20.0f;

    std::vector<Particle> particles(kNumInstances);
    for (UINT i = 0; i < kNumInstances; ++i) {
        particles[i] = MakeNewParticle(randomEngine, emitterPos, emitterRadius);
        particles[i].currentTime = std::uniform_real_distribution<float>(0.0f, particles[i].lifeTime)(randomEngine);
    }

    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> srvDescriptorHeap =
        CreateDescriptorHeap(device, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 128, true);
    const uint32_t descriptorSizeSRV = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    DirectX::ScratchImage mipImages = LoadTexture("resources/circle.png");
    const DirectX::TexMetadata& metadata = mipImages.GetMetadata();
    Microsoft::WRL::ComPtr<ID3D12Resource> textureResource = CreateTextureResource(device, metadata);
    Microsoft::WRL::ComPtr<ID3D12Resource> intermediateResource = UploadTextureData(textureResource.Get(), mipImages, device, commandList);

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

    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
    srvDesc.Format = metadata.format;
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MipLevels = UINT(metadata.mipLevels);
    D3D12_CPU_DESCRIPTOR_HANDLE textureSrvHandleCPU = GetCPUDescriptorHandle(srvDescriptorHeap.Get(), descriptorSizeSRV, 1);
    D3D12_GPU_DESCRIPTOR_HANDLE textureSrvHandleGPU = GetGPUDescriptorHandle(srvDescriptorHeap.Get(), descriptorSizeSRV, 1);
    device->CreateShaderResourceView(textureResource.Get(), &srvDesc, textureSrvHandleCPU);

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

    Transform cameraTransform{ { 1.0f, 1.0f, 1.0f }, { 0.2f, 0.0f, 0.0f }, { 0.0f, 0.0f, -10.0f } };

    const float kDeltaTime = 1.0f / 60.0f;
    bool useBlendAdd = true;

    IMGUI_CHECKVERSION(); ImGui::CreateContext(); ImGui::StyleColorsDark();
    ImGui_ImplWin32_Init(winApp->GetHwnd());
    ImGui_ImplDX12_Init(device, dxCommon->GetBackBufferCount(), dxCommon->GetRtvDesc().Format, srvDescriptorHeap.Get(), srvDescriptorHeap->GetCPUDescriptorHandleForHeapStart(), srvDescriptorHeap->GetGPUDescriptorHandleForHeapStart());

    while (!winApp->IsEndRequested()) {
        winApp->ProcessMessage();

        ImGui_ImplDX12_NewFrame(); ImGui_ImplWin32_NewFrame(); ImGui::NewFrame();
        ImGui::Begin("Particle Settings");
        ImGui::DragFloat3("Field Center", &field.position.x, 0.1f);
        ImGui::DragFloat("Attraction Power", &field.strength, 0.1f, 0.0f, 50.0f);
        ImGui::DragFloat("Vortex Speed", &field.rotation, 0.1f, 0.0f, 30.0f);
        ImGui::DragFloat("Emitter Radius", &emitterRadius, 0.1f, 0.1f, 10.0f);
        ImGui::Checkbox("Glow Effect (Add Blend)", &useBlendAdd);
        ImGui::End();

        Matrix4x4 cameraMatrix = MakeAffineMatrix(cameraTransform.scale, cameraTransform.rotate, cameraTransform.translate);
        Matrix4x4 viewMatrix = Inverse(cameraMatrix);
        Matrix4x4 projectionMatrix = MakePerspectiveFovMatrix(0.45f, (float)winApp->kClientWidth / (float)winApp->kClientHeight, 1.0f, 1000.0f);
        Matrix4x4 viewProjectionMatrix = Multiply(viewMatrix, projectionMatrix);

        // ★ビルボード行列の作成（カメラの回転を反映＝追尾機能）
        Matrix4x4 billboardMatrix = MakeIdentity4x4();
        billboardMatrix.m[0][0] = cameraMatrix.m[0][0]; billboardMatrix.m[0][1] = cameraMatrix.m[0][1]; billboardMatrix.m[0][2] = cameraMatrix.m[0][2];
        billboardMatrix.m[1][0] = cameraMatrix.m[1][0]; billboardMatrix.m[1][1] = cameraMatrix.m[1][1]; billboardMatrix.m[1][2] = cameraMatrix.m[1][2];
        billboardMatrix.m[2][0] = cameraMatrix.m[2][0]; billboardMatrix.m[2][1] = cameraMatrix.m[2][1]; billboardMatrix.m[2][2] = cameraMatrix.m[2][2];

        for (uint32_t i = 0; i < kNumInstances; ++i) {
            if (particles[i].currentTime >= particles[i].lifeTime) {
                particles[i] = MakeNewParticle(randomEngine, field.position, emitterRadius);
            }

            Vector3 diff = {
                field.position.x - particles[i].transform.translate.x,
                field.position.y - particles[i].transform.translate.y,
                field.position.z - particles[i].transform.translate.z
            };
            float dist = Length(diff);
            Vector3 direction = (dist == 0.0f) ? Vector3{ 0,0,0 } : Normalize(diff);

            Vector3 attractionForce = { direction.x * field.strength, direction.y * field.strength, direction.z * field.strength };
            Vector3 up = { 0.0f, 1.0f, 0.0f };
            Vector3 tangent = Cross(direction, up);
            Vector3 rotationForce = { tangent.x * field.rotation, tangent.y * field.rotation, tangent.z * field.rotation };

            particles[i].velocity.x += (attractionForce.x + rotationForce.x) * kDeltaTime;
            particles[i].velocity.y += (attractionForce.y + rotationForce.y) * kDeltaTime;
            particles[i].velocity.z += (attractionForce.z + rotationForce.z) * kDeltaTime;

            particles[i].velocity.x *= 0.90f;
            particles[i].velocity.y *= 0.90f;
            particles[i].velocity.z *= 0.90f;

            particles[i].transform.translate.x += particles[i].velocity.x * kDeltaTime;
            particles[i].transform.translate.y += particles[i].velocity.y * kDeltaTime;
            particles[i].transform.translate.z += particles[i].velocity.z * kDeltaTime;

            // ★修正: ここにあった「範囲外判定で消す」という余計なコードも削除しました。

            particles[i].currentTime += kDeltaTime;
            float lifeRatio = particles[i].currentTime / particles[i].lifeTime;

            float alpha = 1.0f;
            if (lifeRatio < 0.2f) alpha = lifeRatio / 0.2f;
            else if (lifeRatio > 0.8f) alpha = 1.0f - ((lifeRatio - 0.8f) / 0.2f);
            particles[i].color.w = alpha;

            float hue = std::fmod((lifeRatio * 360.0f * 2.0f) + 180.0f, 360.0f);
            particles[i].color = HSVtoRGB(hue, 1.0f, 1.0f, alpha);

            Matrix4x4 worldMatrix = MakeAffineMatrix(particles[i].transform.scale, { 0,0,0 }, particles[i].transform.translate);
            // ★ビルボード（追尾）処理の適用
            worldMatrix = Multiply(billboardMatrix, worldMatrix);

            instancingData[i].World = worldMatrix;
            instancingData[i].WVP = Multiply(worldMatrix, viewProjectionMatrix);
            instancingData[i].color = particles[i].color;
        }

        dxCommon->PreDraw();
        commandList->SetGraphicsRootSignature(graphicsPipeline->GetRootSignature());
        ID3D12DescriptorHeap* descriptorHeaps[] = { srvDescriptorHeap.Get() };
        commandList->SetDescriptorHeaps(1, descriptorHeaps);
        commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        commandList->SetPipelineState(graphicsPipeline->GetPipelineState(useBlendAdd ? kBlendModeAdd : kBlendModeNormal));

        particleModel->Draw(commandList, kNumInstances, textureSrvHandleGPU, instancingSrvHandleGPU);

        ImGui::Render();
        ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), commandList);
        dxCommon->PostDraw();
    }

    ImGui_ImplDX12_Shutdown(); ImGui_ImplWin32_Shutdown(); ImGui::DestroyContext();
    delete particleModel; delete graphicsPipeline;
    dxCommon->Finalize(); CoUninitialize(); winApp->Finalize();
    return 0;
}