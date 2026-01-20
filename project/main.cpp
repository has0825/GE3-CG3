// main.cpp
#include "Game.h"
#include <Windows.h>
#include <dxgidebug.h>
#include <memory> // 追加

struct D3DResourceLeakChecker {
    ~D3DResourceLeakChecker() {
        Microsoft::WRL::ComPtr<IDXGIDebug1> debug;
        if (SUCCEEDED(DXGIGetDebugInterface1(0, IID_PPV_ARGS(&debug)))) {
            debug->ReportLiveObjects(DXGI_DEBUG_ALL, DXGI_DEBUG_RLO_ALL);
        }
    }
};

int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {
    D3DResourceLeakChecker leakChecker;

    // スマートポインタでGameインスタンスを生成（delete不要）
    std::unique_ptr<Game> game = std::make_unique<Game>();

    // 実行
    game->Run();

    return 0;
}