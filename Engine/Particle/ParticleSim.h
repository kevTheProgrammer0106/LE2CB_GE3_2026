#pragma once

#include <random>

#include "Engine/Particle/ParticleConfig.h"
#include "Engine/Math/WorldTransform.h"

//===================================================================
// パーティクルの計算部分(GPU・ウィンドウに依存しない純粋な関数)。
// ParticleEmitter が使う。ここに分けてあるので、単体でテストできる。
//===================================================================
namespace ParticleSim
{
	/// 補間カーブ。t = 0〜1。Pulseだけは start → end → start と往復する
	float ApplyCurve(ParticleCurve curve, float start, float end, float t);

	/// 3次元の値ノイズ(-1〜1)。格子点のハッシュを滑らかに補間する。乱流(turbulence)に使う
	float Noise3(float x, float y, float z);

	/// 乱流の加速度。position と age を入力に、各軸 -1〜1 のノイズ × strength を返す。
	/// seed は粒ごとの位相のずれ(同じ場所でも粒ごとに違う動きにする)
	Vector3 TurbulenceAccel(const Vector3& position, float age, float seed, float strength, float frequency, float speed);

	/// 発生範囲の1サンプル。座標は「Yが上」の空間(2Dに使うときは呼び出し側でYを反転する)
	struct ShapeSample
	{
		Vector3 offset;  // エミッタ位置からのずれ
		Vector3 outward; // 外向きの単位ベクトル(Lineなど決められない形は長さ0)
	};
	/// config.shape に従って発生位置を1つ選ぶ。Boxは spawnExtent、Lineは spawnExtent.x、他は shapeRadius。
	/// 2Dでは Z を使わず、Sphere / Hemisphere / Circle は円になる
	ShapeSample SampleEmitShape(const ParticleConfig& config, bool is2D, std::mt19937& rng);

	/// 寿命の割合 t(0〜1)での色。3色グラデーション(useMid)とフェードイン(fadeIn)を含む
	Vector4 EvaluateColor(const Vector4& start, const Vector4& mid, bool useMid, float midTime, const Vector4& end,
		ParticleCurve curve, float fadeIn, float t);

	/// 発生のタイミング管理(startDelay・バースト・自動発生の開始)
	class Scheduler
	{
	public:
		void Reset();
		/// 時間を進めて、このフレームに出すバーストの粒数(複数回ぶんの合計)を返す
		int Advance(float deltaTime, const ParticleConfig& config);
		/// startDelay が過ぎて、発生してよい状態か
		bool HasStarted(const ParticleConfig& config) const;

		float GetTime() const
		{
			return time_;
		}
		int GetBurstsFired() const
		{
			return burstsFired_;
		}

	private:
		float time_ = 0.0f;
		int burstsFired_ = 0;
	};
}
