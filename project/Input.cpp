#include "Input.h"
#include <cstring> // memcpy を使うために必要
#include <cassert> // ★ assertマクロ を使うために追加

// 達成条件: クラスが自作できる
Input::Input() {
    // コンストラクタ
}

Input::~Input() {
    // デストラクタ
}

// 達成条件: クラスに関連した処理をメンバ関数としてまとめられる
void Input::Initialize() {
    // 全キーの状態を取得
    HRESULT hr = GetKeyboardState(keys_);
    assert(SUCCEEDED(hr)); // ★ これが動作するようになる
    // 前フレームの状態も現在の状態に初期化
    memcpy(prevKeys_, keys_, sizeof(keys_));
}

void Input::Update() {
    // 今のキー状態を「前フレーム」として保存
    memcpy(prevKeys_, keys_, sizeof(keys_));
    // 新たに全キーの状態を取得
    HRESULT hr = GetKeyboardState(keys_);
    assert(SUCCEEDED(hr)); // ★ これが動作するようになる
}

bool Input::IsKeyDown(BYTE key) const {
    // 指定キーの最上位ビットが立っていれば「押されている」
    return (keys_[key] & 0x80);
}

bool Input::IsKeyPressed(BYTE key) const {
    // 「今回は押されている」かつ「前回は押されていない」
    return (keys_[key] & 0x80) && !(prevKeys_[key] & 0x80);
}

bool Input::IsKeyReleased(BYTE key) const {
    // 「今回は押されていない」かつ「前回は押されている」
    return !(keys_[key] & 0x80) && (prevKeys_[key] & 0x80);
}