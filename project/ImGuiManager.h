#pragma once

#include <Windows.h>
#include <d3d12.h>
#include <wrl.h>

// 前方宣言
class WinApp;
class DirectXCommon;

// 修正: _DEBUG ではなく !NDEBUG を使用
// NDEBUG は Release ビルドで標準的に定義されるマクロです。
// "NDEBUG が定義されていない場合" = "Debug ビルド" とみなします。
#ifndef NDEBUG
#define USE_IMGUI
#endif

// ImGuiが有効な場合のみインクルード
#ifdef USE_IMGUI
#include "externals/imgui/imgui.h"
#include "externals/imgui/imgui_impl_dx12.h"
#include "externals/imgui/imgui_impl_win32.h"
#endif

class ImGuiManager {
public:
    ImGuiManager() = default;
    ~ImGuiManager() = default;

    void Initialize(WinApp* winApp, DirectXCommon* dxCommon);
    void NewFrame();
    void Draw(ID3D12GraphicsCommandList* commandList);
    void Shutdown();

private:
#ifdef USE_IMGUI
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> srvHeap_;
#endif
};