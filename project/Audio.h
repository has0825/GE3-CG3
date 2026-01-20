#pragma once
#include <Windows.h>
#include <wrl.h>
#include <string>
#include <vector>
#include <xaudio2.h>

// 音声データを保持する構造体
struct SoundData {
    WAVEFORMATEX wfex;
    std::vector<BYTE> pBuffer;
    unsigned int bufferSize;
};

class Audio {
public:
    // シングルトンインスタンス取得 (追加)
    static Audio* GetInstance();

    // 初期化
    void Initialize();
    // 終了処理
    void Finalize();

    // 音声ファイルの読み込み
    SoundData LoadAudio(const std::string& filename);

    // 音声再生
    void PlayWave(const SoundData& soundData, bool loop = false, float volume = 1.0f);

private:
    // コンストラクタ隠蔽 (シングルトン化)
    Audio() = default;
    ~Audio() = default;
    Audio(const Audio&) = delete;
    Audio& operator=(const Audio&) = delete;

private:
    Microsoft::WRL::ComPtr<IXAudio2> xAudio2_;
    IXAudio2MasteringVoice* masterVoice_ = nullptr;
};