#include "Audio.h"
#include <cassert>

// Media Foundation 関連のヘッダーとライブラリ
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>

#pragma comment(lib, "xaudio2.lib")
#pragma comment(lib, "mfplat.lib")
#pragma comment(lib, "mfreadwrite.lib")
#pragma comment(lib, "mfuuid.lib")

void Audio::Initialize() {
	HRESULT result;

	// XAudio2エンジンのインスタンス生成
	result = XAudio2Create(&xAudio2_, 0, XAUDIO2_DEFAULT_PROCESSOR);
	assert(SUCCEEDED(result));

	// マスターボイスの生成
	result = xAudio2_->CreateMasteringVoice(&masterVoice_);
	assert(SUCCEEDED(result));

	// Media Foundation の初期化
	result = MFStartup(MF_VERSION);
	assert(SUCCEEDED(result));
}

void Audio::Finalize() {
	// XAudio2はComPtrが自動解放してくれますが、MasterVoiceは明示的に破棄が必要な場合がある
	if (masterVoice_) {
		masterVoice_->DestroyVoice();
		masterVoice_ = nullptr;
	}
	xAudio2_.Reset();

	// Media Foundation の終了
	MFShutdown();
}

SoundData Audio::LoadAudio(const std::string& filename) {
	SoundData soundData = {};
	HRESULT result;

	// ファイル名をワイド文字に変換
	std::wstring wFilename = std::wstring(filename.begin(), filename.end());

	// 1. SourceReaderの作成
	Microsoft::WRL::ComPtr<IMFSourceReader> sourceReader;
	result = MFCreateSourceReaderFromURL(wFilename.c_str(), NULL, &sourceReader);
	assert(SUCCEEDED(result));

	// 2. メディアタイプの選択 (オーディオストリームを選択)
	Microsoft::WRL::ComPtr<IMFMediaType> mediaType;
	result = sourceReader->GetCurrentMediaType((DWORD)MF_SOURCE_READER_FIRST_AUDIO_STREAM, &mediaType);
	assert(SUCCEEDED(result));

	// 3. 読み込みフォーマットをPCMに設定 (圧縮解除設定)
	result = mediaType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
	assert(SUCCEEDED(result));
	result = mediaType->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_PCM);
	assert(SUCCEEDED(result));

	// 設定したメディアタイプをSourceReaderにセット
	result = sourceReader->SetCurrentMediaType((DWORD)MF_SOURCE_READER_FIRST_AUDIO_STREAM, NULL, mediaType.Get());
	assert(SUCCEEDED(result));

	// 4. 最終的なフォーマットを取得 (WAVEFORMATEXに変換して保持)
	Microsoft::WRL::ComPtr<IMFMediaType> outputMediaType;
	result = sourceReader->GetCurrentMediaType((DWORD)MF_SOURCE_READER_FIRST_AUDIO_STREAM, &outputMediaType);
	assert(SUCCEEDED(result));

	UINT32 waveFormatSize = 0;
	WAVEFORMATEX* waveFormat = nullptr;
	result = MFCreateWaveFormatExFromMFMediaType(outputMediaType.Get(), &waveFormat, &waveFormatSize);
	assert(SUCCEEDED(result));

	// 構造体にフォーマット情報をコピー
	soundData.wfex = *waveFormat;
	CoTaskMemFree(waveFormat); // 一時バッファの解放

	// 5. データの読み込みループ
	std::vector<BYTE> mediaData;
	while (true) {
		Microsoft::WRL::ComPtr<IMFSample> sample;
		DWORD flags = 0;
		result = sourceReader->ReadSample(
			(DWORD)MF_SOURCE_READER_FIRST_AUDIO_STREAM,
			0, nullptr, &flags, nullptr, &sample);
		assert(SUCCEEDED(result));

		// 読み込み終了チェック
		if (flags & MF_SOURCE_READERF_ENDOFSTREAM) {
			break;
		}

		// サンプル取得
		Microsoft::WRL::ComPtr<IMFMediaBuffer> buffer;
		result = sample->ConvertToContiguousBuffer(&buffer);
		assert(SUCCEEDED(result));

		BYTE* pAudioData = nullptr;
		DWORD cbCurrentLength = 0;
		result = buffer->Lock(&pAudioData, nullptr, &cbCurrentLength);
		assert(SUCCEEDED(result));

		// データを一時バッファに追加
		mediaData.insert(mediaData.end(), pAudioData, pAudioData + cbCurrentLength);

		buffer->Unlock();
	}

	// 読み込んだデータをSoundDataに格納
	soundData.pBuffer = std::move(mediaData);
	soundData.bufferSize = static_cast<unsigned int>(soundData.pBuffer.size());

	return soundData;
}

void Audio::PlayWave(const SoundData& soundData, bool loop, float volume) {
	HRESULT result;

	// ソースボイスの作成
	IXAudio2SourceVoice* pSourceVoice = nullptr;
	result = xAudio2_->CreateSourceVoice(&pSourceVoice, &soundData.wfex);
	assert(SUCCEEDED(result));

	// バッファの設定
	XAUDIO2_BUFFER buffer = {};
	buffer.pAudioData = soundData.pBuffer.data();
	buffer.AudioBytes = soundData.bufferSize;
	buffer.Flags = XAUDIO2_END_OF_STREAM;

	if (loop) {
		buffer.LoopCount = XAUDIO2_LOOP_INFINITE; // 無限ループ
	}

	// 波形データの送信
	result = pSourceVoice->SubmitSourceBuffer(&buffer);
	assert(SUCCEEDED(result));

	// 音量設定
	pSourceVoice->SetVolume(volume);

	// 再生開始
	result = pSourceVoice->Start();
	assert(SUCCEEDED(result));

}