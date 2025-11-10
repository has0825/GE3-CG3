#pragma once
#include <wrl.h>
#include <dinput.h>
#include <cstdint>


class WinApp;

class Input {
public:

    Input();
    ~Input();

    Input(const Input&) = delete;
    const Input& operator=(const Input&) = delete;

    void Initialize(WinApp* winApp);
    void Finalize();
    void Update();

    bool IsKeyPressed(uint8_t keyCode);
    bool IsKeyTriggered(uint8_t keyCode);
    bool IsKeyReleased(uint8_t keyCode);

private:
    
    Microsoft::WRL::ComPtr<IDirectInput8> directInput_ = nullptr;
    Microsoft::WRL::ComPtr<IDirectInputDevice8> keyboard_ = nullptr;

    BYTE keys_[256] = {};
    BYTE prevKeys_[256] = {};
};