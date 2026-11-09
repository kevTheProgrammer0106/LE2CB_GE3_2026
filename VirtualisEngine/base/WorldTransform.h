#pragma once
#include <cstdint>  
#include<math.h>
#include <cmath>
#include <assert.h>
#include <vector>
#include <string>

struct Vector4 {
	float x, y, z, w;
};
struct Vector3 {
	float x, y, z;
	// 複合代入演算子 (Compound Assignment Operator Vector3)
	Vector3& operator*=(float s)
	{
		x *= s;
		y *= s;
		z *= s;
		return *this;
	};
	Vector3& operator-=(const Vector3& v)
	{
		x -= v.x;
		y -= v.y;
		z -= v.z;
		return *this;
	};
	Vector3& operator+=(const Vector3& v)
	{
		x += v.x;
		y += v.y;
		z += v.z;
		return *this;
	};
	Vector3& operator/=(float s)
	{
		x /= s;
		y /= s;
		z /= s;
		return *this;
	};
};
struct Vector2 {
	float x, y;
	Vector2& operator*=(float s)
	{
		x *= s;
		y *= s;
		return *this;
	};
	Vector2& operator-=(const Vector2& v)
	{
		x -= v.x;
		y -= v.y;
		return *this;
	};
	Vector2& operator+=(const Vector2& v)
	{
		x += v.x;
		y += v.y;
		return *this;
	};
	Vector2& operator/=(float s)
	{
		x /= s;
		y /= s;
		return *this;
	};
};

struct Matrix4x4 {
	float m[4][4];
};
struct Matrix3x3 {
	float m[3][3];
};

struct VertexData {
	Vector4 position;
	Vector2 texcoord;
	Vector3 normal;
};

struct MaterialData {
	std::string textureFilePath;
};
struct ModelData {
	std::vector<VertexData> vertices;
	MaterialData material;
};

enum class LightingMode : int32_t
{
	None = 0,
	HalfLambert = 1,
	Lambert = 2,
};

struct Material {
	Vector4 color;
	LightingMode lightingMode;
	float padding[3]; // パディングを追加して16バイト境界に揃える
	Matrix4x4 uvTransform;
};

struct TransformData
{
	Vector4 scale;
	Vector4 rotate;
	Vector4 translate;
};
struct TransformationMatrix
{
	Matrix4x4 WVP;
	Matrix4x4 World;
};
struct DirectionalLight
{
	Vector4 color; //!< ライトの色
	Vector3 direction; // x y z 
	float intensity; //!< 輝度
};

//  contangent関数
inline float cotf(float value)
{
	return 1.0f / tanf(value);
}

// ベクトルの長さ(ノルム)
float Length(const Vector3& v);
// ベクトルの正規化
Vector3 Normalize(const Vector3& v);

// 行列のスカラー倍
Matrix4x4 Multiply(const Matrix4x4& m1, const Matrix4x4& m2);

Vector4 Transform(const Vector4& vector, const Matrix4x4& m);

Matrix4x4 Inverse(Matrix4x4 matrix);
// 単位行列の生成
Matrix4x4 MakeIdentityMatrix();
Matrix4x4 MakeTranslateMatrix(const Vector4& translate);

Matrix4x4 MakeScaleMatrix(const Vector4& scale);
// 1. X軸回転行列
Matrix4x4 MakeRotateXMatrix(float radian);
// 2. Y軸回転行列
Matrix4x4 MakeRotateYMatrix(float radian);

// 3. Z軸回転行列
Matrix4x4 MakeRotateZMatrix(float radian);

Matrix4x4 MakeAffineMatrix(const Vector4& scale, const Vector4& rotate, const Vector4& translate);

//1. 透視投影行列
Matrix4x4 MakePerspectiveFovMatrix(float fovY, float aspectRatio, float nearClip, float farClip);
//2. 正射影行列
Matrix4x4 MakeOrthoGraphicMatrix(float left, float top, float right, float bottom, float nearClip, float farClip);
//3. ビューポート変換行列
Matrix4x4 MakeViewportMatrix(float left, float top, float width, float height, float minDepth, float maxDepth);

