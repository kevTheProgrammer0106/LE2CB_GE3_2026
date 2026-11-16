#pragma once
#include <cstdint>  
#include<math.h>
#include <cmath>
#include <assert.h>
#include <vector>
#include <numbers>
#include <string>
#include <cstddef>


// 角度の定数(円周率・度⇔ラジアン)。どこでもこれを使う
inline constexpr float kPi = std::numbers::pi_v<float>;
inline constexpr float kDegToRad = kPi / 180.0f;
inline constexpr float kRadToDeg = 180.0f / kPi;

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

// Vector3 の二項演算子(MatrixFunc / CollisionDetection の `a - b` などで使う)
inline Vector3 operator+(const Vector3& a, const Vector3& b) { return { a.x + b.x, a.y + b.y, a.z + b.z }; }
inline Vector3 operator-(const Vector3& a, const Vector3& b) { return { a.x - b.x, a.y - b.y, a.z - b.z }; }
inline Vector3 operator-(const Vector3& v) { return { -v.x, -v.y, -v.z }; }
inline Vector3 operator*(const Vector3& v, float s) { return { v.x * s, v.y * s, v.z * s }; }
inline Vector3 operator*(float s, const Vector3& v) { return { v.x * s, v.y * s, v.z * s }; }
inline Vector3 operator/(const Vector3& v, float s) { return { v.x / s, v.y / s, v.z / s }; }

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
	float alphaCutoff; // この値以下のアルファのピクセルは捨てる(0なら完全に透明な物だけ捨てる)
	LightingMode lightingMode;
	float padding[2]; // パディングを追加して16バイト境界に揃える
	Matrix4x4 uvTransform;
};

static_assert( sizeof(Vector4) == 16 );
static_assert( sizeof(Matrix4x4) == 64 );
static_assert( sizeof(Material) == 96 );

static_assert( offsetof(Material, color) == 0 );
static_assert( offsetof(Material, alphaCutoff) == 16 );
static_assert( offsetof(Material, lightingMode) == 20 );
static_assert( offsetof(Material, padding) == 24 );
static_assert( offsetof(Material, uvTransform) == 32 );

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

//===================================================================
// 当たり判定で使う図形(CollisionDetection 用)
//===================================================================
// 球
struct Sphere {
	Vector3 center;
	float radius;
};
// 平面: dot(normal, p) = distance を満たす点pの集合。normalは正規化しておくこと
struct Plane {
	Vector3 normal;
	float distance;
};
// 線分: origin から origin + diff まで(t = 0〜1)
struct Segment {
	Vector3 origin;
	Vector3 diff;
};
// 三角形
struct Triangle {
	Vector3 vertices[3];
};
// 軸に平行な箱(ワールド座標)。DebugDraw::Box() など、AABB を受け取る関数に使う
struct AABB {
	Vector3 min{ 0.0f, 0.0f, 0.0f };
	Vector3 max{ 0.0f, 0.0f, 0.0f };
};

//  contangent関数
inline float cotf(float value)
{
	return 1.0f / tanf(value);
}

//===================================================================
// Vector3 の計算
//===================================================================
// ベクトルの加算
Vector3 Add(const Vector3& v1, const Vector3& v2);
// ベクトルの減算
Vector3 Subtract(const Vector3& v1, const Vector3& v2);
// 成分ごとの積
Vector3 Multiply(const Vector3& v1, const Vector3& v2);
// スカラー倍
Vector3 Multiply(const float& v1, const Vector3& v2);
// ベクトルの内積
float Dot(const Vector3& v1, const Vector3& v2);
// ベクトルの外積
Vector3 Cross(const Vector3& v1, const Vector3& v2);
// ベクトルの長さ(ノルム)
float Length(const Vector3& v);
// ベクトルの正規化
Vector3 Normalize(const Vector3& v);
// vectorに垂直なベクトルを1つ返す(長さは保証しない)
Vector3 Perpendicular(const Vector3& vector);
// 線形補間 (t = 0 で v1、t = 1 で v2)
Vector3 Lerp(const Vector3& v1, const Vector3& v2, float t);
// 2次ベジェ曲線上の点
Vector3 CreateBezier(const Vector3& controlPoint0, const Vector3& controlPoint1, const Vector3& controlPoint2, float t);

//===================================================================
// 行列
//===================================================================
// 行列の積(行ベクトル規約: 左から先に掛かる)
Matrix4x4 Multiply(const Matrix4x4& m1, const Matrix4x4& m2);

// 同次座標の変換(既存の挙動のまま)。入力のwは無視して1として掛け、変換後のwで x,y,z を割る。返すwは変換後のw
Vector4 Transform(const Vector4& vector, const Matrix4x4& m);
// 点の変換(w = 1 として掛けて、変換後の w で割る)。ViewProjection → Viewport の連続変換にも使う
Vector3 Transform(const Vector3& vector, const Matrix4x4& m);

Matrix4x4 Inverse(Matrix4x4 matrix);
// 単位行列の生成
Matrix4x4 MakeIdentityMatrix();
// Vector4 版(TransformData の値をそのまま渡せる。wは無視)と Vector3 版
Matrix4x4 MakeTranslateMatrix(const Vector4& translate);
Matrix4x4 MakeTranslateMatrix(const Vector3& translate);

Matrix4x4 MakeScaleMatrix(const Vector4& scale);
Matrix4x4 MakeScaleMatrix(const Vector3& scale);
// 1. X軸回転行列
Matrix4x4 MakeRotateXMatrix(float radian);
// 2. Y軸回転行列
Matrix4x4 MakeRotateYMatrix(float radian);

// 3. Z軸回転行列
Matrix4x4 MakeRotateZMatrix(float radian);

Matrix4x4 MakeAffineMatrix(const Vector4& scale, const Vector4& rotate, const Vector4& translate);
Matrix4x4 MakeAffineMatrix(const Vector3& scale, const Vector3& rotate, const Vector3& translate);

//1. 透視投影行列
Matrix4x4 MakePerspectiveFovMatrix(float fovY, float aspectRatio, float nearClip, float farClip);
//2. 正射影行列
Matrix4x4 MakeOrthoGraphicMatrix(float left, float top, float right, float bottom, float nearClip, float farClip);
//3. ビューポート変換行列
Matrix4x4 MakeViewportMatrix(float left, float top, float width, float height, float minDepth, float maxDepth);

//===================================================================
// 当たり判定 (CollisionDetection.h から移動)
//===================================================================
// v1 を v2 に射影したベクトル(v2が0ベクトルなら0を返す)
Vector3 Project(const Vector3& v1, const Vector3& v2);
// 点から線分上の最も近い点
Vector3 ClosestPoint(const Vector3& point, const Segment& segment);
// 球 と 球
bool IsCollisionSphere(const Sphere& s1, const Sphere& s2);
// 球 と 平面
bool IsCollisionPlane(const Sphere& s1, const Plane& plane);
// 線分 と 平面
bool IsCollisionSegment(const Segment& segment, const Plane& plane);
// 三角形 と 線分
bool IsCollisionTriangle(const Triangle& triangle, const Segment& segment);
// AABB と AABB
bool IsCollisionAABB(const AABB& a, const AABB& b);
// AABB と 球
bool IsCollisionBoxSphere(const AABB& aabb, const Sphere& sphere);
// AABB と 線分(軸に平行な線分でも安全)
bool IsCollisionBoxLine(const AABB& aabb, const Segment& segment);
