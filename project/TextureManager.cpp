#include "TextureManager.h"
#include "DirectXCommon.h"
#include "D3D12Util.h"
#include <cassert>
#include <vector>
#include <Windows.h>
#include "SrvManager.h"

// stringからwstringへの変換ヘルパー
static std::wstring ConvertString(const std::string& str) {
    if (str.empty()) return std::wstring();
    int sizeNeeded = MultiByteToWideChar(CP_UTF8, 0, reinterpret_cast<const char*>(&str[0]), (int)str.size(), NULL, 0);
    std::wstring result(sizeNeeded, 0);
    MultiByteToWideChar(CP_UTF8, 0, reinterpret_cast<const char*>(&str[0]), (int)str.size(), &result[0], sizeNeeded);
    return result;
}

TextureManager* TextureManager::GetInstance() {
    static TextureManager instance;
    return &instance;
}

void TextureManager::Initialize(ID3D12Device* device, std::string directoryPath) {
    device_ = device;
    directoryPath_ = directoryPath;
    descriptorSizeSRV_ = device_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    useDescriptorIndex_ = 0;
    textureDatas_.clear();

    // SrvManagerを使用するため、独自のヒープ生成は削除
    srvHeap_ = SrvManager::GetInstance()->GetDescriptorHeap();

    // コマンドリストの初期化
    device_->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&commandAllocator_));
    device_->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, commandAllocator_.Get(), nullptr, IID_PPV_ARGS(&commandList_));
}

uint32_t TextureManager::Allocate() {
    return SrvManager::GetInstance()->Allocate();
}

void TextureManager::LoadTexture(const std::string& fileName) {
    if (textureDatas_.contains(fileName)) {
        return;
    }

    std::string fullPath = directoryPath_ + fileName;
    std::wstring filePathW = ConvertString(fullPath);

    DirectX::ScratchImage image;
    HRESULT hr;

    // 拡張子で読み込みを分岐 (DDS対応)
    if (fullPath.ends_with(".dds")) {
        hr = DirectX::LoadFromDDSFile(filePathW.c_str(), DirectX::DDS_FLAGS_NONE, nullptr, image);
    } else {
        hr = DirectX::LoadFromWICFile(filePathW.c_str(), DirectX::WIC_FLAGS_FORCE_SRGB, nullptr, image);
    }

    if (FAILED(hr)) {
        assert(false && "Texture File Not Found or Load Failed! Check the path.");
        return;
    }

    // 圧縮フォーマットならそのまま、そうでないならMipMap生成
    DirectX::ScratchImage mipImages{};
    if (DirectX::IsCompressed(image.GetMetadata().format)) {
        mipImages = std::move(image);
    } else {
        hr = DirectX::GenerateMipMaps(image.GetImages(), image.GetImageCount(), image.GetMetadata(), DirectX::TEX_FILTER_SRGB, 4, mipImages);
        assert(SUCCEEDED(hr));
    }

    const DirectX::TexMetadata& metadata = mipImages.GetMetadata();
    Microsoft::WRL::ComPtr<ID3D12Resource> textureResource = ::CreateTextureResource(device_, metadata);

    if (!textureResource) {
        assert(false && "Failed to create texture resource.");
        return;
    }

    DirectXCommon* dxCommon = DirectXCommon::GetInstance();
    
    // UploadTextureData が内部で自己完結してアップロード・待機まで行う
    Microsoft::WRL::ComPtr<ID3D12Resource> intermediateResource =
        ::UploadTextureData(textureResource.Get(), mipImages, device_, nullptr);



    uint32_t srvIndex = Allocate();
    D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle = SrvManager::GetInstance()->GetCPUDescriptorHandle(srvIndex);
    D3D12_GPU_DESCRIPTOR_HANDLE gpuHandle = SrvManager::GetInstance()->GetGPUDescriptorHandle(srvIndex);

    // SrvManagerを使用してSRVを作成
    if (metadata.IsCubemap()) {
        SrvManager::GetInstance()->CreateSRVforTextureCube(srvIndex, textureResource.Get(), metadata.format, UINT(metadata.mipLevels));
    } else {
        SrvManager::GetInstance()->CreateSRVforTexture2D(srvIndex, textureResource.Get(), metadata.format, UINT(metadata.mipLevels));
    }

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
    return textureDatas_[fileName].srvHandleGPU;
}

const DirectX::TexMetadata& TextureManager::GetMetaData(const std::string& fileName) {
    if (!textureDatas_.contains(fileName)) {
        LoadTexture(fileName);
    }
    return textureDatas_[fileName].metadata;
}