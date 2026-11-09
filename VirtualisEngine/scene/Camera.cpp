#include "Camera.h"

void Camera::Initialize()
{
	scale_ = { 1.0f, 1.0f, 1.0f };
	rotation_ = { 0.0f, 0.0f, 0.0f };
	translation_ = { 0.0f, 0.0f, -50.0f };
	viewMatrix_ = {};
	projectionMatrix_ = {};
	worldViewProjectionMatrix_ = {};
}

/// <summary>
/// 更新（入力を受け取らない。Transformに従うだけ）
/// </summary>
void Camera::Update(const Matrix4x4& worldMatrix)
{
	Matrix4x4 cameraMatrix = MakeAffineMatrix(scale_, rotation_, translation_);
	viewMatrix_ = Inverse(cameraMatrix);
	projectionMatrix_ = MakePerspectiveFovMatrix(fovY_, static_cast<float>(kClientWidth_) / kClientHeight_, nearClip_, farClip_);
	worldViewProjectionMatrix_ = Multiply(worldMatrix, Multiply(viewMatrix_, projectionMatrix_));
}