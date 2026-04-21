#pragma once
#include <Windows.h>
#include <cstdint>
#include <string>

class WinApp {
public:
    // ウィンドウサイズ
    static const int32_t kClientWidth = 1280;
    static const int32_t kClientHeight = 720;

public:
    // シングルトンインスタンスの取得
    static WinApp* GetInstance();

    // ウィンドウの初期化
    void Initialize(const wchar_t* title = L"CG2");

    // 終了処理
    void Finalize();

    // メッセージの処理
    bool ProcessMessage();

    // ゲッター
    HWND GetHwnd() const { return hwnd_; }
    HINSTANCE GetHInstance() const { return wc_.hInstance; }

    // 終了がリクエストされたか
    bool IsEndRequested() const { return endRequest_; }

    // マウスホイール
    int32_t GetWheelDelta() const { return wheelDelta_; }
    void ResetWheelDelta() { wheelDelta_ = 0; }

private:
    // シングルトンパターンのためのプライベートコンストラクタなど
    WinApp() = default;
    ~WinApp() = default;
    WinApp(const WinApp&) = delete;
    const WinApp& operator=(const WinApp&) = delete;

    // ウィンドウプロシージャ
    static LRESULT CALLBACK WindowProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);

private:
    HWND hwnd_ = nullptr;
    WNDCLASSW wc_{};
    std::wstring title_;
    bool endRequest_ = false;
    int32_t wheelDelta_ = 0;
};