#pragma once
#include <d3d12.h>
#include <cstdint> // uint32_t のために必須

// 前方宣言
class WinApp;
class DirectXCommon;

class ImGuiManager {
public:
    static ImGuiManager* GetInstance();

    // 引数名はスニペットに合わせる（型はプロジェクトの WinApp / DirectXCommon）
    void Initialize(WinApp* winAPI, DirectXCommon* dxBase);
    void Finalize();
    void Begin();
    void End();
    void Draw();

private:
    ImGuiManager() = default;
    ~ImGuiManager() = default;
    ImGuiManager(const ImGuiManager&) = delete;
    ImGuiManager& operator=(const ImGuiManager&) = delete;

    // ご提示のスニペット通りのメンバ変数
    WinApp* winAPI_ = nullptr;
    DirectXCommon* dxBase_ = nullptr;
    ID3D12DescriptorHeap* srvHeap_ = nullptr;
    uint32_t srvIndex_ = 0;
    D3D12_CPU_DESCRIPTOR_HANDLE srvHandleCPU_;
    D3D12_GPU_DESCRIPTOR_HANDLE srvHandleGPU_;
};