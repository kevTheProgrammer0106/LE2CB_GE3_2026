#include "VirtualisEngine/Math/WorldTransform.h"

#include <algorithm>
#include <cmath>

//===================================================================
// Vector3 の計算
//===================================================================
// ベクトルの加算
Vector3 Add(const Vector3& v1, const Vector3& v2) {
	Vector3 result;
	result.x = v1.x + v2.x;
	result.y = v1.y + v2.y;
	result.z = v1.z + v2.z;
	return result;
}
// ベクトルの減算
Vector3 Subtract(const Vector3& v1, const Vector3& v2) {
	Vector3 result;
	result.x = v1.x - v2.x;
	result.y = v1.y - v2.y;
	result.z = v1.z - v2.z;
	return result;
}

Vector3 Multiply(const Vector3& v1, const Vector3& v2) {
	Vector3 result;
	result.x = v1.x * v2.x;
	result.y = v1.y * v2.y;
	result.z = v1.z * v2.z;
	return result;
}
Vector3 Multiply(const float& v1, const Vector3& v2) {
	Vector3 result;
	result.x = v1 * v2.x;
	result.y = v1 * v2.y;
	result.z = v1 * v2.z;
	return result;
}

// ベクトルの内積
float Dot(const Vector3& v1, const Vector3& v2) {
	return v1.x * v2.x + v1.y * v2.y + v1.z * v2.z;
}

// ベクトルの外積
Vector3 Cross(const Vector3& v1, const Vector3& v2)
{
	Vector3 result;
	return result = {
		v1.y * v2.z - v1.z * v2.y,
		v1.z * v2.x - v1.x * v2.z,
		v1.x * v2.y - v1.y * v2.x
	};
}

// ベクトルの長さ(ノルム)
float Length(const Vector3& v) {
	return sqrtf(v.x * v.x + v.y * v.y + v.z * v.z);
}
// ベクトルの正規化
Vector3 Normalize(const Vector3& v) {
	float length = Length(v);

	if (length == 0.0f) {
		return { 0.0f,0.0f,0.0f };
	}

	return {
		v.x / length,
		v.y / length,
		v.z / length
	};
}

Vector3 Perpendicular(const Vector3& vector)
{
	if (vector.x != 0.0f || vector.y != 0.0f)
	{
		return { -vector.y, vector.x, 0.0f };
	}
	return { 0.0f, -vector.z, vector.y };
}

Vector3 Lerp(const Vector3& v1, const Vector3& v2, float t)
{
	Vector3 result;
	result.x = v1.x + (v2.x - v1.x) * t;
	result.y = v1.y + (v2.y - v1.y) * t;
	result.z = v1.z + (v2.z - v1.z) * t;
	return result;
}

Vector3 CreateBezier(const Vector3& controlPoint0, const Vector3& controlPoint1, const Vector3& controlPoint2, float t)
{
	//制御点p0,p1を線形補間
	Vector3 p0p1 = Lerp(controlPoint0, controlPoint1, t);
	//制御点p1,p2を線形補間
	Vector3 p1p2 = Lerp(controlPoint1, controlPoint2, t);
	//制御点p0p1,p1p2をさらに線形補間
	return Lerp(p0p1, p1p2, t);
}

//===================================================================
// 行列
//===================================================================
// 行列の積
Matrix4x4 Multiply(const Matrix4x4& m1, const Matrix4x4& m2) {
	Matrix4x4 result;

	for (int i = 0; i < 4; i++)
	{
		for (int j = 0; j < 4; j++)
		{
			result.m[i][j] = 0.0f;
			for (int k = 0; k < 4; k++)
			{
				result.m[i][j] += m1.m[i][k] * m2.m[k][j];
			}
		}
	}

	return result;
}

