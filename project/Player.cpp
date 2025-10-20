#include "Player.h"

Player::Player() {}
Player::~Player() {
    delete model_; // ★ 忘れずに delete
}

void Player::Initialize(ID3D12Device* device) {
    // ★ Playerの初期化時に、自分の Model もロードする
    model_ = Model::Create("resources", "player.obj", device);
    // 初期座標などをセット
    model_->transform.translate = { 0.0f, 0.0f, 0.0f };
}

// ★★★ ここが重要 ★★★
void Player::Update(Input* input) {
    // Player::Update の中で、Input を使って自分の Model を動かす
    const float moveSpeed = 0.1f;

    if (input->IsKeyDown('A')) {
        model_->transform.translate.x -= moveSpeed;
    }
    if (input->IsKeyDown('D')) {
        model_->transform.translate.x += moveSpeed;
    }
    if (input->IsKeyDown('W')) {
        model_->transform.translate.z += moveSpeed;
    }
    if (input->IsKeyDown('S')) {
        model_->transform.translate.z -= moveSpeed;
    }
}

void Player::Draw(ID3D12GraphicsCommandList* commandList, const Matrix4x4& viewProjection, D3D12_GPU_DESCRIPTOR_HANDLE srvHandle) {
    // 自分のモデルを描画する
    model_->Draw(commandList, viewProjection, srvHandle);
}