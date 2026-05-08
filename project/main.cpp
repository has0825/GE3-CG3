#include "Game.h"
#include <Windows.h>
#include <dxgidebug.h>
#include <memory> 
// D3D12のリソースリークをチェックするための構造体
struct D3DResourceLeakChecker {
    ~D3DResourceLeakChecker() {
        Microsoft::WRL::ComPtr<IDXGIDebug1> debug;
        if (SUCCEEDED(DXGIGetDebugInterface1(0, IID_PPV_ARGS(&debug)))) {
            debug->ReportLiveObjects(DXGI_DEBUG_ALL, DXGI_DEBUG_RLO_ALL);
        }
    }
};

int WINAPI WinMain(_In_ HINSTANCE, _In_opt_ HINSTANCE, _In_ LPSTR, _In_ int) {

    D3DResourceLeakChecker leakChecker;

    std::unique_ptr<Game> game = std::make_unique<Game>();

    // 実行
    game->Run();

    return 0;
}