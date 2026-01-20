#include "TextureManager.h"
#include "DirectXCommon.h"
#include "D3D12Util.h"
#include <cassert>

TextureManager* TextureManager::GetInstance() {
    static TextureManager instance;
    return &instance;
}

void TextureManager::Initialize(ID3D12Device* device, std::string directoryPath) {
    assert(device);
    device_ = device;
    directoryPath_ = directoryPath;

    // ディスクリプタサイズの取得
    descriptorSizeSRV_ = device_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    // SRV用ヒープの作成 (十分な数を確保)
    D3D12_DESCRIPTOR_HEAP_DESC descHeapDesc = {};
    descHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    descHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    descHeapDesc.NumDescriptors = 1024;
    HRESULT hr = device_->CreateDescriptorHeap(&descHeapDesc, IID_PPV_ARGS(&srvHeap_));
    assert(SUCCEEDED(hr));
}

// ★追加: 外部リソース用SRV作成実装
D3D12_GPU_DESCRIPTOR_HANDLE TextureManager::CreateSRV(ID3D12Resource* resource, const D3D12_SHADER_RESOURCE_VIEW_DESC& srvDesc) {
    // 空いている場所のハンドルを取得
    D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle = GetCPUDescriptorHandle(srvHeap_.Get(), descriptorSizeSRV_, useDescriptorIndex_);
    D3D12_GPU_DESCRIPTOR_HANDLE gpuHandle = GetGPUDescriptorHandle(srvHeap_.Get(), descriptorSizeSRV_, useDescriptorIndex_);

    // インデックスを進める
    useDescriptorIndex_++;

    // デバイスでSRV作成
    device_->CreateShaderResourceView(resource, &srvDesc, cpuHandle);

    return gpuHandle;
}

void TextureManager::LoadTexture(const std::string& fileName) {
    if (textureDatas_.contains(fileName)) {
        return;
    }

    assert(device_ && "TextureManager::Initializeが呼ばれていません");

    std::string fullPath = directoryPath_ + fileName;
    DirectX::ScratchImage mipImages = ::LoadTexture(fullPath);

    if (mipImages.GetImageCount() == 0) {
        std::string msg = "[Error] Texture file not found: " + fullPath + "\n";
        OutputDebugStringA(msg.c_str());
        assert(false && "Texture file not found!");
        return;
    }

    const DirectX::TexMetadata& metadata = mipImages.GetMetadata();
    Microsoft::WRL::ComPtr<ID3D12Resource> textureResource = CreateTextureResource(device_, metadata);
    if (!textureResource) {
        assert(false && "Failed to create texture resource.");
        return;
    }

    ID3D12GraphicsCommandList* commandList = DirectXCommon::GetInstance()->GetCommandList();

    Microsoft::WRL::ComPtr<ID3D12Resource> intermediateResource = UploadTextureData(textureResource.Get(), mipImages, device_, commandList);
    if (intermediateResource) {
        intermediateResources_.push_back(intermediateResource);
    }

    // SRV作成 (LoadTexture内ではCPUハンドルも保存するため、CreateSRVを呼ばずに直接記述)
    D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle = GetCPUDescriptorHandle(srvHeap_.Get(), descriptorSizeSRV_, useDescriptorIndex_);
    D3D12_GPU_DESCRIPTOR_HANDLE gpuHandle = GetGPUDescriptorHandle(srvHeap_.Get(), descriptorSizeSRV_, useDescriptorIndex_);
    useDescriptorIndex_++;

    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
    srvDesc.Format = metadata.format;
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MipLevels = UINT(metadata.mipLevels);

    device_->CreateShaderResourceView(textureResource.Get(), &srvDesc, cpuHandle);

    TextureData& data = textureDatas_[fileName];
    data.resource = textureResource;
    data.srvHandleCPU = cpuHandle;
    data.srvHandleGPU = gpuHandle;
    data.metadata = metadata;
}

D3D12_GPU_DESCRIPTOR_HANDLE TextureManager::GetSrvHandleGPU(const std::string& fileName) {
    if (!textureDatas_.contains(fileName)) {
        LoadTexture(fileName);
    }
    assert(textureDatas_.contains(fileName));
    return textureDatas_[fileName].srvHandleGPU;
}

const DirectX::TexMetadata& TextureManager::GetMetaData(const std::string& fileName) {
    if (!textureDatas_.contains(fileName)) {
        LoadTexture(fileName);
    }
    assert(textureDatas_.contains(fileName));
    return textureDatas_[fileName].metadata;
}