#pragma once

#include "WorldTransform.h"

/// <summary>
/// ゲーム用カメラ（自由移動なし。Transformに従うだけ）
/// </summary>
class Camera
{
public:
	/// <summary>
	/// 初期化
	/// </summary>
	void Initialize();

	/// <summary>
	/// 更新
	/// </summary>
	void Update(const Matrix4x4& worldMatrix);

	//==================================================
	// Scale
	//==================================================
	Vector4 GetScale() const { return scale_; }
	void SetScale(const Vector4& scale) { scale_ = scale; }
	void SetScale(float x, float y, float z)
	{
		scale_.x = x;
		scale_.y = y;
		scale_.z = z;
	}
	//==================================================
	// Rotation
	//==================================================
	Vector4 GetRotation() const { return rotation_; }
	void SetRotation(const Vector4& rotation) { rotation_ = rotation; }
	void SetRotation(float x, float y, float z)
	{
		rotation_.x = x;
		rotation_.y = y;
		rotation_.z = z;
	}
	//==================================================
	// Translation
	//==================================================
	Vector4 GetTranslation() const { return translation_; }
	void SetTranslation(const Vector4& translation) { translation_ = translation; }
	void SetTranslation(float x, float y, float z)
	{
		translation_.x = x;
		translation_.y = y;
		translation_.z = z;
	}

	Matrix4x4 GetViewMatrix() const { return viewMatrix_; }
	Matrix4x4 GetProjectionMatrix() const { return projectionMatrix_; }
	Matrix4x4 GetWorldViewProjectionMatrix() const { return worldViewProjectionMatrix_; }
	Matrix4x4 GetViewProjectionMatrix() const {
		return Multiply(viewMatrix_, projectionMatrix_); // whatever your members are named
	}
private:
	Vector4 scale_ = { 1.0f, 1.0f, 1.0f };
	// XYZ軸回りのローカル回転角
	Vector4 rotation_ = { 0.0f, 0.0f, 0.0f };
	//ローカル座標
	Vector4 translation_ = { 0.0f, 0.0f, -10.0f };
	// ビュー行列
	Matrix4x4 viewMatrix_;
	// 射影行列
	Matrix4x4 projectionMatrix_;

	Matrix4x4 worldViewProjectionMatrix_;

	// 射影パラメータ（必要ならセッターを追加してImGui等から調整可能にする）
	float fovY_ = 0.45f;
	float nearClip_ = 0.1f;
	float farClip_ = 100.0f;

	//クライント領域のサイズ
	const int32_t kClientWidth_ = 1280;
	const int32_t kClientHeight_ = 720;
};