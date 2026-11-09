#pragma once

#include "WorldTransform.h"

class InputManager;

/// <summary>
/// デバッグカメラの操作モード
/// </summary>
enum class DebugCameraMode
{
	FreeRoam, // 自由移動(WASD + マウスドラッグで視点回転)
	Pivot,    // 注視点を中心に周回(Blenderのテンキー"."/F相当)
};

/// <summary>
/// デバッグカメラ
/// </summary>
class DebugCamera
{
public:
	/// <summary>
	/// 初期化
	/// </summary>
	void Initialize();

	/// <summary>
	/// 更新
	/// </summary>
	void Update(const Matrix4x4& worldMatrix, InputManager& input);

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

	Matrix4x4 GetViewMatrix() { return viewMatrix_; }
	Matrix4x4 GetProjectionMatrix() { return projectionMatrix_; }
	Matrix4x4 GetWorldViewProjectionMatrix() { return worldViewProjectionMatrix_; }
	Matrix4x4 GetViewProjectionMatrix() const {
		return Multiply(viewMatrix_, projectionMatrix_); // whatever your members are named
	}
private:
	void UpdateFreeRoam(InputManager& input);
	void UpdatePivot(InputManager& input);
	Vector3 CalculateForward() const; // rotation_からforwardベクトルを算出(共通処理)

	Vector4 scale_ = { 1.0f, 1.0f, 1.0f };
	// XYZ軸回りのローカル回転角
	Vector4 rotation_ = { 0.0f, 0.0f, 0.0f };
	//ローカル座標
	Vector4 translation_ = { 0.0f, 0.0f, -50.0f };
	// ビュー行列
	Matrix4x4 viewMatrix_;
	// 射影行列
	Matrix4x4 projectionMatrix_;
	Matrix4x4 worldViewProjectionMatrix_; 

	//==================================================
// モード管理
//==================================================
	DebugCameraMode mode_ = DebugCameraMode::FreeRoam;

	//==================================================
	// Pivot(周回)モード用
	//==================================================
	Vector3 pivotPoint_ = { 0.0f, 0.0f, 0.0f }; // 注視点(周回の中心)
	float pivotDistance_ = 50.0f;               // 注視点からの距離(ズーム)

	const float moveSpeed_ = 0.5f;
	const float rotSensitivity_ = 0.005f;
	const float pitchLimit_ = 1.5f; // 約85度

	//クライント領域のサイズ
	const int32_t kClientWidth_ = 1280;
	const int32_t kClientHeight_ = 720;

	bool isCursorLocked_ = false; // カーソルをウィンドウ内に閉じ込めるかどうか
};



