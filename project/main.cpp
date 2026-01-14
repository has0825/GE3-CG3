#include "Game.h"
#include <Windows.h>
#include <dxgidebug.h>

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

    // ゲームクラスのインスタンス生成
    Game* game = new Game();

    // 実行
    game->Run();

    // 解放
    delete game;

    return 0;
}