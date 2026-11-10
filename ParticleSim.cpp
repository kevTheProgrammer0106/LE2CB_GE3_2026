#define NOMINMAX
#include "ParticleSim.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <numbers>

#include "Easing.h"

namespace ParticleSim
{
	namespace
	{
		constexpr float kPi = std::numbers::pi_v<float>;

		float Rand(std::mt19937& rng, float minValue, float maxValue)
		{
			if ( maxValue <= minValue )
			{
				return minValue;
			}
			std::uniform_real_distribution<float> distribution(minValue, maxValue);
			return distribution(rng);
		}

		// 整数格子点 → -1〜1 のハッシュ
		float Hash(int x, int y, int z)
		{
			uint32_t h = static_cast< uint32_t >( x ) * 374761393u
				+ static_cast< uint32_t >( y ) * 668265263u
				+ static_cast< uint32_t >( z ) * 2147483647u;
			h = ( h ^ ( h >> 13 ) ) * 1274126177u;
			h ^= ( h >> 16 );
			return static_cast< float >( h & 0xFFFFFFu ) / 8388607.5f - 1.0f;
		}

		float Smooth(float t)
		{
			return t * t * ( 3.0f - 2.0f * t );
		}
	}

	float ApplyCurve(ParticleCurve curve, float start, float end, float t)
	{
		switch ( curve )
		{
			case ParticleCurve::EaseInOut:
				return EaseInOut(start, end, t);
			case ParticleCurve::EaseIn:
				return Lerp(start, end, t * t);
			case ParticleCurve::EaseOut:
				return Lerp(start, end, 1.0f - ( 1.0f - t ) * ( 1.0f - t ));
			case ParticleCurve::Pulse:
				return Lerp(start, end, std::sin(t * kPi));
			case ParticleCurve::Linear:
			default:
				return Lerp(start, end, t);
		}
	}

	float Noise3(float x, float y, float z)
	{
		const float fx = std::floor(x);
		const float fy = std::floor(y);
		const float fz = std::floor(z);
		const int ix = static_cast< int >( fx );
		const int iy = static_cast< int >( fy );
		const int iz = static_cast< int >( fz );
		const float u = Smooth(x - fx);
		const float v = Smooth(y - fy);
		const float w = Smooth(z - fz);

		const float c000 = Hash(ix, iy, iz);
		const float c100 = Hash(ix + 1, iy, iz);
		const float c010 = Hash(ix, iy + 1, iz);
		const float c110 = Hash(ix + 1, iy + 1, iz);
		const float c001 = Hash(ix, iy, iz + 1);
		const float c101 = Hash(ix + 1, iy, iz + 1);
		const float c011 = Hash(ix, iy + 1, iz + 1);
		const float c111 = Hash(ix + 1, iy + 1, iz + 1);

		const float x00 = Lerp(c000, c100, u);
		const float x10 = Lerp(c010, c110, u);
		const float x01 = Lerp(c001, c101, u);
		const float x11 = Lerp(c011, c111, u);
		const float y0 = Lerp(x00, x10, v);
		const float y1 = Lerp(x01, x11, v);
		return Lerp(y0, y1, w);
	}

	Vector3 TurbulenceAccel(const Vector3& position, float age, float seed, float strength, float frequency, float speed)
	{
		if ( strength <= 0.0f )
		{
			return { 0.0f, 0.0f, 0.0f };
		}
		const float px = position.x * frequency + seed;
		const float py = position.y * frequency + seed * 0.7f;
		const float pz = position.z * frequency + seed * 1.3f;
		const float t = age * speed;

		// 軸ごとに別の場所を読んで、3軸が同じ値にならないようにする
		return {
			Noise3(px + t, py, pz) * strength,
			Noise3(px + 31.4f, py + t, pz + 17.2f) * strength,
			Noise3(px + 11.3f, py + 47.1f, pz + t) * strength,
		};
	}