Vector4 Transform(const Vector4& vector, const Matrix4x4& m) {
	Vector4 result;

	result.x = vector.x * m.m[0][0] + vector.y * m.m[1][0] + vector.z * m.m[2][0] + m.m[3][0];
	result.y = vector.x * m.m[0][1] + vector.y * m.m[1][1] + vector.z * m.m[2][1] + m.m[3][1];
	result.z = vector.x * m.m[0][2] + vector.y * m.m[1][2] + vector.z * m.m[2][2] + m.m[3][2];
	result.w = vector.x * m.m[0][3] + vector.y * m.m[1][3] + vector.z * m.m[2][3] + m.m[3][3];

	assert(result.w != 0.0f);
	result.x /= result.w;
	result.y /= result.w;
	result.z /= result.w;
	return result;
}

Vector3 Transform(const Vector3& vector, const Matrix4x4& m) {
	Vector3 result;
	float w;
	result.x = vector.x * m.m[0][0] + vector.y * m.m[1][0] + vector.z * m.m[2][0] + m.m[3][0];
	result.y = vector.x * m.m[0][1] + vector.y * m.m[1][1] + vector.z * m.m[2][1] + m.m[3][1];
	result.z = vector.x * m.m[0][2] + vector.y * m.m[1][2] + vector.z * m.m[2][2] + m.m[3][2];
	w = vector.x * m.m[0][3] + vector.y * m.m[1][3] + vector.z * m.m[2][3] + m.m[3][3];

	assert(w != 0.0f);
	result.x /= w;
	result.y /= w;
	result.z /= w;
	return result;
}

