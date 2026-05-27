#pragma once
#include <Windows.h>
#include <wrl.h>
#include <string>
#include <vector>
#include <xaudio2.h>

// 音声データを保持する構造体
// 読み込みと再生を分けるために使用します
struct SoundData {
	WAVEFORMATEX wfex;
	std::vector<BYTE> pBuffer;
	unsigned int bufferSize;
};

class Audio {
public:
	// 初期化
	void Initialize();
	// 終了処理
	void Finalize();

	// 音声ファイルの読み込み (.wav, .mp3, .aac等 対応)
	// 返り値としてSoundDataのハンドル(インデックス等)ではなく、構造体そのものを管理しやすい形で返します
	// 実際の運用ではハンドル管理する場合もありますが、今回は分かりやすくポインタ管理などは呼び出し元に任せるか、
	// 簡易的に構造体を返して保持してもらうスタイルにします。
	SoundData LoadAudio(const std::string& filename);

	// 音声再生
	// soundData: LoadAudioで読み込んだデータ
	// loop: ループ再生するかどうか
	// volume: 音量 (0.0f ~ 1.0f)
	void PlayWave(const SoundData& soundData, bool loop = false, float volume = 1.0f);

private:
	Microsoft::WRL::ComPtr<IXAudio2> xAudio2_;
	IXAudio2MasteringVoice* masterVoice_ = nullptr;
};