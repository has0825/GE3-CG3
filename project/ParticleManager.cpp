#include "ParticleManager.h"
#include "MathUtil.h"
#include "D3D12Util.h" 
#include "DirectXCommon.h"
#include <algorithm>
#include <cassert>
#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

void ParticleManager::Initialize(ID3D12Device* device, const std::string& texturePath) {
    device_ = device;

    // 乱数初期化
    std::random_device seedGenerator;
    randomEngine_.seed(seedGenerator());

    // 1. Instancing用リソース作成
    size_t sizeIB = sizeof(ParticleForGPU) * kNumMaxInstance;
    instancingResource_ = CreateBufferResource(device_, sizeIB);
    instancingResource_->Map(0, nullptr, reinterpret_cast<void**>(&instancingData_));

    // 2. デスクリプタヒープ作成
    D3D12_DESCRIPTOR_HEAP_DESC srvHeapDesc = {};
    srvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    srvHeapDesc.NumDescriptors = 2; 
    srvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    HRESULT hr = device_->CreateDescriptorHeap(&srvHeapDesc, IID_PPV_ARGS(&srvHeap_));
    assert(SUCCEEDED(hr));
    descriptorSizeSRV_ = device_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    // 3. テクスチャ読み込み
    LoadParticleTexture(texturePath);

    // 4. StructuredBuffer SRV作成
    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Format = DXGI_FORMAT_UNKNOWN;
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
    srvDesc.Buffer.FirstElement = 0;
    srvDesc.Buffer.NumElements = kNumMaxInstance;
    srvDesc.Buffer.StructureByteStride = sizeof(ParticleForGPU);
    srvDesc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;
    device_->CreateShaderResourceView(instancingResource_.Get(), &srvDesc, GetCPUDescriptorHandle(1));

    // ★追加: 板ポリゴンの作成
    CreateParticleGeometry();
}

// ★追加: 板ポリゴン(四角形)の頂点データを作成する関数
void ParticleManager::CreateParticleGeometry() {
    // 6頂点で1枚の四角形を作る (TriangleList)
    VertexData vertices[] = {
        //  x      y      z      w       u     v       nx    ny    nz
        {{-0.5f, -0.5f, 0.0f, 1.0f}, {0.0f, 1.0f}, {0.0f, 0.0f, -1.0f}}, // 左下
        {{-0.5f,  0.5f, 0.0f, 1.0f}, {0.0f, 0.0f}, {0.0f, 0.0f, -1.0f}}, // 左上
        {{ 0.5f, -0.5f, 0.0f, 1.0f}, {1.0f, 1.0f}, {0.0f, 0.0f, -1.0f}}, // 右下

        {{-0.5f,  0.5f, 0.0f, 1.0f}, {0.0f, 0.0f}, {0.0f, 0.0f, -1.0f}}, // 左上
        {{ 0.5f,  0.5f, 0.0f, 1.0f}, {1.0f, 0.0f}, {0.0f, 0.0f, -1.0f}}, // 右上
        {{ 0.5f, -0.5f, 0.0f, 1.0f}, {1.0f, 1.0f}, {0.0f, 0.0f, -1.0f}}, // 右下
    };

    // バッファ作成
    size_t sizeVB = sizeof(vertices);
    vertexBuffer_ = CreateBufferResource(device_, sizeVB);
    
    // データ転送
    VertexData* data = nullptr;
    vertexBuffer_->Map(0, nullptr, reinterpret_cast<void**>(&data));
    std::memcpy(data, vertices, sizeVB);
    vertexBuffer_->Unmap(0, nullptr);

    // ビュー作成
    vertexBufferView_.BufferLocation = vertexBuffer_->GetGPUVirtualAddress();
    vertexBufferView_.SizeInBytes = static_cast<UINT>(sizeVB);
    vertexBufferView_.StrideInBytes = sizeof(VertexData);
}

void ParticleManager::LoadParticleTexture(const std::string& texturePath) {
    DirectX::ScratchImage mipImages = ::LoadTexture(texturePath);
    metadata_ = mipImages.GetMetadata();
    textureResource_ = CreateTextureResource(device_, metadata_);
    
    auto dxCommon = DirectXCommon::GetInstance();
    auto commandList = dxCommon->GetCommandList();
    auto intermediate = UploadTextureData(textureResource_.Get(), mipImages, device_, commandList);

    commandList->Close();
    ID3D12CommandList* ppCommandLists[] = { commandList };
    dxCommon->GetCommandQueue()->ExecuteCommandLists(1, ppCommandLists);
    
    Microsoft::WRL::ComPtr<ID3D12Fence> fence;
    device_->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence));
    HANDLE fenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
    dxCommon->GetCommandQueue()->Signal(fence.Get(), 1);
    if (fence->GetCompletedValue() < 1) {
        fence->SetEventOnCompletion(1, fenceEvent);
        WaitForSingleObject(fenceEvent, INFINITE);
    }
    CloseHandle(fenceEvent);
    dxCommon->GetCommandAllocator()->Reset();
    commandList->Reset(dxCommon->GetCommandAllocator(), nullptr);

    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Format = metadata_.format;
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MipLevels = UINT(metadata_.mipLevels);
    device_->CreateShaderResourceView(textureResource_.Get(), &srvDesc, GetCPUDescriptorHandle(0));
}