Matrix4x4 Inverse(Matrix4x4 matrix)
{
	float determinant =
		matrix.m[0][0] * (
			matrix.m[1][1] * (matrix.m[2][2] * matrix.m[3][3] - matrix.m[2][3] * matrix.m[3][2]) -
			matrix.m[1][2] * (matrix.m[2][1] * matrix.m[3][3] - matrix.m[2][3] * matrix.m[3][1]) +
			matrix.m[1][3] * (matrix.m[2][1] * matrix.m[3][2] - matrix.m[2][2] * matrix.m[3][1])) -
		matrix.m[0][1] * (
			matrix.m[1][0] * (matrix.m[2][2] * matrix.m[3][3] - matrix.m[2][3] * matrix.m[3][2]) -
			matrix.m[1][2] * (matrix.m[2][0] * matrix.m[3][3] - matrix.m[2][3] * matrix.m[3][0]) +
			matrix.m[1][3] * (matrix.m[2][0] * matrix.m[3][2] - matrix.m[2][2] * matrix.m[3][0])) +
		matrix.m[0][2] * (
			matrix.m[1][0] * (matrix.m[2][1] * matrix.m[3][3] - matrix.m[2][3] * matrix.m[3][1]) -
			matrix.m[1][1] * (matrix.m[2][0] * matrix.m[3][3] - matrix.m[2][3] * matrix.m[3][0]) +
			matrix.m[1][3] * (matrix.m[2][0] * matrix.m[3][1] - matrix.m[2][1] * matrix.m[3][0])) -
		matrix.m[0][3] * (
			matrix.m[1][0] * (matrix.m[2][1] * matrix.m[3][2] - matrix.m[2][2] * matrix.m[3][1]) -
			matrix.m[1][1] * (matrix.m[2][0] * matrix.m[3][2] - matrix.m[2][2] * matrix.m[3][0]) +
			matrix.m[1][2] * (matrix.m[2][0] * matrix.m[3][1] - matrix.m[2][1] * matrix.m[3][0]));

	assert(determinant != 0.0f);
	float invDet = 1.0f / determinant;
	return matrix =
	{
		 (matrix.m[1][1] * (matrix.m[2][2] * matrix.m[3][3] - matrix.m[2][3] * matrix.m[3][2]) - matrix.m[1][2] * (matrix.m[2][1] * matrix.m[3][3] - matrix.m[2][3] * matrix.m[3][1]) + matrix.m[1][3] * (matrix.m[2][1] * matrix.m[3][2] - matrix.m[2][2] * matrix.m[3][1])) * invDet,
		-(matrix.m[0][1] * (matrix.m[2][2] * matrix.m[3][3] - matrix.m[2][3] * matrix.m[3][2]) - matrix.m[0][2] * (matrix.m[2][1] * matrix.m[3][3] - matrix.m[2][3] * matrix.m[3][1]) + matrix.m[0][3] * (matrix.m[2][1] * matrix.m[3][2] - matrix.m[2][2] * matrix.m[3][1])) * invDet,
		 (matrix.m[0][1] * (matrix.m[1][2] * matrix.m[3][3] - matrix.m[1][3] * matrix.m[3][2]) - matrix.m[0][2] * (matrix.m[1][1] * matrix.m[3][3] - matrix.m[1][3] * matrix.m[3][1]) + matrix.m[0][3] * (matrix.m[1][1] * matrix.m[3][2] - matrix.m[1][2] * matrix.m[3][1])) * invDet,
		-(matrix.m[0][1] * (matrix.m[1][2] * matrix.m[2][3] - matrix.m[1][3] * matrix.m[2][2]) - matrix.m[0][2] * (matrix.m[1][1] * matrix.m[2][3] - matrix.m[1][3] * matrix.m[2][1]) + matrix.m[0][3] * (matrix.m[1][1] * matrix.m[2][2] - matrix.m[1][2] * matrix.m[2][1])) * invDet,

		-(matrix.m[1][0] * (matrix.m[2][2] * matrix.m[3][3] - matrix.m[2][3] * matrix.m[3][2]) - matrix.m[1][2] * (matrix.m[2][0] * matrix.m[3][3] - matrix.m[2][3] * matrix.m[3][0]) + matrix.m[1][3] * (matrix.m[2][0] * matrix.m[3][2] - matrix.m[2][2] * matrix.m[3][0])) * invDet,
		 (matrix.m[0][0] * (matrix.m[2][2] * matrix.m[3][3] - matrix.m[2][3] * matrix.m[3][2]) - matrix.m[0][2] * (matrix.m[2][0] * matrix.m[3][3] - matrix.m[2][3] * matrix.m[3][0]) + matrix.m[0][3] * (matrix.m[2][0] * matrix.m[3][2] - matrix.m[2][2] * matrix.m[3][0])) * invDet,
		-(matrix.m[0][0] * (matrix.m[1][2] * matrix.m[3][3] - matrix.m[1][3] * matrix.m[3][2]) - matrix.m[0][2] * (matrix.m[1][0] * matrix.m[3][3] - matrix.m[1][3] * matrix.m[3][0]) + matrix.m[0][3] * (matrix.m[1][0] * matrix.m[3][2] - matrix.m[1][2] * matrix.m[3][0])) * invDet,
		 (matrix.m[0][0] * (matrix.m[1][2] * matrix.m[2][3] - matrix.m[1][3] * matrix.m[2][2]) - matrix.m[0][2] * (matrix.m[1][0] * matrix.m[2][3] - matrix.m[1][3] * matrix.m[2][0]) + matrix.m[0][3] * (matrix.m[1][0] * matrix.m[2][2] - matrix.m[1][2] * matrix.m[2][0])) * invDet,

		 (matrix.m[1][0] * (matrix.m[2][1] * matrix.m[3][3] - matrix.m[2][3] * matrix.m[3][1]) - matrix.m[1][1] * (matrix.m[2][0] * matrix.m[3][3] - matrix.m[2][3] * matrix.m[3][0]) + matrix.m[1][3] * (matrix.m[2][0] * matrix.m[3][1] - matrix.m[2][1] * matrix.m[3][0])) * invDet,
		-(matrix.m[0][0] * (matrix.m[2][1] * matrix.m[3][3] - matrix.m[2][3] * matrix.m[3][1]) - matrix.m[0][1] * (matrix.m[2][0] * matrix.m[3][3] - matrix.m[2][3] * matrix.m[3][0]) + matrix.m[0][3] * (matrix.m[2][0] * matrix.m[3][1] - matrix.m[2][1] * matrix.m[3][0])) * invDet,
		 (matrix.m[0][0] * (matrix.m[1][1] * matrix.m[3][3] - matrix.m[1][3] * matrix.m[3][1]) - matrix.m[0][1] * (matrix.m[1][0] * matrix.m[3][3] - matrix.m[1][3] * matrix.m[3][0]) + matrix.m[0][3] * (matrix.m[1][0] * matrix.m[3][1] - matrix.m[1][1] * matrix.m[3][0])) * invDet,
		-(matrix.m[0][0] * (matrix.m[1][1] * matrix.m[2][3] - matrix.m[1][3] * matrix.m[2][1]) - matrix.m[0][1] * (matrix.m[1][0] * matrix.m[2][3] - matrix.m[1][3] * matrix.m[2][0]) + matrix.m[0][3] * (matrix.m[1][0] * matrix.m[2][1] - matrix.m[1][1] * matrix.m[2][0])) * invDet,

		-(matrix.m[1][0] * (matrix.m[2][1] * matrix.m[3][2] - matrix.m[2][2] * matrix.m[3][1]) - matrix.m[1][1] * (matrix.m[2][0] * matrix.m[3][2] - matrix.m[2][2] * matrix.m[3][0]) + matrix.m[1][2] * (matrix.m[2][0] * matrix.m[3][1] - matrix.m[2][1] * matrix.m[3][0])) * invDet,
		 (matrix.m[0][0] * (matrix.m[2][1] * matrix.m[3][2] - matrix.m[2][2] * matrix.m[3][1]) - matrix.m[0][1] * (matrix.m[2][0] * matrix.m[3][2] - matrix.m[2][2] * matrix.m[3][0]) + matrix.m[0][2] * (matrix.m[2][0] * matrix.m[3][1] - matrix.m[2][1] * matrix.m[3][0])) * invDet,
		-(matrix.m[0][0] * (matrix.m[1][1] * matrix.m[3][2] - matrix.m[1][2] * matrix.m[3][1]) - matrix.m[0][1] * (matrix.m[1][0] * matrix.m[3][2] - matrix.m[1][2] * matrix.m[3][0]) + matrix.m[0][2] * (matrix.m[1][0] * matrix.m[3][1] - matrix.m[1][1] * matrix.m[3][0])) * invDet,
		 (matrix.m[0][0] * (matrix.m[1][1] * matrix.m[2][2] - matrix.m[1][2] * matrix.m[2][1]) - matrix.m[0][1] * (matrix.m[1][0] * matrix.m[2][2] - matrix.m[1][2] * matrix.m[2][0]) + matrix.m[0][2] * (matrix.m[1][0] * matrix.m[2][1] - matrix.m[1][1] * matrix.m[2][0])) * invDet,
	};
}

