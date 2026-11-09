#include "FrameTimer.h"

#include <algorithm>
#include <thread>

void FrameTimer::Initialize(uint32_t targetFps)
{
	targetFps_ = targetFps;
	//現在の時間を記録する
	referenceFPS_ = Clock::now();
}

float FrameTimer::Update()
{
	if ( targetFps_ != 0 )
	{
		// 1/目標FPS秒ぴったりの時間
		const std::chrono::microseconds kMinTime(uint64_t(1000000.0f / float(targetFps_)));
		// 1/目標FPS秒よりわずかに短い時間(60FPSなら1/65秒)
		const std::chrono::microseconds kMinCheckTime(uint64_t(1000000.0f / ( float(targetFps_) * 65.0f / 60.0f )));

		//現在の時間を取得する
		Clock::time_point currentTime = Clock::now();
		// 前回の時間からの経過時間を計算する
		std::chrono::microseconds elapsedTime = std::chrono::duration_cast< std::chrono::microseconds >( currentTime - referenceFPS_ );

		// 1/目標FPS秒 (よりわずかに短い時間) 経ってない場合
		if ( elapsedTime < kMinCheckTime )
		{
			// 1/目標FPS秒経過するまで微小なスリープを繰り返す
			while ( Clock::now() - referenceFPS_ < kMinTime )
			{
				// 1マイクロ秒スリープする
				std::this_thread::sleep_for(std::chrono::microseconds(1));
			}
		}
	}

	// 待った後の時間から、前フレームからの実測経過時間を求める
	const Clock::time_point now = Clock::now();
	float deltaTime = std::chrono::duration< float >(now - referenceFPS_).count();
	//現在の時間を記録する
	referenceFPS_ = now;

	if ( deltaTime > 0.0f )
	{
		fps_ = 1.0f / deltaTime;
	}
	return std::min(deltaTime, kMaxDeltaTime);
}