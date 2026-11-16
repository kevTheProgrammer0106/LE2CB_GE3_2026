#include "VirtualisEngine/Audio/SoundManager.h"

#include <cstring>
#include <fstream>

#include <mfapi.h>
#include <mfidl.h>        // IMFMediaSource / IMFMediaSink
#include <mfreadwrite.h>  // IMFSourceReader / IMFSinkWriter

#include "VirtualisEngine/Core/Logger.h"
#include "StringUtility.h"


#pragma comment(lib, "xaudio2.lib")
#pragma comment(lib, "mfplat.lib")
#pragma comment(lib, "mfreadwrite.lib")
#pragma comment(lib, "mfuuid.lib") // MFStartupに必要

using namespace Microsoft::WRL;

namespace
{
	// RIFFファイルの「チャンク」の頭(名前4文字 + サイズ)
	struct ChunkHeader
	{
		char id[4];
		int32_t size;
	};

	// RIFFヘッダ(先頭のチャンク + "WAVE")
	struct RiffHeader
	{
		ChunkHeader chunk;
		char type[4];
	};
}

SoundManager::~SoundManager()
{
	Shutdown();
}

bool SoundManager::Initialize()
{
	HRESULT hr = XAudio2Create(&xAudio2_, 0, XAUDIO2_DEFAULT_PROCESSOR);
	if ( FAILED(hr) )
	{
		Logger::Log("[SoundManager] XAudio2Create failed. Sound is disabled.\n");
		Shutdown();
		return false;
	}

	hr = xAudio2_->CreateMasteringVoice(&masteringVoice_);
	if ( FAILED(hr) )
	{
		Logger::Log("[SoundManager] CreateMasteringVoice failed.\n");
		Shutdown();
		return false;
	}

	// Media Foundationを初期化して音声デコードを使用可能にする
	hr = MFStartup(MF_VERSION, MFSTARTUP_NOSOCKET);
	if ( FAILED(hr) )
	{
		Logger::Log("[SoundManager] MFStartup failed.\n");
		Shutdown();
		return false;
	}

	mfStarted_ = true;
	return true;
}

void SoundManager::Shutdown()
{
	// Media Foundationを初期化した場合だけ終了する
	if ( mfStarted_ )
	{
		MFShutdown();
		mfStarted_ = false;
	}

	if ( masteringVoice_ )
	{
		masteringVoice_->DestroyVoice();
		masteringVoice_ = nullptr;
	}

	xAudio2_.Reset();
}

SoundData SoundManager::LoadSoundWav(const std::string& filePath)
{
	SoundData sound;

	std::ifstream file(filePath, std::ios_base::binary);
	if ( !file.is_open() )
	{
		Logger::Log("[SoundManager] Cannot open: " + filePath + "\n");
		return sound;
	}

	// RIFFヘッダー(RIFF/WAVEかを確かめる)
	RiffHeader riff{};
	file.read(reinterpret_cast< char* >( &riff ), sizeof(riff));
	if ( !file || std::strncmp(riff.chunk.id, "RIFF", 4) != 0 || std::strncmp(riff.type, "WAVE", 4) != 0 )
	{
		Logger::Log("[SoundManager] Not a WAV file: " + filePath + "\n");
		return sound;
	}

	// fmtチャンク(波形のフォーマット)
	ChunkHeader fmtHeader{};
	file.read(reinterpret_cast< char* >( &fmtHeader ), sizeof(fmtHeader));
	if ( !file || std::strncmp(fmtHeader.id, "fmt ", 4) != 0 || fmtHeader.size < 16 ||
		fmtHeader.size > static_cast< int32_t >( sizeof(WAVEFORMATEX) ) )
	{
		Logger::Log("[SoundManager] Unsupported fmt chunk: " + filePath + "\n");
		return sound;
	}
	file.read(reinterpret_cast< char* >( &sound.format ), fmtHeader.size);
	if ( fmtHeader.size & 1 )
	{
		file.seekg(1, std::ios_base::cur); // 詰め物の1バイト
	}

	// PCM以外(ADPCMなど)は再生できないので、ここで弾く
	if ( !file || sound.format.wFormatTag != WAVE_FORMAT_PCM )
	{
		Logger::Log("[SoundManager] PCM以外のWAVは未対応です: " + filePath + "\n");
		return SoundData{};
	}

	// dataチャンクまで読み進める(JUNKなど、関係ないチャンクは読み飛ばす)
	ChunkHeader dataHeader{};
	while ( file.read(reinterpret_cast< char* >( &dataHeader ), sizeof(dataHeader)) )
	{
		if ( dataHeader.size < 0 )
		{
			break; // 壊れたファイル
		}
		if ( std::strncmp(dataHeader.id, "data", 4) == 0 )
		{
			sound.samples.resize(static_cast< size_t >( dataHeader.size ));
			file.read(reinterpret_cast< char* >( sound.samples.data() ), dataHeader.size);
			// ファイルが途中で終わっていたら、読めた分だけにする
			sound.samples.resize(static_cast< size_t >( file.gcount() ));
			return sound;
		}
		// RIFFのチャンクは偶数バイト境界。奇数サイズなら詰め物の1バイトも読み飛ばす
		file.seekg(dataHeader.size + ( dataHeader.size & 1 ), std::ios_base::cur);
	}

	Logger::Log("[SoundManager] No data chunk: " + filePath + "\n");
	return SoundData{};
}