// 単位行列の生成
Matrix4x4 MakeIdentityMatrix() {
	Matrix4x4 result = {};
	for (int i = 0; i < 4; ++i) {
		result.m[i][i] = 1.0f; // 単位行列
	}
	return result;
}
Matrix4x4 MakeTranslateMatrix(const Vector4& translate) {
	Matrix4x4 result = MakeIdentityMatrix();
	result.m[3][0] = translate.x;
	result.m[3][1] = translate.y;
	result.m[3][2] = translate.z;
	return result;
}
Matrix4x4 MakeTranslateMatrix(const Vector3& translate) {
	Matrix4x4 result = MakeIdentityMatrix();
	result.m[3][0] = translate.x;
	result.m[3][1] = translate.y;
	result.m[3][2] = translate.z;
	return result;
}

Matrix4x4 MakeScaleMatrix(const Vector4& scale) {
	Matrix4x4 result;
	result = {
		scale.x, 0.0f, 0.0f, 0.0f,
		0.0f, scale.y, 0.0f, 0.0f,
		0.0f, 0.0f, scale.z, 0.0f,
		0.0f, 0.0f, 0.0f, 1.0f
	};
	return result;
}
Matrix4x4 MakeScaleMatrix(const Vector3& scale) {
	Matrix4x4 result;
	result = {
		scale.x, 0.0f, 0.0f, 0.0f,
		0.0f, scale.y, 0.0f, 0.0f,
		0.0f, 0.0f, scale.z, 0.0f,
		0.0f, 0.0f, 0.0f, 1.0f
	};
	return result;
}
// 1. X軸回転行列
Matrix4x4 MakeRotateXMatrix(float radian) {
	Matrix4x4 result = {
		1.0f, 0.0f, 0.0f, 0.0f,
		0.0f, cosf(radian), sinf(radian), 0.0f,
		0.0f, -sinf(radian), cosf(radian), 0.0f,
		0.0f, 0.0f, 0.0f, 1.0f
	};
	return result;
}
// 2. Y軸回転行列
Matrix4x4 MakeRotateYMatrix(float radian) {
	Matrix4x4 result = {
		cosf(radian), 0.0f, -sinf(radian), 0.0f,
		0.0f, 1.0f, 0.0f, 0.0f,
		sinf(radian), 0.0f, cosf(radian), 0.0f,
		0.0f, 0.0f, 0.0f, 1.0f
	};
	return result;
}
// 3. Z軸回転行列
Matrix4x4 MakeRotateZMatrix(float radian) {
	Matrix4x4 result = {
		cosf(radian), sinf(radian), 0.0f, 0.0f,
		-sinf(radian), cosf(radian), 0.0f, 0.0f,
		0.0f, 0.0f, 1.0f, 0.0f,
		0.0f, 0.0f, 0.0f, 1.0f
	};
	return result;
}

