#pragma once
#include <string>
#include <unordered_map>
#include <wrl.h>
#include <d3d12.h>
#include <vector>
#include "externals/DirectXTex/DirectXTex.h"

// テクスチャ管理クラス
class TextureManager {
public:
    // データ構造体
    struct TextureData {
        Microsoft::WRL::ComPtr<ID3D12Resource> resource;
        D3D12_CPU_DESCRIPTOR_HANDLE srvHandleCPU;
        D3D12_GPU_DESCRIPTOR_HANDLE srvHandleGPU;
        DirectX::TexMetadata metadata;
    };

    // シングルトンインスタンス取得
    static TextureManager* GetInstance();

    // 初期化
    void Initialize(ID3D12Device* device, std::string directoryPath = "Resources/");

    // テクスチャ読み込み
    void LoadTexture(const std::string& fileName);

    // SRVハンドル取得
    D3D12_GPU_DESCRIPTOR_HANDLE GetSrvHandleGPU(const std::string& fileName);

    // メタデータ取得
    const DirectX::TexMetadata& GetMetaData(const std::string& fileName);

    // ★追加: 外部リソース（インスタンシングバッファ等）のSRVを作成する関数
    D3D12_GPU_DESCRIPTOR_HANDLE CreateSRV(ID3D12Resource* resource, const D3D12_SHADER_RESOURCE_VIEW_DESC& srvDesc);

    // ★追加: 描画時にコマンドリストにセットするためにヒープを取得する関数
    ID3D12DescriptorHeap* GetSrvHeap() const { return srvHeap_.Get(); }

private:
    TextureManager() = default;
    ~TextureManager() = default;
    TextureManager(const TextureManager&) = delete;
    TextureManager& operator=(const TextureManager&) = delete;

private:
    // メンバ変数
    ID3D12Device* device_ = nullptr;
    std::string directoryPath_;

    // SRV用デスクリプタヒープ
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> srvHeap_;
    // ディスクリプタサイズ
    uint32_t descriptorSizeSRV_ = 0;
    // 次に使用するディスクリプタインデックス
    uint32_t useDescriptorIndex_ = 0;

    // テクスチャデータ一覧
    std::unordered_map<std::string, TextureData> textureDatas_;

    // 転送用の中間リソースリスト
    std::vector<Microsoft::WRL::ComPtr<ID3D12Resource>> intermediateResources_;
};