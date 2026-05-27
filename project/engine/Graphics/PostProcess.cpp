#include "PostProcess.h"
#include "DirectXCommon.h"
#include "D3D12Util.h"
#include "SrvManager.h"
#include <cassert>

void PostProcess::Initialize(DirectXCommon* dxCommon, uint32_t width, uint32_t height) {
    dxCommon_ = dxCommon;
    
    // RenderTextureリソース作成 (資料通りSRGBあり)
    Vector4 clearColor = { 1.0f, 0.0f, 0.0f, 1.0f }; // 資料通り一旦赤
    resource_ = CreateRenderTextureResource(dxCommon_->GetDevice(), width, height, DXGI_FORMAT_R8G8B8A8_UNORM_SRGB, clearColor);
    
    // RTVの作成
    rtvHandle_ = dxCommon_->GetRenderTextureRtvHandle();
    D3D12_RENDER_TARGET_VIEW_DESC rtvDesc{};
    rtvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
    rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
    dxCommon_->GetDevice()->CreateRenderTargetView(resource_.Get(), &rtvDesc, rtvHandle_);
    
    // SRVの作成 (SrvManagerを使用)
    srvIndex_ = SrvManager::GetInstance()->Allocate();
    SrvManager::GetInstance()->CreateSRVforTexture2D(srvIndex_, resource_.Get(), DXGI_FORMAT_R8G8B8A8_UNORM_SRGB, 1);

    // 初期状態をPIXEL_SHADER_RESOURCEにしておく
    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
    barrier.Transition.pResource = resource_.Get();
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    dxCommon_->GetCommandList()->ResourceBarrier(1, &barrier);
}

void PostProcess::PreDraw() {
    ID3D12GraphicsCommandList* commandList = dxCommon_->GetCommandList();

    // RENDER_TARGET状態へ遷移
    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
    barrier.Transition.pResource = resource_.Get();
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
    commandList->ResourceBarrier(1, &barrier);
    
    // レンダーターゲットのセット
    D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle = dxCommon_->GetDsvHandle();
    commandList->OMSetRenderTargets(1, &rtvHandle_, false, &dsvHandle);

    // クリア
    float clearColor[] = { 1.0f, 0.0f, 0.0f, 1.0f };
    commandList->ClearRenderTargetView(rtvHandle_, clearColor, 0, nullptr);
    commandList->ClearDepthStencilView(dsvHandle, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);
}

void PostProcess::PostDraw() {
    ID3D12GraphicsCommandList* commandList = dxCommon_->GetCommandList();

    // 深度バッファをPIXEL_SHADER_RESOURCE状態へ安全に遷移（重複バリアをスキップ）
    dxCommon_->TransitionDepthStencilState(D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

    // PIXEL_SHADER_RESOURCE状態へ遷移
    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
    barrier.Transition.pResource = resource_.Get();
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    commandList->ResourceBarrier(1, &barrier);
}
