#include "ImGuiManager.h"
#include "WinApp.h"
#include "DirectXCommon.h"

void ImGuiManager::Initialize(WinApp* winApp, DirectXCommon* dxCommon) {
#ifdef USE_IMGUI
    D3D12_DESCRIPTOR_HEAP_DESC desc = {};
    desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    desc.NumDescriptors = 1;
    desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;

    HRESULT result = dxCommon->GetDevice()->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&srvHeap_));
    assert(SUCCEEDED(result));

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::StyleColorsDark();

    ImGui_ImplWin32_Init(winApp->GetHwnd());
    ImGui_ImplDX12_Init(
        dxCommon->GetDevice(),
        dxCommon->GetBackBufferCount(),
        DXGI_FORMAT_R8G8B8A8_UNORM,
        srvHeap_.Get(),
        srvHeap_->GetCPUDescriptorHandleForHeapStart(),
        srvHeap_->GetGPUDescriptorHandleForHeapStart()
    );
#else
    // Release時は変数を使わないため警告回避
    (void)winApp;
    (void)dxCommon;
#endif
}

void ImGuiManager::NewFrame() {
#ifdef USE_IMGUI
    ImGui_ImplDX12_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();
#endif
}

void ImGuiManager::Draw(ID3D12GraphicsCommandList* commandList) {
#ifdef USE_IMGUI
    // ★修正: ここに Render を移動
    ImGui::Render();

    ID3D12DescriptorHeap* descriptorHeaps[] = { srvHeap_.Get() };
    commandList->SetDescriptorHeaps(1, descriptorHeaps);
    ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), commandList);
#else
    (void)commandList;
#endif
}

void ImGuiManager::Shutdown() {
#ifdef USE_IMGUI
    ImGui_ImplDX12_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
    srvHeap_.Reset();
#endif
}