SoundData SoundManager::LoadSoundMp3(const std::string& fileName)
{
    SoundData sound;

    if ( !mfStarted_ )
    {
        Logger::Log("[SoundManager] Media Foundation is not initialized.\n");
        return sound;
    }

    // ファイルパスをワイド文字列に変換する
    const std::wstring filePathW = StringUtility::ConvertString(fileName);

    // Source Readerを作成する
    ComPtr<IMFSourceReader> sourceReader;
    HRESULT hr = MFCreateSourceReaderFromURL(
        filePathW.c_str(),
        nullptr,
        &sourceReader
    );

    if ( FAILED(hr) )
    {
        Logger::Log("[SoundManager] Failed to open audio file: " + fileName + "\n");
        return sound;
    }

    // 出力形式をPCMに指定する
    ComPtr<IMFMediaType> pcmType;
    hr = MFCreateMediaType(&pcmType);
    if ( FAILED(hr) )
    {
        Logger::Log("[SoundManager] MFCreateMediaType failed.\n");
        return sound;
    }

    hr = pcmType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
    if ( FAILED(hr) )
    {
        return sound;
    }

    hr = pcmType->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_PCM);
    if ( FAILED(hr) )
    {
        return sound;
    }

    // MP3をPCMにデコードするようSource Readerに要求する
    hr = sourceReader->SetCurrentMediaType(
        MF_SOURCE_READER_FIRST_AUDIO_STREAM,
        nullptr,
        pcmType.Get()
    );

    if ( FAILED(hr) )
    {
        Logger::Log("[SoundManager] Failed to configure PCM output: " + fileName + "\n");
        return sound;
    }

    // 実際に使用されるPCM形式を取得する
    ComPtr<IMFMediaType> outputType;
    hr = sourceReader->GetCurrentMediaType(
        MF_SOURCE_READER_FIRST_AUDIO_STREAM,
        &outputType
    );

    if ( FAILED(hr) )
    {
        Logger::Log("[SoundManager] Failed to get output media type.\n");
        return sound;
    }

    // Media Foundationの形式をXAudio2用のWAVEFORMATEXに変換する
    WAVEFORMATEX* waveFormat = nullptr;
    UINT32 waveFormatSize = 0;

    hr = MFCreateWaveFormatExFromMFMediaType(
        outputType.Get(),
        &waveFormat,
        &waveFormatSize
    );

    if ( FAILED(hr) )
    {
        Logger::Log("[SoundManager] Failed to create WAVEFORMATEX.\n");
        return sound;
    }

    // 現在のSoundDataは標準のPCM WAVEFORMATEXを保持する設計
    // 拡張形式は現状未対応
    if ( waveFormatSize < sizeof(WAVEFORMATEX) ||
        waveFormat->wFormatTag != WAVE_FORMAT_PCM ||
        waveFormat->cbSize != 0 ||
        waveFormat->nBlockAlign == 0 )
    {
        CoTaskMemFree(waveFormat);
        Logger::Log("[SoundManager] Unsupported decoded audio format.\n");
        return sound;
    }

    std::memcpy(&sound.format, waveFormat, sizeof(WAVEFORMATEX));
    CoTaskMemFree(waveFormat);

    // 音声ストリームを選択する
    hr = sourceReader->SetStreamSelection(
        MF_SOURCE_READER_FIRST_AUDIO_STREAM,
        TRUE
    );

    if ( FAILED(hr) )
    {
        Logger::Log("[SoundManager] Failed to select audio stream.\n");
        return SoundData{};
    }

    // デコードされたPCMサンプルを最後まで読み込む
    while ( true )
    {
        DWORD actualStreamIndex = 0;
        DWORD streamFlags = 0;
        LONGLONG timestamp = 0;

        ComPtr<IMFSample> sample;

        hr = sourceReader->ReadSample(
            MF_SOURCE_READER_FIRST_AUDIO_STREAM,
            0,
            &actualStreamIndex,
            &streamFlags,
            &timestamp,
            &sample
        );

        if ( FAILED(hr) )
        {
            Logger::Log("[SoundManager] Failed to read audio sample.\n");
            return SoundData{};
        }

        // ファイルの最後まで読み込んだ
        if ( streamFlags & MF_SOURCE_READERF_ENDOFSTREAM )
        {
            break;
        }

        if ( sample == nullptr )
        {
            continue;
        }

        // サンプルを連続したメモリバッファに変換する
        ComPtr<IMFMediaBuffer> mediaBuffer;
        hr = sample->ConvertToContiguousBuffer(&mediaBuffer);

        if ( FAILED(hr) )
        {
            Logger::Log("[SoundManager] Failed to convert audio buffer.\n");
            return SoundData{};
        }

        BYTE* bufferData = nullptr;
        DWORD maxLength = 0;
        DWORD currentLength = 0;

        // バッファの内容をロックして取得する
        hr = mediaBuffer->Lock(
            &bufferData,
            &maxLength,
            &currentLength
        );

        if ( FAILED(hr) )
        {
            Logger::Log("[SoundManager] Failed to lock audio buffer.\n");
            return SoundData{};
        }

        // デコードされたPCMデータをSoundDataに蓄積する
        if ( currentLength > 0 )
        {
            sound.samples.insert(
                sound.samples.end(),
                bufferData,
                bufferData + currentLength
            );
        }

        hr = mediaBuffer->Unlock();

        if ( FAILED(hr) )
        {
            Logger::Log("[SoundManager] Failed to unlock audio buffer.\n");
            return SoundData{};
        }
    }

    // PCMのブロック境界に合わせる
    const size_t remainder = sound.samples.size() % sound.format.nBlockAlign;

    if ( remainder > 0 )
    {
        sound.samples.resize(sound.samples.size() - remainder);
    }

    if ( !sound.IsValid() )
    {
        Logger::Log("[SoundManager] Decoded audio contains no samples: " + fileName + "\n");
        return SoundData{};
    }

    Logger::Log("[SoundManager] Loaded audio: " + fileName + "\n");

    return sound;
}

