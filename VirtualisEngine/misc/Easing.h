#pragma once

#include <cmath>
#include <numbers>

// 線形補間
inline float Lerp(float start, float end, float t)
{
	return ( 1.0f - t ) * start + t * end;
}

// コサインで滑らかに加減速する補間(M_PIに頼らず std::numbers を使う)
inline float EaseInOut(float start, float end, float t)
{
	const float num = -( std::cos(std::numbers::pi_v<float> *t) - 1.0f ) / 2.0f;
	return Lerp(start, end, num);
}