Matrix4x4 MakeAffineMatrix(const Vector4& scale, const Vector4& rotate, const Vector4& translate) {
	Matrix4x4 scaleMatrix = MakeScaleMatrix(scale);
	Matrix4x4 rotateXMatrix = MakeRotateXMatrix(rotate.x);
	Matrix4x4 rotateYMatrix = MakeRotateYMatrix(rotate.y);
	Matrix4x4 rotateZMatrix = MakeRotateZMatrix(rotate.z);
	Matrix4x4 translateMatrix = MakeTranslateMatrix(translate);

	Matrix4x4 rotateXYZMatrix = Multiply(rotateXMatrix, Multiply(rotateYMatrix, rotateZMatrix));
	Matrix4x4 worldMatrix = Multiply(scaleMatrix, Multiply(rotateXYZMatrix, translateMatrix));
	//worldMatrix = Multiply(worldMatrix, translateMatrix);

	return worldMatrix;
}
Matrix4x4 MakeAffineMatrix(const Vector3& scale, const Vector3& rotate, const Vector3& translate) {
	Matrix4x4 scaleMatrix = MakeScaleMatrix(scale);
	Matrix4x4 rotateXMatrix = MakeRotateXMatrix(rotate.x);
	Matrix4x4 rotateYMatrix = MakeRotateYMatrix(rotate.y);
	Matrix4x4 rotateZMatrix = MakeRotateZMatrix(rotate.z);
	Matrix4x4 translateMatrix = MakeTranslateMatrix(translate);

	Matrix4x4 rotateXYZMatrix = Multiply(rotateXMatrix, Multiply(rotateYMatrix, rotateZMatrix));
	Matrix4x4 worldMatrix = Multiply(scaleMatrix, Multiply(rotateXYZMatrix, translateMatrix));

	return worldMatrix;
}