IXAudio2SourceVoice* SoundManager::Play(const SoundData& sound, bool loop, float volume)
{
	if ( !xAudio2_ || !sound.IsValid() )
	{
		return nullptr;
	}

	// 音声データのフォーマットに合わせてボイスを作る
	IXAudio2SourceVoice* voice = nullptr;
	if ( FAILED(xAudio2_->CreateSourceVoice(&voice, &sound.format)) )
	{
		return nullptr;
	}
	voice->SetVolume(volume);

	// 再生する波形データを設定する
	XAUDIO2_BUFFER buffer{};
	buffer.pAudioData = sound.samples.data();
	buffer.AudioBytes = static_cast< UINT32 >( sound.samples.size() );
	buffer.Flags = XAUDIO2_END_OF_STREAM;
	buffer.LoopCount = loop ? XAUDIO2_LOOP_INFINITE : 0;

	if ( FAILED(voice->SubmitSourceBuffer(&buffer)) || FAILED(voice->Start(0)) )
	{
		voice->DestroyVoice();
		return nullptr;
	}
	return voice;
}

void SoundManager::Stop(IXAudio2SourceVoice*& voice)
{
	if ( voice == nullptr )
	{
		return;
	}
	voice->Stop(0);
	voice->FlushSourceBuffers();
	voice->DestroyVoice();
	voice = nullptr;
}
