#pragma once
#include <wrl.h>
#define DIRECTINPUT_VERSION 0x0800 // DirectInputのバージョン指定
#include <dinput.h>
#include <cstdint>

// 前方宣言
class WinApp;

/**
 * @class Input
 * @brief キーボード入力を管理するクラス (シングルトン)
 * @details DirectInputを使用してキーボードの状態を取得します。
 * Initialize()で初期化し、毎フレーム Update() を呼び出してください。
 */
class Input {
public:
    /**
     * @brief シングルトンインスタンスの取得
     * @return Inputクラスの静的インスタンス
     */
    static Input* GetInstance();

    /**
     * @brief 初期化処理
     * @param winApp WinAppのインスタンス (ウィンドウハンドルとインスタンスハンドル取得用)
     */
    void Initialize(WinApp* winApp);

    /**
     * @brief 終了処理
     */
    void Finalize();

    /**
     * @brief 毎フレーム処理
     * @details キーボードの状態をポーリングし、内部バッファを更新します。
     * ゲームループの先頭で呼び出してください。
     */
    void Update();

    // --- 条件判定のための bool 型関数 ---

    /**
     * @brief キーが押されているか (プレス)
     * @param keyCode DIK_SPACE や DIK_A などのDirectInputキーコード
     * @return 押されていれば true
     */
    bool IsKeyPressed(uint8_t keyCode);

    /**
     * @brief キーが押された瞬間か (トリガー)
     * @param keyCode DIK_SPACE や DIK_A などのDirectInputキーコード
     * @return 今フレームで押された瞬間なら true
     */
    bool IsKeyTriggered(uint8_t keyCode);

    /**
     * @brief キーが離された瞬間か (リリース)
     * @param keyCode DIK_SPACE や DIK_A などのDirectInputキーコード
     * @return 今フレームで離された瞬間なら true
     */
    bool IsKeyReleased(uint8_t keyCode);

    // ===========================================
    // ★追加: マウスボタンが押されているか判定
    // buttonNumber: 0=左クリック, 1=右クリック, 2=中クリック
    // ===========================================
    bool IsMousePressed(int buttonNumber);
    int32_t GetWheelDelta() const { return wheelDelta_; }

private:
    // シングルトンにするためのプライベートコンストラクタ
    Input() = default;
    ~Input() = default;
    Input(const Input&) = delete;
    const Input& operator=(const Input&) = delete;

private:
    // --- クラスに関連したデータ (メンバ変数) ---

    // DirectInputのコアインターフェース
    Microsoft::WRL::ComPtr<IDirectInput8> directInput_ = nullptr;
    // キーボードデバイス
    Microsoft::WRL::ComPtr<IDirectInputDevice8> keyboard_ = nullptr;

    // 現在のフレームのキー入力状態 (256キー分)
    BYTE keys_[256] = {};
    // 前のフレームのキー入力状態 (トリガー判定用)
    BYTE prevKeys_[256] = {};
    // マウスホイールのデルタ値
    int32_t wheelDelta_ = 0;
};