void ParticleManager::Update(const Matrix4x4& viewProjectionMatrix) {
    particles_.remove_if([](Particle& p) {
        return p.currentTime >= p.lifeTime;
    });

    uint32_t instanceCount = 0;
    const float kDeltaTime = 1.0f / 60.0f;

    for (auto& particle : particles_) {
        if (instanceCount >= kNumMaxInstance) break;

        particle.currentTime += kDeltaTime;
        particle.position.x += particle.velocity.x * kDeltaTime;
        particle.position.y += particle.velocity.y * kDeltaTime;
        particle.position.z += particle.velocity.z * kDeltaTime;

        float alpha = 1.0f - (particle.currentTime / particle.lifeTime);
        if (alpha < 0.0f) alpha = 0.0f;

        Matrix4x4 worldMatrix = MakeAffineMatrix(particle.scale, particle.rotate, particle.position);
        Matrix4x4 wvpMatrix = Multiply(worldMatrix, viewProjectionMatrix);
        
        Vector4 color = particle.color;
        color.w *= alpha;

        instancingData_[instanceCount].WVP = wvpMatrix;
        instancingData_[instanceCount].World = worldMatrix;
        instancingData_[instanceCount].color = color;

        instanceCount++;
    }
}

void ParticleManager::Draw(ID3D12GraphicsCommandList* commandList) {
    UINT count = static_cast<UINT>(std::min<size_t>(particles_.size(), kNumMaxInstance));
    if (count == 0) return;

    ID3D12DescriptorHeap* heaps[] = { srvHeap_.Get() };
    commandList->SetDescriptorHeaps(1, heaps);

    // ★追加: ここで頂点バッファをセットする！
    commandList->IASetVertexBuffers(0, 1, &vertexBufferView_);

    commandList->SetGraphicsRootDescriptorTable(1, GetGPUDescriptorHandle(1));
    commandList->SetGraphicsRootDescriptorTable(2, GetGPUDescriptorHandle(0));

    commandList->DrawInstanced(6, count, 0, 0);
}

void ParticleManager::Emit(ParticleType type, const Vector3& emitterPos, const Vector3& baseVelocity) {
    Particle p;
    p.rotate = { 0,0,0 }; 
    p.currentTime = 0.0f;

    std::uniform_real_distribution<float> distPos(-1.0f, 1.0f);
    std::uniform_real_distribution<float> distVel(-1.0f, 1.0f);
    std::uniform_real_distribution<float> distColor(0.0f, 1.0f);
    std::uniform_real_distribution<float> distLife(1.0f, 3.0f);

    switch (type) {
    case ParticleType::kExplosion:
    default:
        p.position = { emitterPos.x + distPos(randomEngine_) * 0.5f, emitterPos.y + distPos(randomEngine_) * 0.5f, emitterPos.z + distPos(randomEngine_) * 0.5f };
        p.velocity = { distVel(randomEngine_), distVel(randomEngine_), distVel(randomEngine_) };
        p.lifeTime = distLife(randomEngine_);
        p.color = { distColor(randomEngine_), distColor(randomEngine_), distColor(randomEngine_), 1.0f };
        p.scale = { 1.0f, 1.0f, 1.0f };
        break;
    case ParticleType::kFountain:
        p.position = { emitterPos.x + distPos(randomEngine_) * 0.2f, emitterPos.y, emitterPos.z + distPos(randomEngine_) * 0.2f };
        p.velocity = { distVel(randomEngine_) * 0.5f, 2.0f + std::abs(distVel(randomEngine_)), distVel(randomEngine_) * 0.5f };
        p.lifeTime = 2.0f;
        p.color = { 0.2f, 0.5f, 1.0f, 1.0f };
        p.scale = { 0.5f, 0.5f, 0.5f };
        break;
    case ParticleType::kSpiral: {
        float angle = distPos(randomEngine_) * (float)M_PI;
        p.position = { emitterPos.x + std::cos(angle) * 1.5f, emitterPos.y, emitterPos.z + std::sin(angle) * 1.5f };
        p.velocity = { 0.0f, 1.0f, 0.0f };
        p.lifeTime = 3.0f;
        p.color = { distColor(randomEngine_), distColor(randomEngine_), distColor(randomEngine_), 1.0f };
        p.scale = { 0.8f, 0.8f, 0.8f };
        break;
    }
    case ParticleType::kRain:
        p.position = { emitterPos.x + distPos(randomEngine_) * 5.0f, emitterPos.y + 5.0f, emitterPos.z + distPos(randomEngine_) * 5.0f };
        p.velocity = { 0.0f, -3.0f, 0.0f };
        p.lifeTime = 3.0f;
        p.color = { 0.8f, 0.8f, 1.0f, 1.0f };
        p.scale = { 0.1f, 1.0f, 0.1f };
        break;
    }

    particles_.push_back(p);
}

D3D12_CPU_DESCRIPTOR_HANDLE ParticleManager::GetCPUDescriptorHandle(uint32_t index) {
    D3D12_CPU_DESCRIPTOR_HANDLE handle = srvHeap_->GetCPUDescriptorHandleForHeapStart();
    handle.ptr += (static_cast<UINT64>(descriptorSizeSRV_) * index);
    return handle;
}

D3D12_GPU_DESCRIPTOR_HANDLE ParticleManager::GetGPUDescriptorHandle(uint32_t index) {
    D3D12_GPU_DESCRIPTOR_HANDLE handle = srvHeap_->GetGPUDescriptorHandleForHeapStart();
    handle.ptr += (static_cast<UINT64>(descriptorSizeSRV_) * index);
    return handle;
}