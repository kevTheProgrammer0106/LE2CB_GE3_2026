#include "VirtualisEngine/Audio/SoundManager.h"

#include <cstring>
#include <fstream>

#include "VirtualisEngine/Core/Logger.h"

#pragma comment(lib, "xaudio2.lib")

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
		xAudio2_.Reset();
		return false;
	}
	hr = xAudio2_->CreateMasteringVoice(&masteringVoice_);
	if ( FAILED(hr) )
	{
		Logger::Log("[SoundManager] CreateMasteringVoice failed. Sound is disabled.\n");
		xAudio2_.Reset();
		return false;
	}
	return true;
}

void SoundManager::Shutdown()
{
	if ( masteringVoice_ )
	{
		masteringVoice_->DestroyVoice();
		masteringVoice_ = nullptr;
	}
	xAudio2_.Reset();
}

SoundData SoundManager::LoadWave(const std::string& filePath)
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
