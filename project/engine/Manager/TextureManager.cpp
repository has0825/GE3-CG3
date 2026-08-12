#include "TextureManager.h"
#include "DirectXCommon.h"
#include "D3D12Util.h"
#include <cassert>
#include <vector>
#include <fstream>
#include <algorithm>
#include <cctype>
#include <Windows.h>
#include "SrvManager.h"
#include <sstream>

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
    if (device_) {
        return;
    }
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
        std::string errMsg = "Texture File Not Found or Load Failed! Path: " + fullPath + "\n";
        OutputDebugStringA(errMsg.c_str());
        MessageBoxA(nullptr, errMsg.c_str(), "TextureManager Error", MB_OK | MB_ICONERROR);
        assert(false);
        return;
    }

    // WICロード後のフォーマット変換 (グレースケール画像等が赤く描画される問題への対策)
    if (!fullPath.ends_with(".dds")) {
        if (image.GetMetadata().format != DXGI_FORMAT_R8G8B8A8_UNORM_SRGB) {
            DirectX::ScratchImage convertedImage;
            hr = DirectX::Convert(
                image.GetImages(), image.GetImageCount(), image.GetMetadata(),
                DXGI_FORMAT_R8G8B8A8_UNORM_SRGB, DirectX::TEX_FILTER_DEFAULT, DirectX::TEX_THRESHOLD_DEFAULT, convertedImage);
            if (SUCCEEDED(hr)) {
                image = std::move(convertedImage);
            }
        }
    }

    // 【ミップマップ生成前】切り抜き画像（isihahen.png）の透明〜半透明ピクセルのRGBを黒にクリアする。
    // これにより、ミップマップ生成（縮小フィルタ）時に透明ピクセルの不要な色（赤）が周囲の不透明ピクセルににじみ出るのを防ぐ。
    // パスは大文字小文字を区別しないように判定する。
    std::string fullPathLower = fullPath;
    std::transform(fullPathLower.begin(), fullPathLower.end(), fullPathLower.begin(), [](unsigned char c) { return std::tolower(c); });

    if (fullPathLower.find("isihahen") != std::string::npos) {
        const DirectX::Image* img = image.GetImage(0, 0, 0);
        if (img && img->pixels) {
            uint8_t* pixels = img->pixels;
            for (size_t y = 0; y < img->height; ++y) {
                uint8_t* row = pixels + y * img->rowPitch;
                for (size_t x = 0; x < img->width; ++x) {
                    uint8_t* pixel = row + x * 4;
                    // アルファが255未満かつ、色が赤に近いピクセル（赤フリンジ）を黒（透明）にクリア
                    if (pixel[3] < 255) {
                        if (pixel[0] > 150 && pixel[1] < 80 && pixel[2] < 80) {
                            pixel[0] = 0; // R
                            pixel[1] = 0; // G
                            pixel[2] = 0; // B
                            pixel[3] = 0; // Aも完全に透明にする
                        }
                    }
                }
            }
        }
    }

    // ミップマップ生成判定:
    // ・圧縮フォーマット（DXT等）はそのまま使う（生成不可）
    // ・既にミップマップが存在する場合はそのまま使う
    // ・非圧縮DDSかつミップマップなし → ミップマップを生成する（MipMapLODBiasによるぼかしを有効にするため）
    DirectX::ScratchImage mipImages{};
    bool isCompressed = DirectX::IsCompressed(image.GetMetadata().format);
    bool hasMips = image.GetMetadata().mipLevels > 1;
    bool tooSmall = image.GetMetadata().width < 4 || image.GetMetadata().height < 4;
    if (isCompressed || hasMips || tooSmall) {
        // 圧縮済み・ミップあり・小さすぎる場合はそのまま
        mipImages = std::move(image);
    } else {
        // 非圧縮でミップなし（新しいRGBA DDSを含む）→ ミップマップを生成する
        hr = DirectX::GenerateMipMaps(image.GetImages(), image.GetImageCount(), image.GetMetadata(), DirectX::TEX_FILTER_DEFAULT, 0, mipImages);
        if (FAILED(hr)) {
            // 生成失敗時はそのまま使う
            mipImages = std::move(image);
        }
    }

    // 【ミップマップ生成後】切り抜き画像（isihahen.png）の全ミップレベルで
    // 透明〜半透明ピクセルのRGBを黒にクリアする。
    if (fullPathLower.find("isihahen") != std::string::npos) {
        const DirectX::TexMetadata& meta = mipImages.GetMetadata();
        for (size_t mip = 0; mip < meta.mipLevels; ++mip) {
            // 2Dテクスチャは GetImage(mipLevel, arrayIndex=0, depth=0)
            const DirectX::Image* img = mipImages.GetImage(mip, 0, 0);
            if (!img || !img->pixels) continue;
            uint8_t* pixels = img->pixels;
            for (size_t y = 0; y < img->height; ++y) {
                uint8_t* row = pixels + y * img->rowPitch;
                for (size_t x = 0; x < img->width; ++x) {
                    uint8_t* pixel = row + x * 4;
                    // アルファが255未満かつ、色が赤に近いピクセル（赤フリンジ）を黒（透明）にクリア
                    if (pixel[3] < 255) {
                        if (pixel[0] > 150 && pixel[1] < 80 && pixel[2] < 80) {
                            pixel[0] = 0; // R
                            pixel[1] = 0; // G
                            pixel[2] = 0; // B
                            pixel[3] = 0; // Aも完全に透明にする
                        }
                    }
                }
            }
        }
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