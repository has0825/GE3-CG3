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

    // UAVデスクリプタテーブルをセット (u0: particles, u1: counter)
    SrvManager::GetInstance()->PreDraw();
    SrvManager::GetInstance()->SetComputeRootDescriptorTable(0, uavIndex_);

    // Dispatch (1, 1, 1) - 1024個なので (InitializeParticle.CS.hlslはnumthreads(1024, 1, 1))
    commandList->Dispatch(1, 1, 1);

    // UAVバリア
    D3D12_RESOURCE_BARRIER barriers[2] = {};
    barriers[0].Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
    barriers[0].UAV.pResource = particleResource_.Get();
    barriers[1].Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
    barriers[1].UAV.pResource = freeCounterResource_.Get();
    commandList->ResourceBarrier(_countof(barriers), barriers);
}

void GpuParticleManager::Update(const Matrix4x4& viewProjection, const Matrix4x4& billboardMatrix, float deltaTime) {
    perViewData_->viewProjection = viewProjection;
    perViewData_->billboardMatrix = billboardMatrix;

    // 時間の更新
    time_ += deltaTime;
    perFrameData_->time = time_;
    perFrameData_->deltaTime = deltaTime;

    // Emitterの更新処理 (資料に基づく)
    emitterData_->frequencyTime += deltaTime;
    if (emitterData_->frequency <= emitterData_->frequencyTime) {
        emitterData_->frequencyTime -= emitterData_->frequency;
        emitterData_->emit = 1;
    } else {
        emitterData_->emit = 0;
    }
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
    // [2] b1 (PerFrame)
    commandList->SetComputeRootConstantBufferView(2, perFrameResource_->GetGPUVirtualAddress());

    commandList->Dispatch(1, 1, 1);

    // UAVバリア
    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
    barrier.UAV.pResource = particleResource_.Get();
    commandList->ResourceBarrier(1, &barrier);
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

    // 1024スレッド実行
    commandList->Dispatch(1, 1, 1);

    // UAVバリア
    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
    barrier.UAV.pResource = particleResource_.Get();
    commandList->ResourceBarrier(1, &barrier);
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
    
    // カウンタリソース (DEFAULT heap)
    freeCounterResource_ = CreateUAVBufferResource(device_, sizeof(int32_t));

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

    // UAV確保 (Particle と Counter を連続させてテーブル化しやすくする)
    uavIndex_ = srvManager->Allocate();
    counterUavIndex_ = srvManager->Allocate();
    
    srvManager->CreateUAVforStructuredBuffer(uavIndex_, particleResource_.Get(), kMaxParticles, sizeof(ParticleCS));
    srvManager->CreateUAVforStructuredBuffer(counterUavIndex_, freeCounterResource_.Get(), 1, sizeof(int32_t));
    
    uavHandleGPU_ = srvManager->GetGPUDescriptorHandle(uavIndex_);
    counterUavHandleGPU_ = srvManager->GetGPUDescriptorHandle(counterUavIndex_);
}
