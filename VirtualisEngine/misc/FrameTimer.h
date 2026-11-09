#pragma once
#include <chrono>
#include <cstdint>

/// <summary>
/// フレーム時間の計測とFPS固定。
/// 元のmain.cppでコメントアウトされていた InitializeVSync / UpdateVSync(Frames per Second固定)を
/// クラスにして復活させたもの。さらに、実測した経過時間(deltaTime)を返す。
///
/// 使い方: ループの先頭で毎回 Update() を呼ぶ。
///   - 目標FPSが指定されていれば、1フレームが目標時間に達するまで待つ
///   - 戻り値は前フレームからの経過秒数(固定の1/60ではなく実測値)
/// </summary>
class FrameTimer
{
public:
	/// <param name="targetFps">目標FPS。0なら固定しない(垂直同期やGPU待ちに任せる)</param>
	void Initialize(uint32_t targetFps);

	/// 目標FPSに達するまで待ち、前フレームからの経過秒数を返す
	float Update();

	bool IsLimiting() const
	{
		return targetFps_ != 0;
	}
	/// 直近フレームの実測FPS(UI表示用)
	float GetFps() const
	{
		return fps_;
	}

private:
	using Clock = std::chrono::steady_clock;

	// 記録時間(FPS固定用)
	Clock::time_point referenceFPS_;
	uint32_t targetFps_ = 0;
	float fps_ = 0.0f;

	// ウィンドウのドラッグ移動中やブレークポイントで止まった後に、巨大なdeltaTimeで
	// シミュレーションが飛ばないようにする上限(秒)
	static constexpr float kMaxDeltaTime = 0.1f;
};