#pragma once

#include <wrl.h>
#include <xaudio2.h>

#include <cstdint>
#include <string>
#include <vector>

/// 読み込んだ音声データ(wavの波形そのもの)。メモリは vector が持つので、解放を忘れてもリークしない
struct SoundData
{
	WAVEFORMATEX format{};
	std::vector<BYTE> samples;

	bool IsValid() const
	{
		return !samples.empty();
	}
};

/// <summary>
/// 音の再生。XAudio2のエンジンとマスターボイスを持つ。
///
///   SoundManager sound;
///   sound.Initialize();
///   SoundData jump = sound.LoadWave("resources/jump.wav");
///   IXAudio2SourceVoice* voice = sound.Play(jump);   // 鳴らした音のボイス。止めたいときに Stop に渡す
///   sound.Stop(voice);
///
/// SoundData を使っている再生が終わるまで、SoundData を破棄しないこと。
/// COM(CoInitializeEx)はWinMainで初期化済みの前提
/// </summary>
class SoundManager
{
public:
	SoundManager() = default;
	SoundManager(const SoundManager&) = delete;
	SoundManager& operator=(const SoundManager&) = delete;
	~SoundManager();

	/// XAudio2のエンジンとマスターボイスを作る。失敗したら false(音が出ないだけでゲームは続けられる)
	bool Initialize();
	void Shutdown();

	/// .wav(PCM)を読み込む。読めなかったら IsValid() が false の SoundData を返す
	static SoundData LoadSoundWav(const std::string& filePath);
	/// <summary>
	/// 音声ファイル読み込み
	/// <summary>
	/// <param name="fileName">音声ファイル名</param>
	// Media Foundationで読み込んだ音声をPCMに変換して返す
	SoundData LoadSoundMp3(const std::string& fileName);


	/// 再生する。同じ音を重ねて鳴らせる(1回ごとに新しいボイスを作る)。
	/// 戻り値は Stop() に渡すためのボイス。ループしない音は、鳴り終わっても Stop() で片付けること
	IXAudio2SourceVoice* Play(const SoundData& sound, bool loop = false, float volume = 1.0f);
	/// 止めてボイスを破棄し、ポインタを nullptr にする(nullptrを渡しても何もしない)
	static void Stop(IXAudio2SourceVoice*& voice);

private:
	Microsoft::WRL::ComPtr<IXAudio2> xAudio2_;
	IXAudio2MasteringVoice* masteringVoice_ = nullptr;

	bool mfStarted_ = false;
};
