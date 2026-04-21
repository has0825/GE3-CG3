#include "WinApp.h"
#include "externals/imgui/imgui_impl_win32.h"

// ★追加: timeBeginPeriodを使用するために必要
#pragma comment(lib, "winmm.lib")

// ImGuiのウィンドウプロシージャハンドラの前方宣言
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

WinApp* WinApp::GetInstance() {
    static WinApp instance;
    return &instance;
}

void WinApp::Initialize(const wchar_t* title) {
    // ★追加: システムタイマーの分解能を上げる（FPS安定化のため）
    timeBeginPeriod(1);

    title_ = title;

    // ウィンドウクラスの設定
    wc_.lpfnWndProc = WindowProc;
    wc_.lpszClassName = L"CG2WindowClass";
    wc_.hInstance = GetModuleHandle(nullptr);
    wc_.hCursor = LoadCursor(nullptr, IDC_ARROW);

    // ウィンドウクラスの登録
    RegisterClassW(&wc_);

    // ウィンドウサイズの矩形を定義
    RECT wrc = { 0, 0, kClientWidth, kClientHeight };

    // クライアント領域を元に実際のウィンドウサイズへ調整
    AdjustWindowRect(&wrc, WS_OVERLAPPEDWINDOW, false);

    // ウィンドウの生成
    hwnd_ = CreateWindowW(
        wc_.lpszClassName,
        title_.c_str(),
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        wrc.right - wrc.left,
        wrc.bottom - wrc.top,
        nullptr,
        nullptr,
        wc_.hInstance,
        nullptr
    );

    // ウィンドウを表示
    ShowWindow(hwnd_, SW_SHOW);
}

void WinApp::Finalize() {
    // ウィンドウを閉じる
    CloseWindow(hwnd_);

    // ★追加: ウィンドウクラスの登録解除（行儀の良い終了処理）
    UnregisterClassW(wc_.lpszClassName, wc_.hInstance);

    // ★追加: システムタイマーの分解能を戻す
    timeEndPeriod(1);
}

bool WinApp::ProcessMessage() {
    MSG msg{};
    // メッセージキューからメッセージを取得
    if (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    // 終了メッセージが来たらフラグを立てる
    if (msg.message == WM_QUIT) {
        endRequest_ = true;
    }

    return endRequest_;
}

LRESULT CALLBACK WinApp::WindowProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    // ImGuiにメッセージを渡す（ここが最重要：これがないとImGuiが操作できない）
    if (ImGui_ImplWin32_WndProcHandler(hwnd, msg, wparam, lparam)) {
        return true;
    }

    // 通常のメッセージ処理
    switch (msg) {
    case WM_MOUSEWHEEL:
        GetInstance()->wheelDelta_ += GET_WHEEL_DELTA_WPARAM(wparam);
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }

    return DefWindowProcW(hwnd, msg, wparam, lparam);
}