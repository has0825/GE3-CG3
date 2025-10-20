#pragma once
#include "Model.h"
#include "Input.h"

class Player {
public:
    Player();
    ~Player();

    // ★ 1. モデルのロードや初期化
    void Initialize(ID3D12Device* device);

    // ★ 2. 毎フレームの更新 (Inputクラスを引数で受け取る)
    void Update(Input* input);

    // ★ 3. 描画 (Model.h に合わせた引数)
    void Draw(ID3D12GraphicsCommandList* commandList, const Matrix4x4& viewProjection, D3D12_GPU_DESCRIPTOR_HANDLE srvHandle);

private:
    // ★ Playerは Model を「持っている (has-a)」
    Model* model_ = nullptr;
    // (必要ならテクスチャなども)
};