//1. 透視投影行列
Matrix4x4 MakePerspectiveFovMatrix(float fovY, float aspectRatio, float nearClip, float farClip)
{
	Matrix4x4 result =
	{
	(1.0f / aspectRatio) * cotf(fovY / 2.0f), 0, 0, 0,
	0, cotf(fovY / 2), 0, 0,
	0, 0, farClip / (farClip - nearClip), 1.0f,
	0, 0, -nearClip * farClip / (farClip - nearClip), 0
	};
	return result;

};
//2. 正射影行列
Matrix4x4 MakeOrthoGraphicMatrix(float left, float top, float right, float bottom, float nearClip, float farClip)
{
	Matrix4x4 result =
	{
		2.0f / (right - left), 0, 0, 0,
		0, 2.0f / (top - bottom), 0, 0,
		0, 0, 1.0f / (farClip - nearClip), 0,
		(left + right) / (left - right), (top + bottom) / (bottom - top), nearClip / (nearClip - farClip), 1.0f
	};
	return result;
};
//3. ビューポート変換行列
Matrix4x4 MakeViewportMatrix(float left, float top, float width, float height, float minDepth, float maxDepth)
{
	Matrix4x4 result =
	{
		width / 2.0f, 0, 0, 0,
		0, -height / 2.0f, 0, 0,
		0, 0, maxDepth - minDepth, 0,
		left + (width / 2.0f), top + (height / 2.0f), minDepth, 1.0f
	};
	return result;
};

//===================================================================
// 当たり判定 (CollisionDetection.cpp から移動)
// Windows.h の min / max マクロに負けないよう、std::min / std::max は括弧で囲んで呼ぶ
//===================================================================
Vector3 Project(const Vector3& v1, const Vector3& v2)
{
	float dotProduct = Dot(v1, v2);
	float bLSquared = Dot(v2, v2);  // |b|²
	if (bLSquared == 0.0f)
	{
		return { 0.0f, 0.0f, 0.0f }; // 0ベクトルへは射影できない
	}
	float scale = dotProduct / bLSquared;
	return { scale * v2.x, scale * v2.y, scale * v2.z };
}

Vector3 ClosestPoint(const Vector3& point, const Segment& segment)
{
	Vector3 a = Subtract(point, segment.origin);
	float lengthSq = Dot(segment.diff, segment.diff);
	if (lengthSq == 0.0f)
	{
		return segment.origin; // 長さ0の線分は始点そのもの
	}
	float t = Dot(a, segment.diff) / lengthSq;
	t = std::clamp(t, 0.0f, 1.0f);  // 線分なので[0,1]に収める
	return {
		segment.origin.x + t * segment.diff.x,
		segment.origin.y + t * segment.diff.y,
		segment.origin.z + t * segment.diff.z
	};
}

bool IsCollisionSphere(const Sphere& s1, const Sphere& s2)
{
	// 2つの球の中心点問の距離を求める
	float distance = Length(s2.center - s1.center);
	// 半径の合計よりも短ければ衝突
	if (distance <= s1.radius + s2.radius)
	{
		return true;
	}
	return false;
}
bool IsCollisionPlane(const Sphere& s1, const Plane& plane)
{
	// 球の中心と平面との距離（符号付き）
	float distance = Dot(plane.normal, s1.center) - plane.distance;
	// 距離の絶対値が半径以下なら衝突
	return std::fabs(distance) <= s1.radius;
}
bool IsCollisionSegment(const Segment& segment, const Plane& plane)
{
	// まず垂直判定を行うために、法線と線の内積を求める
	float dot = Dot(plane.normal, segment.diff);

	// 垂直 = 平行であるので、衝突しているはずがない
	if (dot == 0.0f)
	{
		return false;
	}
	// tを求める
	float t = (plane.distance - Dot(segment.origin, plane.normal)) / dot;
	if (t >= 0.0f && t <= 1.0f)
	{
		return true;
	}
	return false;
}

