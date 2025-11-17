#pragma once
#include <string>
#include <unordered_map>
#include <d3d12.h>
#include <wrl.h>
#include "externals/DirectXTex/DirectXTex.h"

class TextureManager {
private:
    // シングルトンパターン
    static TextureManager* instance;
    TextureManager() = default;
    ~TextureManager() = default;

public:
    static TextureManager* GetInstance();

    void Initialize(ID3D12Device* device, std::string directoryPath = "resources/");

    // テクスチャ読み込み
    void LoadTexture(const std::string& fileName);

    // SRVハンドルの取得
    D3D12_GPU_DESCRIPTOR_HANDLE GetSrvHandleGPU(const std::string& fileName);

    // ★追加: SRVヒープを取得する関数
    ID3D12DescriptorHeap* GetSrvHeap() { return srvHeap_.Get(); }

    // メタデータ取得
    const DirectX::TexMetadata& GetMetaData(const std::string& fileName);

private:
    struct TextureData {
        Microsoft::WRL::ComPtr<ID3D12Resource> resource;
        D3D12_CPU_DESCRIPTOR_HANDLE srvHandleCPU;
        D3D12_GPU_DESCRIPTOR_HANDLE srvHandleGPU;
        DirectX::TexMetadata metadata;
    };

    ID3D12Device* device_ = nullptr;
    std::string directoryPath_;

    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> srvHeap_;
    uint32_t useDescriptorIndex_ = 0;
    uint32_t descriptorSizeSRV_ = 0;

    std::unordered_map<std::string, TextureData> textureDatas_;
};