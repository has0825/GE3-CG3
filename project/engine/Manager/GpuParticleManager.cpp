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

    // UAVデスクリプタテーブルをセット (u0: particles, u1: freeListIndex, u2: freeList)
    SrvManager::GetInstance()->PreDraw();
    SrvManager::GetInstance()->SetComputeRootDescriptorTable(0, uavIndex_);

    // Dispatch (kMaxParticles / 1024, 1, 1) - (InitializeParticle.CS.hlslはnumthreads(1024, 1, 1))
    commandList->Dispatch(kMaxParticles / 1024, 1, 1);

    // UAVバリア
    D3D12_RESOURCE_BARRIER barriers[3] = {};
    barriers[0].Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
    barriers[0].UAV.pResource = particleResource_.Get();
    barriers[1].Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
    barriers[1].UAV.pResource = freeListIndexResource_.Get();
    barriers[2].Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
    barriers[2].UAV.pResource = freeListResource_.Get();
    commandList->ResourceBarrier(_countof(barriers), barriers);
}

void GpuParticleManager::Update(const Matrix4x4& viewProjection, const Matrix4x4& billboardMatrix, float deltaTime) {
    perViewData_->viewProjection = viewProjection;
    perViewData_->billboardMatrix = billboardMatrix;

    // 時間の更新
    time_ += deltaTime;
    perFrameData_->time = time_;
    perFrameData_->deltaTime = deltaTime;

    // Emitterの更新処理 (自動エミットは無効化)
    emitterData_->emit = 0;
}

void GpuParticleManager::Emit() {
    ID3D12GraphicsCommandList* commandList = DirectXCommon::GetInstance()->GetCommandList();
    GraphicsPipeline* graphicsPipeline = GraphicsPipeline::GetInstance();

    commandList->SetComputeRootSignature(graphicsPipeline->GetGpuParticleEmitRootSignature());
    commandList->SetPipelineState(graphicsPipeline->GetGpuParticleEmitPipelineState());

    // [0] u0, u1 (DescriptorTable)
    SrvManager::GetInstance()->SetComputeRootDescriptorTable(0, uavIndex_);
    // [1] b0 (Emitter)
    commandList->SetComputeRootConstantBufferView(1, emitterResource_->GetGPUVirtualAddress());
    // 実行前にUAV状態であることを保証（本来は状態管理が必要だが、簡易的に）
    // [2] b1 (PerFrame)
    commandList->SetComputeRootConstantBufferView(2, perFrameResource_->GetGPUVirtualAddress());

    commandList->Dispatch((emitterData_->count + 1023) / 1024, 1, 1);

    // UAVバリア (資料に基づき、次のUpdateCSで確実に読み込めるようにする)
    D3D12_RESOURCE_BARRIER barriers[3] = {};
    barriers[0].Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
    barriers[0].UAV.pResource = particleResource_.Get();
    barriers[1].Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
    barriers[1].UAV.pResource = freeListIndexResource_.Get();
    barriers[2].Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
    barriers[2].UAV.pResource = freeListResource_.Get();
    commandList->ResourceBarrier(_countof(barriers), barriers);
}

void GpuParticleManager::TriggerEmit(const Vector3& position, uint32_t count) {
    if (emitterData_) {
        emitterData_->translate = position;
        emitterData_->count = count;
        emitterData_->emit = 1;
        Emit();
        emitterData_->emit = 0;
    }
}

void GpuParticleManager::UpdateCS() {
    ID3D12GraphicsCommandList* commandList = DirectXCommon::GetInstance()->GetCommandList();
    GraphicsPipeline* graphicsPipeline = GraphicsPipeline::GetInstance();

    commandList->SetComputeRootSignature(graphicsPipeline->GetGpuParticleUpdateRootSignature());
    commandList->SetPipelineState(graphicsPipeline->GetGpuParticleUpdatePipelineState());

    // [0] u0 (DescriptorTable)
    SrvManager::GetInstance()->SetComputeRootDescriptorTable(0, uavIndex_);
    // [1] b0 (PerFrame)
    commandList->SetComputeRootConstantBufferView(1, perFrameResource_->GetGPUVirtualAddress());

    // kMaxParticlesスレッド実行 (numthreads(1024, 1, 1))
    commandList->Dispatch(kMaxParticles / 1024, 1, 1);

    // UAVバリア
    D3D12_RESOURCE_BARRIER barriers[3] = {};
    barriers[0].Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
    barriers[0].UAV.pResource = particleResource_.Get();
    barriers[1].Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
    barriers[1].UAV.pResource = freeListIndexResource_.Get();
    barriers[2].Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
    barriers[2].UAV.pResource = freeListResource_.Get();
    commandList->ResourceBarrier(_countof(barriers), barriers);
}

