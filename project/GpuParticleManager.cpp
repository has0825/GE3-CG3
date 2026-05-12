#include "GpuParticle.h"
#include "DirectXCommon.h"
#include "D3D12Util.h"
#include "SrvManager.h"
#include "GraphicsPipeline.h"
#include <cassert>

void GpuParticleManager::Initialize(ID3D12Device* device) {
    device_ = device;
    CreateResource();
    CreateSrvUav();

    // 初期化用CSの実行
    ID3D12GraphicsCommandList* commandList = DirectXCommon::GetInstance()->GetCommandList();
    GraphicsPipeline* graphicsPipeline = GraphicsPipeline::GetInstance();

    commandList->SetComputeRootSignature(graphicsPipeline->GetGpuParticleInitializeRootSignature());
    commandList->SetPipelineState(graphicsPipeline->GetGpuParticleInitializePipelineState());

    // UAVデスクリプタテーブルをセット
    SrvManager::GetInstance()->PreDraw();
    SrvManager::GetInstance()->SetComputeRootDescriptorTable(0, uavIndex_);

    // Dispatch (1, 1, 1) - 1024個なので
    commandList->Dispatch(1, 1, 1);

    // UAVバリア (念のため)
    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
    barrier.UAV.pResource = particleResource_.Get();
    commandList->ResourceBarrier(1, &barrier);
}

void GpuParticleManager::Update(const Matrix4x4& viewProjection, const Matrix4x4& billboardMatrix) {
    perViewData_->viewProjection = viewProjection;
    perViewData_->billboardMatrix = billboardMatrix;
}

void GpuParticleManager::Draw(ID3D12GraphicsCommandList* commandList, D3D12_GPU_DESCRIPTOR_HANDLE textureHandle) {
    GraphicsPipeline* graphicsPipeline = GraphicsPipeline::GetInstance();

    commandList->SetGraphicsRootSignature(graphicsPipeline->GetGpuParticleRootSignature());
    commandList->SetPipelineState(graphicsPipeline->GetGpuParticlePipelineState());

    // [0] PerView (b0)
    commandList->SetGraphicsRootConstantBufferView(0, perViewResource_->GetGPUVirtualAddress());

    // [1] gParticles (t0)
    SrvManager::GetInstance()->SetGraphicsRootDescriptorTable(1, srvIndex_);

    // [2] Texture (t1)
    commandList->SetGraphicsRootDescriptorTable(2, textureHandle);

    // 描画コマンド (板ポリゴン 6頂点, 1024インスタンス)
    commandList->DrawInstanced(6, kMaxParticles, 0, 0);
}

void GpuParticleManager::CreateResource() {
    // パーティクルリソース (DEFAULT heap)
    particleResource_ = CreateUAVBufferResource(device_, sizeof(ParticleCS) * kMaxParticles);
    
    // PerViewリソース (UPLOAD heap)
    perViewResource_ = CreateBufferResource(device_, sizeof(PerView));
    perViewResource_->Map(0, nullptr, reinterpret_cast<void**>(&perViewData_));
}

void GpuParticleManager::CreateSrvUav() {
    SrvManager* srvManager = SrvManager::GetInstance();
    
    // SRV確保
    srvIndex_ = srvManager->Allocate();
    srvManager->CreateSRVforStructuredBuffer(srvIndex_, particleResource_.Get(), kMaxParticles, sizeof(ParticleCS));
    srvHandleGPU_ = srvManager->GetGPUDescriptorHandle(srvIndex_);

    // UAV確保
    uavIndex_ = srvManager->Allocate();
    srvManager->CreateUAVforStructuredBuffer(uavIndex_, particleResource_.Get(), kMaxParticles, sizeof(ParticleCS));
    uavHandleGPU_ = srvManager->GetGPUDescriptorHandle(uavIndex_);
}
