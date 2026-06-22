#include "Input.h"
#include "WinApp.h" // 初期化にWinAppが必要
#include <cassert>  // assert()用
#include <cstring>  // memcpy用
#include <windows.h> // ★GetAsyncKeyState用に追加

// DirectInputのライブラリをリンク
#pragma comment(lib, "dinput8.lib")
#pragma comment(lib, "dxguid.lib")

Input* Input::GetInstance() {
    static Input instance;
    return &instance;
}

// 1. 初期化処理
void Input::Initialize(WinApp* winApp) {
    HRESULT hr;

    // DirectInputオブジェクトの作成
    hr = DirectInput8Create(
        winApp->GetHInstance(), // WinAppからインスタンスハンドルを取得
        DIRECTINPUT_VERSION,
        IID_IDirectInput8,
        (void**)&directInput_,
        nullptr);
    assert(SUCCEEDED(hr));

    // キーボードデバイスの作成
    hr = directInput_->CreateDevice(GUID_SysKeyboard, &keyboard_, nullptr);
    assert(SUCCEEDED(hr));

    // 入力データ形式の設定 (DirectInputに「キーボード」として認識させる)
    hr = keyboard_->SetDataFormat(&c_dfDIKeyboard);
    assert(SUCCEEDED(hr));

    // 協力レベルの設定 (フォアグラウンド・非占有モード)
    // DISCL_FOREGROUND: ウィンドウがアクティブな時だけ入力を受け取る
    // DISCL_NONEXCLUSIVE: 他のアプリも入力を受け取れる
    hr = keyboard_->SetCooperativeLevel(
        winApp->GetHwnd(), // WinAppからウィンドウハンドルを取得
        DISCL_FOREGROUND | DISCL_NONEXCLUSIVE);
    assert(SUCCEEDED(hr));
}

// 終了処理
void Input::Finalize() {
    if (keyboard_) {
        keyboard_->Unacquire(); // デバイスの制御権を解放
    }
    keyboard_.Reset();
    directInput_.Reset();
}

// 2. 毎フレーム処理
void Input::Update() {
    // キーボードの入力を取得開始 (制御権の取得)
    HRESULT hr = keyboard_->Acquire();

    // デバイスがロストしているか、他のアプリが制御している場合は再試行
    if (hr == DIERR_INPUTLOST || hr == DIERR_NOTACQUIRED) {
        hr = keyboard_->Acquire();
        if (FAILED(hr)) {
            // それでも失敗したら、今フレームの更新は諦める
            wasAcquired_ = false;
            return;
        }
    }

    // キー状態の取得 (現在の状態を一時バッファに格納)
    BYTE currentKeys[256] = {};
    hr = keyboard_->GetDeviceState(sizeof(currentKeys), currentKeys);
    if (FAILED(hr)) {
        // 取得失敗
        wasAcquired_ = false;
        return;
    }

    if (!wasAcquired_) {
        // 前回取得に失敗していた、またはこれが最初の取得成功フレームなら、
        // 前フレームとの差分を作らないように両方のバッファを現在の状態で初期化する
        std::memcpy(keys_, currentKeys, sizeof(keys_));
        std::memcpy(prevKeys_, currentKeys, sizeof(prevKeys_));
        wasAcquired_ = true;
    } else {
        // 通常の更新 (前フレームの状態を保存し、新入力を反映)
        std::memcpy(prevKeys_, keys_, sizeof(keys_));
        std::memcpy(keys_, currentKeys, sizeof(keys_));
    }

    // マウスホイールの更新
    wheelDelta_ = WinApp::GetInstance()->GetWheelDelta();
    WinApp::GetInstance()->ResetWheelDelta();
}

// 3. 条件判定のための bool 型関数 (Press)
bool Input::IsKeyPressed(uint8_t keyCode) {
    // 指定されたキーの最上位ビット (0x80) が立っていれば「押されている」
    return (keys_[keyCode] & 0x80);
}

// 4. 条件判定のための bool 型関数 (Trigger)
bool Input::IsKeyTriggered(uint8_t keyCode) {
    // (今押されている) AND (前は押されていない)
    return (keys_[keyCode] & 0x80) && !(prevKeys_[keyCode] & 0x80);
}

// 5. 条件判定のための bool 型関数 (Release)
bool Input::IsKeyReleased(uint8_t keyCode) {
    // (今は押されていない) AND (前は押されていた)
    return !(keys_[keyCode] & 0x80) && (prevKeys_[keyCode] & 0x80);
}

// ===========================================
// ★追加: マウスボタンの判定 (Windows APIを使用)
// ===========================================
bool Input::IsMousePressed(int buttonNumber) {
    int vKey = 0;

    // 引数に応じて仮想キーコードを設定
    if (buttonNumber == 0) {
        vKey = VK_LBUTTON; // 左クリック
    } else if (buttonNumber == 1) {
        vKey = VK_RBUTTON; // 右クリック
    } else if (buttonNumber == 2) {
        vKey = VK_MBUTTON; // 中クリック
    } else {
        return false; // それ以外の無効な番号
    }

    // GetAsyncKeyState で現在のボタンの状態を取得
    // 最上位ビット(0x8000)が立っていれば押されている
    return (GetAsyncKeyState(vKey) & 0x8000) != 0;
}