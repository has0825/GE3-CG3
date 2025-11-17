#include "TextureManager.h"
#include "D3D12Util.h" // あなたの環境にある便利関数を使う前提
#include <cassert>

TextureManager* TextureManager::instance = nullptr;

TextureManager* TextureManager::GetInstance() {
    if (instance == nullptr) {
        instance = new TextureManager();
    }
    return instance;
}

void TextureManager::Initialize(ID3D12Device* device, std::string directoryPath) {
    device_ = device;
    directoryPath_ = directoryPath;
    descriptorSizeSRV_ = device_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    // SRVヒープの作成 (最大数は適当に設定)
    D3D12_DESCRIPTOR_HEAP_DESC descHeapDesc = {};
    descHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    descHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    descHeapDesc.NumDescriptors = 1024;
    device_->CreateDescriptorHeap(&descHeapDesc, IID_PPV_ARGS(&srvHeap_));
}

void TextureManager::LoadTexture(const std::string& fileName) {
    // 既に読み込み済みなら早期リターン（二重読み込み防止）
    if (textureDatas_.contains(fileName)) {
        return;
    }

    // --- テクスチャ読み込み処理 (main.cppにあった処理を移植) ---
    DirectX::ScratchImage mipImages = ::LoadTexture(directoryPath_ + fileName); // グローバルのLoadTextureを利用
    const DirectX::TexMetadata& metadata = mipImages.GetMetadata();
    Microsoft::WRL::ComPtr<ID3D12Resource> textureResource = ::CreateTextureResource(device_, metadata);

    // データ転送 (uploadResourceはローカルで破棄してOKだが、コマンドリスト実行までは待つ必要あり。ここでは簡略化)
    // ※本来はUploadTextureData内でCommandListを実行・待機するか、UploadResourceを保持する必要がある
    // ここでは既存の関数の仕組みに準拠します
    // 実装注意: UploadTextureDataの戻り値(IntermediateResource)はコマンドリスト実行完了まで保持が必要

    // ハンドル計算
    D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle = GetCPUDescriptorHandle(srvHeap_.Get(), descriptorSizeSRV_, useDescriptorIndex_);
    D3D12_GPU_DESCRIPTOR_HANDLE gpuHandle = GetGPUDescriptorHandle(srvHeap_.Get(), descriptorSizeSRV_, useDescriptorIndex_);
    useDescriptorIndex_++;

    // SRV作成
    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
    srvDesc.Format = metadata.format;
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MipLevels = UINT(metadata.mipLevels);
    device_->CreateShaderResourceView(textureResource.Get(), &srvDesc, cpuHandle);

    // データ保存
    TextureData& data = textureDatas_[fileName];
    data.resource = textureResource;
    data.srvHandleCPU = cpuHandle;
    data.srvHandleGPU = gpuHandle;
    data.metadata = metadata;

    // 転送用リソースの管理はWinApp等のコマンドリスト実行タイミングに依存するため、
    // ここでは実際の転送処理実装は省略していますが、main.cppのロジックをここに持ってきます。
}

D3D12_GPU_DESCRIPTOR_HANDLE TextureManager::GetSrvHandleGPU(const std::string& fileName) {
    assert(textureDatas_.contains(fileName));
    return textureDatas_[fileName].srvHandleGPU;
}

const DirectX::TexMetadata& TextureManager::GetMetaData(const std::string& fileName) {
    assert(textureDatas_.contains(fileName));
    return textureDatas_[fileName].metadata;
}