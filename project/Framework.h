#pragma once
#include <Windows.h>
#include <wrl/client.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <memory>
#include <string>

#include "WinApp.h"
#include "DirectXCommon.h"
#include "Audio.h"
#include "GraphicsPipeline.h"
#include "D3D12Util.h"

// フレームワーククラス
class Framework {
public:
    virtual ~Framework() = default;

    // 実行（初期化、ループ、終了処理）
    void Run();

    // 継承先で実装・拡張する仮想関数
    virtual void Initialize();
    virtual void Finalize();
    virtual void Update();
    virtual void Draw();

    // 終了フラグのチェック
    virtual bool IsEndRequest();

protected:
    WinApp* winApp_ = nullptr;
    DirectXCommon* dxCommon_ = nullptr;
    Audio* audio_ = nullptr;
    GraphicsPipeline* graphicsPipeline_ = nullptr;

    // SRVヒープ（汎用的に使うためここに保持）
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> srvDescriptorHeap_;
    UINT descriptorSizeSRV_ = 0;

#ifdef _DEBUG
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> imguiDescriptorHeap_;
#endif
};