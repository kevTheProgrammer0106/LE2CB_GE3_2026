#pragma once

#include "Camera.h"
#include "DebugCamera.h"
#include "WorldTransform.h"

class InputManager;

/// <summary>
/// 通常カメラとデバッグカメラをまとめ、「今有効なカメラ」のview/projectionを提供する。
/// ウィンドウ(シーン)ごとに1つ持つので、ウィンドウ間でカメラは共有されない
/// </summary>
class SceneCamera
{
public:
	void Initialize();
	/// F1で通常カメラ/デバッグカメラを切り替え、有効なカメラを更新する
	void Update(InputManager& input);
	void DrawUi();

	const Matrix4x4& GetViewMatrix() const
	{
		return view_;
	}
	const Matrix4x4& GetProjectionMatrix() const
	{
		return projection_;
	}
	const Matrix4x4& GetViewProjectionMatrix() const
	{
		return viewProjection_;
	}
	bool IsDebugCameraActive() const
	{
		return useDebugCamera_;
	}

private:
	Camera camera_;
	DebugCamera debugCamera_;
	bool useDebugCamera_ = false;

	Matrix4x4 view_ = MakeIdentityMatrix();
	Matrix4x4 projection_ = MakeIdentityMatrix();
	Matrix4x4 viewProjection_ = MakeIdentityMatrix();
};