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

// シングルトン実装 (追加)
Audio* Audio::GetInstance() {
    static Audio instance;
    return &instance;
}

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

    // ソースリーダーの作成
    IMFSourceReader* pMFSourceReader = nullptr;
    std::wstring wFilename(filename.begin(), filename.end()); // string -> wstring変換

    HRESULT result = MFCreateSourceReaderFromURL(wFilename.c_str(), NULL, &pMFSourceReader);
    if (FAILED(result)) {
        assert(false && "Failed to open audio file.");
        return soundData;
    }

    // メディアタイプの設定 (PCM)
    IMFMediaType* pMFMediaType = nullptr;
    MFCreateMediaType(&pMFMediaType);
    pMFMediaType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
    pMFMediaType->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_PCM);
    pMFSourceReader->SetCurrentMediaType(static_cast<DWORD>(MF_SOURCE_READER_FIRST_AUDIO_STREAM), NULL, pMFMediaType);

    pMFMediaType->Release();
    pMFMediaType = nullptr;
    pMFSourceReader->GetCurrentMediaType(static_cast<DWORD>(MF_SOURCE_READER_FIRST_AUDIO_STREAM), &pMFMediaType);

    // フォーマットの取得
    WAVEFORMATEX* waveFormat = nullptr;
    UINT32 waveFormatSize = 0;
    MFCreateWaveFormatExFromMFMediaType(pMFMediaType, &waveFormat, &waveFormatSize);
    soundData.wfex = *waveFormat;
    CoTaskMemFree(waveFormat);
    pMFMediaType->Release();

    // データの読み込み
    std::vector<BYTE> mediaData;
    while (true) {
        IMFSample* sample = nullptr;
        DWORD flags = 0;
        result = pMFSourceReader->ReadSample(static_cast<DWORD>(MF_SOURCE_READER_FIRST_AUDIO_STREAM), 0, nullptr, &flags, nullptr, &sample);
        assert(SUCCEEDED(result));

        if (flags & MF_SOURCE_READERF_ENDOFSTREAM) {
            break;
        }

        Microsoft::WRL::ComPtr<IMFMediaBuffer> buffer;
        result = sample->ConvertToContiguousBuffer(&buffer);
        assert(SUCCEEDED(result));

        BYTE* pAudioData = nullptr;
        DWORD cbCurrentLength = 0;
        result = buffer->Lock(&pAudioData, nullptr, &cbCurrentLength);
        assert(SUCCEEDED(result));

        mediaData.insert(mediaData.end(), pAudioData, pAudioData + cbCurrentLength);

        buffer->Unlock();
        sample->Release();
    }

    // データ格納
    soundData.pBuffer = std::move(mediaData);
    soundData.bufferSize = static_cast<unsigned int>(soundData.pBuffer.size());

    if (pMFSourceReader) {
        pMFSourceReader->Release();
    }

    return soundData;
}

void Audio::PlayWave(const SoundData& soundData, bool loop, float volume) {
    HRESULT result;

    // ソースボイスの作成
    IXAudio2SourceVoice* pSourceVoice = nullptr;
    result = xAudio2_->CreateSourceVoice(&pSourceVoice, &soundData.wfex);
    assert(SUCCEEDED(result));

    // バッファの設定
    XAUDIO2_BUFFER buf{};
    buf.pAudioData = soundData.pBuffer.data();
    buf.AudioBytes = soundData.bufferSize;
    buf.Flags = XAUDIO2_END_OF_STREAM;
    if (loop) {
        buf.LoopCount = XAUDIO2_LOOP_INFINITE;
    }

    // 再生
    result = pSourceVoice->SubmitSourceBuffer(&buf);
    result = pSourceVoice->SetVolume(volume);
    result = pSourceVoice->Start();
}