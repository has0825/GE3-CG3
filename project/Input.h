#pragma once
#include <Windows.h> // GetKeyboardState を使うために必要

// 達成条件: キーボード入力をクラス化できている
class Input {
public:
    /// <summary>
    /// コンストラクタ
    /// </summary>
    Input();

    /// <summary>
    /// デストラクタ
    /// </summary>
    ~Input();

    // 達成条件: 初期化処理と毎フレーム処理を分別して別の関数にまとめられる (初期化)
    /// <summary>
    /// 初期化処理
    /// </summary>
    void Initialize();

    // 達成条件: 初期化処理と毎フレーム処理を分別して別の関数にまとめられる (毎フレーム)
    /// <summary>
    /// 毎フレーム更新処理
    /// </summary>
    void Update();

    // 達成条件: 条件判定のための戻り値 bool 型の関数を自作できる
    /// <summary>
    /// 指定キーが押され続けているか (プレス)
    /// </summary>
    /// <param name="key">チェックしたいキー (VK_W など)</param>
    /// <returns>押されている: true, 押されていない: false</returns>
    bool IsKeyDown(BYTE key) const;

    /// <summary>
    /// 指定キーが押された瞬間か (トリガー)
    /// </summary>
    /// <param name="key">チェックしたいキー</param>
    /// <returns>押された瞬間: true, それ以外: false</returns>
    bool IsKeyPressed(BYTE key) const;

    /// <summary>
    /// 指定キーが離された瞬間か (リリース)
    /// </summary>
    /// <param name="key">チェックしたいキー</param>
    /// <returns>離された瞬間: true, それ以外: false</returns>
    bool IsKeyReleased(BYTE key) const;


private:
    // 達成条件: クラスに関連したデータをメンバ変数としてまとめられる

    /// <summary>
    /// 現在フレームのキーボード状態
    /// </summary>
    BYTE keys_[256] = {};

    /// <summary>
    /// 前フレームのキーボード状態 (押された瞬間・離された瞬間を判定するために使用)
    /// </summary>
    BYTE prevKeys_[256] = {};
};