bool IsCollisionTriangle(const Triangle& triangle, const Segment& segment)
{
	// 三角形の法線を求める（2辺の外積）
	Vector3 edge1 = Subtract(triangle.vertices[1], triangle.vertices[0]);
	Vector3 edge2 = Subtract(triangle.vertices[2], triangle.vertices[0]);
	Vector3 normal = Normalize(Cross(edge1, edge2));

	// 法線と線分の方向ベクトルの内積（垂直＝平行なら衝突しない）
	float dot = Dot(normal, segment.diff);
	if (dot == 0.0f)
	{
		return false;
	}

	// 平面とのtを求める（vertices[0]を平面上の点として使用）
	float t = Dot(Subtract(triangle.vertices[0], segment.origin), normal) / dot;

	// tが[0,1]の範囲外なら、線分は平面と交差しない
	if (t < 0.0f || t > 1.0f)
	{
		return false;
	}

	// 交点pを求める
	Vector3 p = Add(segment.origin, Multiply(t, segment.diff));

	// pが三角形の内側にあるかを、各辺との外積の向きで判定する
	Vector3 v01 = Subtract(triangle.vertices[1], triangle.vertices[0]);
	Vector3 v1p = Subtract(p, triangle.vertices[1]);
	Vector3 cross01 = Cross(v01, v1p);

	Vector3 v12 = Subtract(triangle.vertices[2], triangle.vertices[1]);
	Vector3 v2p = Subtract(p, triangle.vertices[2]);
	Vector3 cross12 = Cross(v12, v2p);

	Vector3 v20 = Subtract(triangle.vertices[0], triangle.vertices[2]);
	Vector3 v0p = Subtract(p, triangle.vertices[0]);
	Vector3 cross20 = Cross(v20, v0p);

	// 全て同じ方向を向いていれば、pは三角形の内側
	if (Dot(cross01, normal) >= 0.0f &&
		Dot(cross12, normal) >= 0.0f &&
		Dot(cross20, normal) >= 0.0f)
	{
		return true;
	}
	return false;
}

bool IsCollisionAABB(const AABB& a, const AABB& b)
{
	if ((a.min.x <= b.max.x && a.max.x >= b.min.x) && // x軸
		(a.min.y <= b.max.y && a.max.y >= b.min.y) && // y軸
		(a.min.z <= b.max.z && a.max.z >= b.min.z)) // z軸
	{
		return true;
	}
	return false;
}
bool IsCollisionBoxSphere(const AABB& aabb, const Sphere& sphere)
{
	//最近接点を求める
	Vector3 closestPoint = {
	std::clamp(sphere.center.x, aabb.min.x, aabb.max.x),
	std::clamp(sphere.center.y, aabb.min.y, aabb.max.y),
	std::clamp(sphere.center.z, aabb.min.z, aabb.max.z)
	};
	// 最近接点と球の中心との距離を求める
	float distance = Length(closestPoint - sphere.center);
	// 距離が半径よりも小さいければ衝突
	if (distance <= sphere.radius)
	{
		return true;
	}
	return false;
}

bool IsCollisionBoxLine(const AABB& aabb, const Segment& segment)
{
	// 軸ごとに「箱の2枚の面に入る t」を求め、全軸で重なる区間が[0,1]と交わるかを見る(スラブ法)
	// diffの成分が0(その軸に平行)のときは割り算できないので、始点がその軸の範囲内かどうかだけで決める
	const float origin[3] = { segment.origin.x, segment.origin.y, segment.origin.z };
	const float diff[3] = { segment.diff.x, segment.diff.y, segment.diff.z };
	const float boxMin[3] = { aabb.min.x, aabb.min.y, aabb.min.z };
	const float boxMax[3] = { aabb.max.x, aabb.max.y, aabb.max.z };

	// 区間の初期値は線分そのもの(0〜1)。ここから軸ごとに狭めていく
	float tmin = 0.0f;
	float tmax = 1.0f;

	for (int axis = 0; axis < 3; ++axis)
	{
		if (diff[axis] == 0.0f)
		{
			if (origin[axis] < boxMin[axis] || origin[axis] > boxMax[axis])
			{
				return false; // 平行で、箱のスラブの外にいる
			}
			continue;
		}
		float tNear = (boxMin[axis] - origin[axis]) / diff[axis];
		float tFar = (boxMax[axis] - origin[axis]) / diff[axis];
		if (tNear > tFar)
		{
			std::swap(tNear, tFar);
		}
		// AABBとの衝突点 (貫通点)のtが小さい方 / 大きい方
		tmin = (std::max)(tmin, tNear);
		tmax = (std::min)(tmax, tFar);
		if (tmin > tmax)
		{
			return false;
		}
	}
	return true;
}
