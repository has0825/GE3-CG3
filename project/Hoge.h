#pragma once
#include <memory>
#include "Fuga.h"

class Hoge {
public:
    Hoge();
    ~Hoge();

    void Update();

private:
    // 必須条件：unique_ptr を活用
    std::unique_ptr<Fuga> fuga_;
};