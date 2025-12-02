#pragma once
#include <d3d12.h>
#include <wrl.h>
#include <string>
#include <vector>
#include <iostream> // std::ostream用
#include "externals/DirectXTex/DirectXTex.h"
#include "externals/DirectXTex/d3dx12.h"

// ==========================================
// ★ヘルパー関数宣言 (文字列変換・ログ)
// ==========================================
std::wstring ConvertString(const std::string& str);
std::string ConvertString(const std::wstring& str);
void Log(const std::string& message);
void Log(std::ostream& os, const std::string& message);

// ==========================================
// DirectX12 リソース作成ヘルパー
// ==========================================

// バッファリソース作成
Microsoft::WRL::ComPtr<ID3D12Resource> CreateBufferResource(ID3D12Device* device, size_t sizeInBytes);

// ディスクリプタヒープ作成
Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> CreateDescriptorHeap(ID3D12Device* device, D3D12_DESCRIPTOR_HEAP_TYPE heapType, UINT numDescriptors, bool shaderVisible);

// Textureリソース作成
Microsoft::WRL::ComPtr<ID3D12Resource> CreateTextureResource(ID3D12Device* device, const DirectX::TexMetadata& metadata);

// 深度ステンシルTextureリソース作成
Microsoft::WRL::ComPtr<ID3D12Resource> CreateDepthStencilTextureResource(ID3D12Device* device, int32_t width, int32_t height);

// Textureデータをアップロード
[[nodiscard]]
Microsoft::WRL::ComPtr<ID3D12Resource> UploadTextureData(ID3D12Resource* texture, const DirectX::ScratchImage& mipImages, ID3D12Device* device, ID3D12GraphicsCommandList* commandList);

// Texture読み込み
DirectX::ScratchImage LoadTexture(const std::string& filePath);

// ディスクリプタハンドルの取得
D3D12_CPU_DESCRIPTOR_HANDLE GetCPUDescriptorHandle(ID3D12DescriptorHeap* descriptorHeap, uint32_t descriptorSize, uint32_t index);
D3D12_GPU_DESCRIPTOR_HANDLE GetGPUDescriptorHandle(ID3D12DescriptorHeap* descriptorHeap, uint32_t descriptorSize, uint32_t index);