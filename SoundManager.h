#pragma once
#define _USE_MATH_DEFINES
#include "DirectXTex.h"
#include "externals/DirectXTex/d3dx12.h"
#include <vector>

#include <math.h>

#include <fstream>
#include <sstream>

#include<wrl.h>

#include<xaudio2.h>
#pragma comment(lib,"xaudio2.lib")

//チャンクヘッダ
struct ChunkHeader
{
	char id[4]; // チャンクのID
	int32_t size; // チャンクのサイズ
};

// RIFFヘッダチャンク
struct RiffHeader
{
	ChunkHeader chunk; // チャンクヘッダ
	char type[4]; // RIFFのタイプ
};

//FMTチャンク
struct FormatChunk
{
	ChunkHeader chunk; // チャンクヘッダ
	WAVEFORMATEX fmt; // WAVEフォーマット
};

//音声データ
struct SoundData
{
	// 波形フォーマット
	WAVEFORMATEX wfex;
	// バッファの先頭アドレス
	BYTE* pBuffer;
	// バッファのサイズ
	unsigned int bufferSize;
};

SoundData SoundLoadWave(const char* fileName);

//音声データ解放
void SoundUnload(SoundData* soundData);

// 音声再生
void SoundPlayWave(IXAudio2* xAudio2, const SoundData& soundData, IXAudio2SourceVoice*& pSourceVoice);

void SoundStopWave(IXAudio2SourceVoice*& pSourceVoice);

class SoundManager{};

