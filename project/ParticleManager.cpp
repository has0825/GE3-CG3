#include "ParticleManager.h"
#include "MathUtil.h" // ★修正1: これを追加しないと MakeAffineMatrix が使えません
#include <algorithm>  // std::min用
#include <cassert>

void ParticleManager::Initialize(ID3D12Device* device) {
    device_ = device;

    // ---------------------------------------------------------
    // 1. StructuredBuffer用リソースの作成
    // ---------------------------------------------------------
    // サイズ = 1つ分のサイズ * 最大個数
    size_t sizeIB = sizeof(TransformationMatrix) * kNumMaxInstance;

    D3D12_HEAP_PROPERTIES uploadHeapProp{};
    uploadHeapProp.Type = D3D12_HEAP_TYPE_UPLOAD; // CPUから書き込むのでUPLOAD

    D3D12_RESOURCE_DESC bufferDesc{};
    bufferDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    bufferDesc.Width = sizeIB;
    bufferDesc.Height = 1;
    bufferDesc.DepthOrArraySize = 1;
    bufferDesc.MipLevels = 1;
    bufferDesc.Format = DXGI_FORMAT_UNKNOWN; // StructuredBufferはフォーマット指定なし
    bufferDesc.SampleDesc.Count = 1;
    bufferDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    bufferDesc.Flags = D3D12_RESOURCE_FLAG_NONE;

    HRESULT hr = device_->CreateCommittedResource(
        &uploadHeapProp,
        D3D12_HEAP_FLAG_NONE,
        &bufferDesc,
        D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr,
        IID_PPV_ARGS(&instancingResource_)
    );
    assert(SUCCEEDED(hr));

    // マッピングしておく（書き込み用）
    instancingResource_->Map(0, nullptr, reinterpret_cast<void**>(&instancingData_));

    // ---------------------------------------------------------
    // 2. SRV用デスクリプタヒープの作成
    // ---------------------------------------------------------
    D3D12_DESCRIPTOR_HEAP_DESC srvHeapDesc = {};
    srvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    srvHeapDesc.NumDescriptors = 1;
    srvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;

    hr = device_->CreateDescriptorHeap(&srvHeapDesc, IID_PPV_ARGS(&srvDescriptorHeap_));
    assert(SUCCEEDED(hr));

    // ---------------------------------------------------------
    // 3. SRV (Shader Resource View) の作成
    // ---------------------------------------------------------
    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Format = DXGI_FORMAT_UNKNOWN; // StructuredBufferはUNKNOWN必須
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
    // StructuredBufferの設定
    srvDesc.Buffer.FirstElement = 0;
    srvDesc.Buffer.NumElements = kNumMaxInstance;
    srvDesc.Buffer.StructureByteStride = sizeof(TransformationMatrix); // 1つ分のサイズ
    srvDesc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;

    // デスクリプタヒープの先頭に作成
    device_->CreateShaderResourceView(instancingResource_.Get(), &srvDesc, srvDescriptorHeap_->GetCPUDescriptorHandleForHeapStart());
}

void ParticleManager::Update(const Matrix4x4& viewProjectionMatrix) {
    // 寿命が尽きたパーティクルを削除
    particles_.remove_if([](Particle& p) {
        return p.currentTime >= p.lifeTime;
        });

    uint32_t instanceCount = 0;

    // 全パーティクル更新 & データ転送
    for (auto& particle : particles_) {
        // 最大数を超えたら処理打ち切り（書き込む場所がないため）
        if (instanceCount >= kNumMaxInstance) break;

        // 1. CPU側での移動計算など
        particle.currentTime += 1.0f / 60.0f; // 簡易的なデルタタイム

        // ★修正2: Vector3の足し算 (演算子+が無いので成分ごとに計算)
        particle.position.x += particle.velocity.x;
        particle.position.y += particle.velocity.y;
        particle.position.z += particle.velocity.z;

        // 2. GPUへ送るデータの計算 (World行列とWVP行列)
        Matrix4x4 worldMatrix = MakeAffineMatrix(particle.scale, particle.rotate, particle.position);

        // ★修正3: 行列の掛け算 (演算子*が無いので Multiply 関数を使用)
        Matrix4x4 wvpMatrix = Multiply(worldMatrix, viewProjectionMatrix);

        // 3. StructuredBufferへ書き込み
        instancingData_[instanceCount].WVP = wvpMatrix;
        instancingData_[instanceCount].World = worldMatrix;

        instanceCount++;
    }
}

void ParticleManager::Draw(ID3D12GraphicsCommandList* commandList) {
    // 描画するインスタンス数（現在のパーティクル数）
    // ただし最大数を超えないようにクランプ
    UINT currentInstanceCount = static_cast<UINT>(std::min<size_t>(particles_.size(), kNumMaxInstance));

    if (currentInstanceCount == 0) return; // 描画するものがない

    // --- ここから描画設定 ---

    // 1. デスクリプタヒープを設定 (SRVを使うため必須)
    ID3D12DescriptorHeap* descriptorHeaps[] = { srvDescriptorHeap_.Get() };
    commandList->SetDescriptorHeaps(1, descriptorHeaps);

    // 2. RootParameterの設定
    // GraphicsPipeline.cpp で設定した通り、
    // Index 1 が "StructuredBuffer(t1)" 用の DescriptorTable
    commandList->SetGraphicsRootDescriptorTable(1, srvDescriptorHeap_->GetGPUDescriptorHandleForHeapStart());

    // (Textureなどは別途 Index 2 に設定してください)

    // 3. 描画コマンド (Instancing)
    // 頂点数6(四角形), インスタンス数(パーティクル数)
    commandList->DrawInstanced(6, currentInstanceCount, 0, 0);
}

void ParticleManager::Emit(const Vector3& position, const Vector3& velocity) {
    Particle newParticle;
    newParticle.position = position;
    newParticle.velocity = velocity;
    newParticle.rotate = { 0,0,0 };
    newParticle.scale = { 1.0f, 1.0f, 1.0f };
    newParticle.lifeTime = 2.0f; // 2秒
    newParticle.currentTime = 0.0f;
    particles_.push_back(newParticle);
}