#include "TextureManager.h"
#include "DirectXCommon.h"
#include "D3D12Util.h"
#include <cassert>
#include <vector>


TextureManager* TextureManager::GetInstance() {
    static TextureManager instance; 
    return &instance;
}

void TextureManager::Initialize(ID3D12Device* device, std::string directoryPath) {
    device_ = device;
    directoryPath_ = directoryPath;
    descriptorSizeSRV_ = device_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    D3D12_DESCRIPTOR_HEAP_DESC descHeapDesc = {};
    descHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    descHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    descHeapDesc.NumDescriptors = 1024;
    device_->CreateDescriptorHeap(&descHeapDesc, IID_PPV_ARGS(&srvHeap_));
}

void TextureManager::LoadTexture(const std::string& fileName) {
    // 既に読み込み済みなら早期リターン
    if (textureDatas_.contains(fileName)) {
        return;
    }

    // 1. ファイル読み込み
    // ★修正点: 読み込みパスをログに出して確認しやすくする
    std::string fullPath = directoryPath_ + fileName;
    DirectX::ScratchImage mipImages = ::LoadTexture(fullPath);

    // ★修正点: ファイルが見つからなかった場合のエラーチェックを追加
    if (mipImages.GetImageCount() == 0) {
        // ここで止まる場合、ファイルパスが間違っています。
        // "Resources" フォルダが exe と同じ場所にあるか確認してください。
        assert(false && "Texture File Not Found! Check the path.");
        return;
    }

    const DirectX::TexMetadata& metadata = mipImages.GetMetadata();
    Microsoft::WRL::ComPtr<ID3D12Resource> textureResource = ::CreateTextureResource(device_, metadata);

    // ★修正点: リソース作成失敗時のチェック
    if (!textureResource) {
        assert(false && "Failed to create texture resource.");
        return;
    }

    // 2. データ転送
    DirectXCommon* dxCommon = DirectXCommon::GetInstance();
    ID3D12GraphicsCommandList* commandList = dxCommon->GetCommandList();

    // データ転送用の一時リソース (ここで textureResource が NULL だとクラッシュしていた)
    Microsoft::WRL::ComPtr<ID3D12Resource> intermediateResource =
        ::UploadTextureData(textureResource.Get(), mipImages, device_, commandList);

    // 3. リソースステート変更
    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
    barrier.Transition.pResource = textureResource.Get();
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    commandList->ResourceBarrier(1, &barrier);

    // 4. コマンド実行と待機 (安全に転送を完了させる)
    commandList->Close();

    ID3D12CommandQueue* commandQueue = dxCommon->GetCommandQueue();
    ID3D12CommandList* commandLists[] = { commandList };
    commandQueue->ExecuteCommandLists(1, commandLists);

    // フェンスで待機
    Microsoft::WRL::ComPtr<ID3D12Fence> fence;
    device_->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence));
    HANDLE fenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);

    commandQueue->Signal(fence.Get(), 1);
    if (fence->GetCompletedValue() < 1) {
        fence->SetEventOnCompletion(1, fenceEvent);
        WaitForSingleObject(fenceEvent, INFINITE);
    }
    CloseHandle(fenceEvent);

    // コマンドリストのリセット
    ID3D12CommandAllocator* allocator = dxCommon->GetCommandAllocator();
    allocator->Reset();
    commandList->Reset(allocator, nullptr);


    // 5. SRV作成
    D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle = GetCPUDescriptorHandle(srvHeap_.Get(), descriptorSizeSRV_, useDescriptorIndex_);
    D3D12_GPU_DESCRIPTOR_HANDLE gpuHandle = GetGPUDescriptorHandle(srvHeap_.Get(), descriptorSizeSRV_, useDescriptorIndex_);
    useDescriptorIndex_++;

    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
    srvDesc.Format = metadata.format;
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MipLevels = UINT(metadata.mipLevels);
    device_->CreateShaderResourceView(textureResource.Get(), &srvDesc, cpuHandle);

    // 6. データ保存
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