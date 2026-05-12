#pragma once
#include "DirectXCommon.h" // DirectXBaseがDirectXCommonを指していると想定
#include <d3d12.h>
#include <wrl.h>
#include <cstdint>
#include <assert.h>

class SrvManager {
public:
    template <class T> using ComPtr = Microsoft::WRL::ComPtr<T>;

    static SrvManager* GetInstance();
    void Finalize();
    void Initialize(DirectXCommon* dxCommon);

    // 描画前処理（ヒープのセット）
    void PreDraw();

    // 次の空きインデックスを確保
    uint32_t Allocate();

    // SRV生成（テクスチャ用）
    void CreateSRVforTexture2D(uint32_t srvIndex, ID3D12Resource* pResource, DXGI_FORMAT Format, UINT MipLevels);
    void CreateSRVforTextureCube(uint32_t srvIndex, ID3D12Resource* pResource, DXGI_FORMAT Format, UINT MipLevels);

    // SRV生成（Structured Buffer用）
    void CreateSRVforStructuredBuffer(uint32_t srvIndex, ID3D12Resource* pResource, UINT numElements, UINT structureByteStride);

    // UAV生成（Structured Buffer用）
    void CreateUAVforStructuredBuffer(uint32_t srvIndex, ID3D12Resource* pResource, UINT numElements, UINT structureByteStride);

    // ハンドル取得
    D3D12_CPU_DESCRIPTOR_HANDLE GetCPUDescriptorHandle(uint32_t index);
    D3D12_GPU_DESCRIPTOR_HANDLE GetGPUDescriptorHandle(uint32_t index);

    // 描画コマンド発行
    void SetGraphicsRootDescriptorTable(UINT RootParameterIndex, uint32_t srvIndex);
    void SetComputeRootDescriptorTable(UINT RootParameterIndex, uint32_t srvIndex);

    ID3D12DescriptorHeap* GetDescriptorHeap() { return descriptorHeap_.Get(); }
    uint32_t GetDescriptorSize() const { return descriptorSize_; }

    static const uint32_t kMaxSRVCount;

private:
    SrvManager() = default;
    ~SrvManager() = default;
    SrvManager(const SrvManager&) = delete;
    SrvManager& operator=(const SrvManager&) = delete;

    DirectXCommon* dxCommon_ = nullptr;
    ComPtr<ID3D12DescriptorHeap> descriptorHeap_ = nullptr;
    uint32_t descriptorSize_ = 0;
    uint32_t useIndex_ = 0;
};