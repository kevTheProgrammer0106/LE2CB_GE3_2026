#include "SoundManager.h"

SoundData SoundLoadWave(const char* fileName)
{
	//ファイル入力ストリームのインスタンス
	std::ifstream file;
	// .wav ファイルをバイナリモードで開く
	file.open(fileName, std::ios_base::binary);
	// ファイルが開けなかった場合はエラー
	assert(file.is_open());

	// RIFFヘッダーの読み込み
	RiffHeader riff;
	file.read((char*)&riff, sizeof(riff));
	// ファイルがRIFFかチェック
	if (strncmp(riff.chunk.id, "RIFF", 4) != 0)
	{
		assert(0 && "Not a RIFF file");
	}
	// WAVEフォーマットかチェック
	if (strncmp(riff.type, "WAVE", 4) != 0)
	{
		assert(0 && "Not a WAV file");
	}

	//Formatチャンクの読み込み
	FormatChunk format = {};

	file.read((char*)&format, sizeof(ChunkHeader));
	if (strncmp(format.chunk.id, "fmt ", 4) != 0)
	{
		assert(0 && "Not a fmt chunk");
	}

	//チャンク本体の読み込み
	assert(format.chunk.size <= sizeof(format.fmt));
	file.read((char*)&format.fmt, format.chunk.size);

	// Dataチャンクの読み込み
	ChunkHeader data;
	file.read((char*)&data, sizeof(data));
	// JUNKチャンクを検出した場合
	if (strncmp(data.id, "JUNK", 4) == 0)
	{
		// JUNKチャンクのサイズ分だけ読み飛ばす
		file.seekg(data.size, std::ios_base::cur);
		// Dataチャンクを再度読み込む
		file.read((char*)&data, sizeof(data));
	}

	if (strncmp(data.id, "data", 4) != 0)
	{
		assert(0 && "Not a data chunk");
	}

	// Dataチャンクのデータ部(波形データ)の読み込み
	char* pBuffer = new char[data.size];
	file.read(pBuffer, data.size);

	// Waveファイルを閉じる
	file.close();

	// returnする為の音声データ
	SoundData soundData = {};

	soundData.wfex = format.fmt;
	soundData.pBuffer = reinterpret_cast<BYTE*>(pBuffer);
	soundData.bufferSize = data.size;

	return soundData;
}
void SoundUnload(SoundData* soundData)
{
	// バッファのメモリを解放
	delete[] soundData->pBuffer;

	soundData->pBuffer = 0;
	soundData->bufferSize = 0;
	soundData->wfex = {};
}

void SoundPlayWave( IXAudio2* xAudio2, const SoundData& soundData, IXAudio2SourceVoice*& pSourceVoice)
{
	assert(xAudio2 != nullptr);
	assert(soundData.pBuffer != nullptr);

	// 同じボイスが残っている場合は、先に破棄する
	if ( pSourceVoice != nullptr )
	{
		pSourceVoice->Stop(0);
		pSourceVoice->FlushSourceBuffers();
		pSourceVoice->DestroyVoice();
		pSourceVoice = nullptr;
	}

	// 音声データのフォーマットに合わせてボイスを生成する
	HRESULT result =
		xAudio2->CreateSourceVoice(&pSourceVoice, &soundData.wfex);

	assert(SUCCEEDED(result));
	if ( FAILED(result) )
	{
		pSourceVoice = nullptr;
		return;
	}

	// 再生する波形データを設定する
	XAUDIO2_BUFFER buffer{};
	buffer.pAudioData = soundData.pBuffer;
	buffer.AudioBytes = soundData.bufferSize;
	buffer.Flags = XAUDIO2_END_OF_STREAM;

	result = pSourceVoice->SubmitSourceBuffer(&buffer);
	if ( FAILED(result) )
	{
		pSourceVoice->DestroyVoice();
		pSourceVoice = nullptr;
		return;
	}

	// 音声の再生を開始する
	result = pSourceVoice->Start(0);
	if ( FAILED(result) )
	{
		pSourceVoice->DestroyVoice();
		pSourceVoice = nullptr;
	}
}

void SoundStopWave(IXAudio2SourceVoice*& pSourceVoice)
{
	// ボイスが存在しなければ何もしない
	if ( pSourceVoice == nullptr )
	{
		return;
	}

	// 再生を停止する
	HRESULT result = pSourceVoice->Stop(0);
	assert(SUCCEEDED(result));

	// 再生待ちのバッファをキューから削除する
	result = pSourceVoice->FlushSourceBuffers();
	assert(SUCCEEDED(result));

	// ボイスを破棄して、ポインタを無効化する
	pSourceVoice->DestroyVoice();
	pSourceVoice = nullptr;
}