	ShapeSample SampleEmitShape(const ParticleConfig& config, bool is2D, std::mt19937& rng)
	{
		ShapeSample sample{ { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f } };
		const Vector3& extent = config.spawnExtent;

		auto normalized = [] (const Vector3& v) -> Vector3
		{
			const float length = std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
			return length < 1e-6f ? Vector3{ 0.0f, 0.0f, 0.0f } : Vector3{ v.x / length, v.y / length, v.z / length };
		};

		switch ( config.shape )
		{
			case ParticleEmitShape::Sphere:
			case ParticleEmitShape::Hemisphere:
			{
				const bool half = ( config.shape == ParticleEmitShape::Hemisphere );
				if ( is2D )
				{
					// 円(半円)。thickness: 0 = 輪 / 1 = 円盤(面積が一様になるよう sqrt)
					const float angle = half ? Rand(rng, 0.0f, kPi) : Rand(rng, 0.0f, 2.0f * kPi);
					const float r = config.shapeRadius * Lerp(1.0f, std::sqrt(Rand(rng, 0.0f, 1.0f)), config.shapeThickness);
					sample.outward = { std::cos(angle), std::sin(angle), 0.0f };
					sample.offset = sample.outward * r;
				} else
				{
					// 球面上の一様な向き(zを一様にサンプリング)。thickness: 0 = 殻 / 1 = 体積が一様(cbrt)
					const float z = Rand(rng, -1.0f, 1.0f);
					const float phi = Rand(rng, 0.0f, 2.0f * kPi);
					const float s = std::sqrt(std::max(0.0f, 1.0f - z * z));
					Vector3 dir = { s * std::cos(phi), z, s * std::sin(phi) }; // Yが上
					if ( half )
					{
						dir.y = std::fabs(dir.y);
					}
					const float r = config.shapeRadius * Lerp(1.0f, std::cbrt(Rand(rng, 0.0f, 1.0f)), config.shapeThickness);
					sample.outward = dir;
					sample.offset = dir * r;
				}
				break;
			}

			case ParticleEmitShape::Circle:
			{
				const float angle = Rand(rng, 0.0f, 2.0f * kPi);
				const float r = config.shapeRadius * Lerp(1.0f, std::sqrt(Rand(rng, 0.0f, 1.0f)), config.shapeThickness);
				if ( is2D )
				{
					sample.outward = { std::cos(angle), std::sin(angle), 0.0f };
				} else
				{
					sample.outward = { std::cos(angle), 0.0f, std::sin(angle) }; // XZ平面
				}
				sample.offset = sample.outward * r;
				break;
			}

			case ParticleEmitShape::Line:
			{
				sample.offset = { Rand(rng, -extent.x, extent.x), 0.0f, 0.0f };
				// 外向きは決められない(長さ0)
				break;
			}

			case ParticleEmitShape::Box:
			default:
			{
				sample.offset = {
					Rand(rng, -extent.x, extent.x),
					Rand(rng, -extent.y, extent.y),
					is2D ? 0.0f : Rand(rng, -extent.z, extent.z),
				};
				sample.outward = normalized(sample.offset);
				break;
			}
		}
		return sample;
	}

	Vector4 EvaluateColor(const Vector4& start, const Vector4& mid, bool useMid, float midTime, const Vector4& end,
		ParticleCurve curve, float fadeIn, float t)
	{
		Vector4 color;
		auto blend = [&] (const Vector4& a, const Vector4& b, float u) -> Vector4
		{
			return {
				ApplyCurve(curve, a.x, b.x, u),
				ApplyCurve(curve, a.y, b.y, u),
				ApplyCurve(curve, a.z, b.z, u),
				ApplyCurve(curve, a.w, b.w, u),
			};
		};

		if ( useMid )
		{
			const float m = std::clamp(midTime, 0.01f, 0.99f);
			color = ( t < m ) ? blend(start, mid, t / m) : blend(mid, end, ( t - m ) / ( 1.0f - m ));
		} else
		{
			color = blend(start, end, t);
		}

		if ( fadeIn > 0.0f )
		{
			color.w *= std::clamp(t / fadeIn, 0.0f, 1.0f);
		}
		return color;
	}

	//----------------------------------------------------------------
	// Scheduler
	//----------------------------------------------------------------
	void Scheduler::Reset()
	{
		time_ = 0.0f;
		burstsFired_ = 0;
	}

	bool Scheduler::HasStarted(const ParticleConfig& config) const
	{
		return time_ >= config.startDelay;
	}

	int Scheduler::Advance(float deltaTime, const ParticleConfig& config)
	{
		time_ += std::max(deltaTime, 0.0f);

		if ( config.burstCount <= 0 )
		{
			return 0;
		}

		int total = 0;
		// 1フレームに何回分も遅れていても暴走しないよう、回数に上限を付ける
		for ( int guard = 0; guard < 64; ++guard )
		{
			if ( config.burstCycles > 0 && burstsFired_ >= config.burstCycles )
			{
				break;
			}
			const float nextTime = config.startDelay + static_cast< float >( burstsFired_ ) * std::max(config.burstInterval, 0.01f);
			if ( time_ < nextTime )
			{
				break;
			}
			total += config.burstCount;
			++burstsFired_;
		}
		return total;
	}
}