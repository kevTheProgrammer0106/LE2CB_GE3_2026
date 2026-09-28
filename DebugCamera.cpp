#include "DebugCamera.h"
#include "InputManager.h"

void DebugCamera::Initialize()
{
	scale_ = { 1.0f, 1.0f, 1.0f };
	rotation_ = { 0.0f, 0.0f, 0.0f };
	translation_ = { 0.0f, 0.0f, -50.0f };
	viewMatrix_ = {};
	projectionMatrix_ = {};
	worldViewProjectionMatrix_ = {};
	mode_ = DebugCameraMode::FreeRoam;
}

/// <summary>
/// rotation_からローカルのforwardベクトルを算出。両モード共通で使う
/// </summary>
Vector3 DebugCamera::CalculateForward() const
{
	Matrix4x4 rotateXMatrix = MakeRotateXMatrix(rotation_.x);
	Matrix4x4 rotateYMatrix = MakeRotateYMatrix(rotation_.y);
	Matrix4x4 rotateZMatrix = MakeRotateZMatrix(rotation_.z);
	Matrix4x4 rotateMatrix = Multiply(rotateXMatrix, Multiply(rotateYMatrix, rotateZMatrix));
	return { rotateMatrix.m[2][0], rotateMatrix.m[2][1], rotateMatrix.m[2][2] };
}

/// <summary>
/// 自由移動モード: RMBドラッグで視点回転、WASDでローカル軸移動
/// </summary>
void DebugCamera::UpdateFreeRoam(InputManager& input)
{
	if (input.PushMouseButton(1))
	{
		Vector2 mouseDelta = input.GetMouseDelta();
		rotation_.y += mouseDelta.x * rotSensitivity_;
		rotation_.x += mouseDelta.y * rotSensitivity_;

		if (rotation_.x > pitchLimit_) rotation_.x = pitchLimit_;
		if (rotation_.x < -pitchLimit_) rotation_.x = -pitchLimit_;
	}

	Matrix4x4 rotateXMatrix = MakeRotateXMatrix(rotation_.x);
	Matrix4x4 rotateYMatrix = MakeRotateYMatrix(rotation_.y);
	Matrix4x4 rotateZMatrix = MakeRotateZMatrix(rotation_.z);
	Matrix4x4 rotateMatrix = Multiply(rotateXMatrix, Multiply(rotateYMatrix, rotateZMatrix));

	Vector3 forward = { rotateMatrix.m[2][0], rotateMatrix.m[2][1], rotateMatrix.m[2][2] };
	Vector3 right = { rotateMatrix.m[0][0], rotateMatrix.m[0][1], rotateMatrix.m[0][2] };

	Vector3 moveDir = {};
	if (input.PushKey(DIK_W)) moveDir += forward;
	if (input.PushKey(DIK_S)) moveDir -= forward;
	if (input.PushKey(DIK_D)) moveDir += right;
	if (input.PushKey(DIK_A)) moveDir -= right;

	float lengthSq = moveDir.x * moveDir.x + moveDir.y * moveDir.y + moveDir.z * moveDir.z;
	if (lengthSq > 0.0f)
	{
		Vector3 normalized = Normalize(moveDir);
		translation_.x += normalized.x * moveSpeed_;
		translation_.y += normalized.y * moveSpeed_;
		translation_.z += normalized.z * moveSpeed_;
	}

	if ( input.TriggerKey(DIK_Q) )
	{
		if ( !isCursorLocked_ )
		{
			isCursorLocked_ = true;
		} else
		{
			isCursorLocked_ = false;
		}
	}
	if ( isCursorLocked_ )
	{
		//後でカーソルを固定機能修正
		SetCursorPos(640, 360);
		//SetPhysicalCursorPos(640, 360);
	}
} //11

/// <summary>
/// Pivot(周回)モード: 注視点を中心に、rotation_を軌道角として周回する。
/// 位置は毎フレーム pivot - forward * distance で再計算するため、常に注視点を向く。
/// </summary>
void DebugCamera::UpdatePivot(InputManager& input)
{
	// 左ボタンドラッグ中だけ軌道回転(Blenderの中ボタンドラッグ相当。好みでボタン番号を変更可)
	if (input.PushMouseButton(0))
	{
		Vector2 mouseDelta = input.GetMouseDelta();
		rotation_.y += mouseDelta.x * rotSensitivity_;
		rotation_.x += mouseDelta.y * rotSensitivity_;

		if (rotation_.x > pitchLimit_) rotation_.x = pitchLimit_;
		if (rotation_.x < -pitchLimit_) rotation_.x = -pitchLimit_;
	}

	// ホイールでズーム(距離調整)。手前に回す(負値)ほど離れる想定
	const float zoomSensitivity = 0.05f;
	pivotDistance_ -= input.GetWheelDelta() * zoomSensitivity;
	if (pivotDistance_ < 1.0f) pivotDistance_ = 1.0f; // 近づきすぎ防止
	if (pivotDistance_ > 500.0f) pivotDistance_ = 500.0f; // 好みで上限を調整


	Vector3 forward = CalculateForward();
	// 注視点から forward の逆方向に distance だけ離れた位置がカメラ位置になる
	translation_.x = pivotPoint_.x - forward.x * pivotDistance_;
	translation_.y = pivotPoint_.y - forward.y * pivotDistance_;
	translation_.z = pivotPoint_.z - forward.z * pivotDistance_;
}

/// <summary>
/// 更新
/// </summary>
void DebugCamera::Update(const Matrix4x4& worldMatrix, InputManager& input)
{
	// Fキーでモード切り替え(Blenderの"."インスペクトに近い操作感)
	if (input.TriggerKey(DIK_F))
	{
		if (mode_ == DebugCameraMode::FreeRoam)
		{
			// 現在の視線の少し先を注視点にしてPivotモードへ入る
			Vector3 forward = CalculateForward();
			pivotDistance_ = 50.0f; // 好みで現在距離を維持する計算に変更可
			pivotPoint_ = {
				translation_.x + forward.x * pivotDistance_,
				translation_.y + forward.y * pivotDistance_,
				translation_.z + forward.z * pivotDistance_
			};
			mode_ = DebugCameraMode::Pivot;
		}
		else
		{
			// Free-Roamに戻る。rotation_/translation_はPivot時点の値をそのまま引き継ぐ
			mode_ = DebugCameraMode::FreeRoam;
		}
	}

	if (mode_ == DebugCameraMode::FreeRoam)
	{
		UpdateFreeRoam(input);
	}
	else
	{
		UpdatePivot(input);
	}

	Matrix4x4 cameraDebugMatrix = MakeAffineMatrix(scale_, rotation_, translation_);
	viewMatrix_ = Inverse(cameraDebugMatrix);
	projectionMatrix_ = MakePerspectiveFovMatrix(0.45f, static_cast<float>(kClientWidth_) / kClientHeight_, 0.1f, 100.0f);
	worldViewProjectionMatrix_ = Multiply(worldMatrix, Multiply(viewMatrix_, projectionMatrix_));
}