void GpuParticleManager::Draw(ID3D12GraphicsCommandList* commandList, D3D12_GPU_DESCRIPTOR_HANDLE textureHandle, D3D12_VERTEX_BUFFER_VIEW* vbView, D3D12_INDEX_BUFFER_VIEW* ibView) {
    // 描画前に UAV -> SRV へ遷移させる
    D3D12_RESOURCE_BARRIER transitionBarrier{};
    transitionBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    transitionBarrier.Transition.pResource = particleResource_.Get();
    transitionBarrier.Transition.StateBefore = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    transitionBarrier.Transition.StateAfter = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    transitionBarrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    commandList->ResourceBarrier(1, &transitionBarrier);

    GraphicsPipeline* graphicsPipeline = GraphicsPipeline::GetInstance();

    commandList->SetGraphicsRootSignature(graphicsPipeline->GetGpuParticleRootSignature());
    commandList->SetPipelineState(graphicsPipeline->GetGpuParticlePipelineState());

    // グラフィックスPSOを設定した後にIAをバインド (安全な順序)
    if (vbView && ibView) {
        commandList->IASetVertexBuffers(0, 1, vbView);
        commandList->IASetIndexBuffer(ibView);
        commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    }

    // [0] PerView (b0)
    commandList->SetGraphicsRootConstantBufferView(0, perViewResource_->GetGPUVirtualAddress());

    // [1] gParticles (t0)
    SrvManager::GetInstance()->SetGraphicsRootDescriptorTable(1, srvIndex_);

    // [2] Texture (t1)
    commandList->SetGraphicsRootDescriptorTable(2, textureHandle);

    // 描画コマンド (板ポリゴン 6インデックス, 4096インスタンス)
    commandList->DrawIndexedInstanced(6, kMaxParticles, 0, 0, 0);

    // 次フレームのために SRV -> UAV へ戻しておく
    transitionBarrier.Transition.StateBefore = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    transitionBarrier.Transition.StateAfter = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    commandList->ResourceBarrier(1, &transitionBarrier);
}

void GpuParticleManager::CreateResource() {
    // パーティクルリソース (DEFAULT heap)
    particleResource_ = CreateUAVBufferResource(device_, sizeof(ParticleCS) * kMaxParticles);
    
    // FreeListIndexリソース (DEFAULT heap)
    freeListIndexResource_ = CreateUAVBufferResource(device_, sizeof(int32_t));

    // FreeListリソース (DEFAULT heap)
    freeListResource_ = CreateUAVBufferResource(device_, sizeof(uint32_t) * kMaxParticles);

    // PerViewリソース (UPLOAD heap)
    perViewResource_ = CreateBufferResource(device_, sizeof(PerView));
    perViewResource_->Map(0, nullptr, reinterpret_cast<void**>(&perViewData_));

    // Emitterリソース (UPLOAD heap)
    emitterResource_ = CreateBufferResource(device_, sizeof(EmitterSphere));
    emitterResource_->Map(0, nullptr, reinterpret_cast<void**>(&emitterData_));
    // 資料に基づいた初期値
    emitterData_->count = 32;
    emitterData_->frequency = 0.1f;
    emitterData_->frequencyTime = 0.0f;
    emitterData_->translate = {0.0f, 0.0f, 0.0f};
    emitterData_->radius = 1.0f;
    emitterData_->emit = 0;

    // PerFrameリソース (UPLOAD heap)
    perFrameResource_ = CreateBufferResource(device_, sizeof(PerFrame));
    perFrameResource_->Map(0, nullptr, reinterpret_cast<void**>(&perFrameData_));
}

void GpuParticleManager::CreateSrvUav() {
    SrvManager* srvManager = SrvManager::GetInstance();
    
    // SRV確保
    srvIndex_ = srvManager->Allocate();
    srvManager->CreateSRVforStructuredBuffer(srvIndex_, particleResource_.Get(), kMaxParticles, sizeof(ParticleCS));
    srvHandleGPU_ = srvManager->GetGPUDescriptorHandle(srvIndex_);

    // UAV確保 (Particle と FreeListIndex と FreeList を連続させてテーブル化する)
    uavIndex_ = srvManager->Allocate();
    freeListIndexUavIndex_ = srvManager->Allocate();
    freeListUavIndex_ = srvManager->Allocate();
    
    srvManager->CreateUAVforStructuredBuffer(uavIndex_, particleResource_.Get(), kMaxParticles, sizeof(ParticleCS));
    srvManager->CreateUAVforStructuredBuffer(freeListIndexUavIndex_, freeListIndexResource_.Get(), 1, sizeof(int32_t));
    srvManager->CreateUAVforStructuredBuffer(freeListUavIndex_, freeListResource_.Get(), kMaxParticles, sizeof(uint32_t));
    
    uavHandleGPU_ = srvManager->GetGPUDescriptorHandle(uavIndex_);
    freeListIndexUavHandleGPU_ = srvManager->GetGPUDescriptorHandle(freeListIndexUavIndex_);
    freeListUavHandleGPU_ = srvManager->GetGPUDescriptorHandle(freeListUavIndex_);
}
