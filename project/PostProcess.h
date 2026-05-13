#pragma once
#include <d3d12.h>
#include <wrl.h>
#include <cstdint>
#include "engine/Math/MathTypes.h"

class DirectXCommon;

class PostProcess {
public:
    void Initialize(DirectXCommon* dxCommon, uint32_t width, uint32_t height);
    
    // 描画先をRenderTextureに切り替える
    void PreDraw();
    
    // 描画先を戻す
    void PostDraw();
    
    // RenderTextureをSRVとして取得
    uint32_t GetSrvIndex() const { return srvIndex_; }
    ID3D12Resource* GetResource() const { return resource_.Get(); }

private:
    DirectXCommon* dxCommon_ = nullptr;
    Microsoft::WRL::ComPtr<ID3D12Resource> resource_ = nullptr;
    D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle_{};
    uint32_t srvIndex_ = 0;
};
