#include "Hoge.h"

Hoge::Hoge() {
    // 必須条件：new を排除し make_unique を使用
    fuga_ = std::make_unique<Fuga>();
}

// デストラクタで delete を書く必要はない
Hoge::~Hoge() = default;

void Hoge::Update() {
    if (fuga_) {
        fuga_->Update();
    }
}