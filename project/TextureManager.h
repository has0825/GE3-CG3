#pragma once
#include <string>
#include <unordered_map>
#include <d3d12.h>
#include <wrl.h>
#include "externals/DirectXTex/DirectXTex.h"

class TextureManager {
public:
    // シングルトン
    static TextureManager* GetInstance();

    void Initialize(ID3D12Device* device, std::string directoryPath = "resources/");

    void LoadTexture(const std::string& fileName);
    D3D12_GPU_DESCRIPTOR_HANDLE GetSrvHandleGPU(const std::string& fileName);
    ID3D12DescriptorHeap* GetSrvHeap() { return srvHeap_.Get(); }
    const DirectX::TexMetadata& GetMetaData(const std::string& fileName);

private:
    // シングルトンのための非公開化
    TextureManager() = default;
    ~TextureManager() = default;
    TextureManager(const TextureManager&) = delete;
    TextureManager& operator=(const TextureManager&) = delete;

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

    // テクスチャ読み込み用コマンドリスト
    Microsoft::WRL::ComPtr<ID3D12CommandAllocator> commandAllocator_;
    Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> commandList_;
};