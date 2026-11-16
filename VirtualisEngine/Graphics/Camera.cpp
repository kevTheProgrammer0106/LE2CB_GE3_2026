#include "VirtualisEngine/Graphics/Camera.h"

#include <algorithm>
#include <cmath>

#include "VirtualisEngine/Platform/InputManager.h"

#ifdef USE_IMGUI
#include "externals/imgui/imgui.h"
#endif

void Camera::Initialize(const CameraSettings& settings)
{
	settings_ = settings;
	pose_ = settings.pose;
	debugPose_ = settings.debugPose;
	useDebugCamera_ = settings.startWithDebugCamera;
	mode_ = settings.startMode;
	pivotPoint_ = settings.pivotPoint;
	pivotDistance_ = settings.controls.pivotDistance;
	isCursorLocked_ = false;

	// 入力が無くても最初のフレームから行列が使えるようにしておく(アスペクトは16:9を仮置き)
	UpdateMatrices(16.0f, 9.0f);
}

void Camera::Reset()
{
	Initialize(settings_);
}

Vector3 Camera::CalculateForward(const CameraPose& pose) const
{
	const Matrix4x4 rotateXMatrix = MakeRotateXMatrix(pose.rotation.x);
	const Matrix4x4 rotateYMatrix = MakeRotateYMatrix(pose.rotation.y);
	const Matrix4x4 rotateZMatrix = MakeRotateZMatrix(pose.rotation.z);
	const Matrix4x4 rotateMatrix = Multiply(rotateXMatrix, Multiply(rotateYMatrix, rotateZMatrix));
	return { rotateMatrix.m[2][0], rotateMatrix.m[2][1], rotateMatrix.m[2][2] };
}

/// <summary>
/// 自由移動モード: 右ドラッグで視点回転、WASDでローカル軸移動
/// </summary>
void Camera::UpdateFreeRoam(InputManager& input, float deltaTime)
{
	const CameraControls& c = settings_.controls;
	CameraPose& pose = debugPose_;

	if ( input.PushMouseButton(1) )
	{
		const Vector2 mouseDelta = input.GetMouseDelta();
		pose.rotation.y += mouseDelta.x * c.rotSensitivity;
		pose.rotation.x += mouseDelta.y * c.rotSensitivity;
		pose.rotation.x = std::clamp(pose.rotation.x, -c.pitchLimit, c.pitchLimit);
	}

	const Matrix4x4 rotateXMatrix = MakeRotateXMatrix(pose.rotation.x);
	const Matrix4x4 rotateYMatrix = MakeRotateYMatrix(pose.rotation.y);
	const Matrix4x4 rotateZMatrix = MakeRotateZMatrix(pose.rotation.z);
	const Matrix4x4 rotateMatrix = Multiply(rotateXMatrix, Multiply(rotateYMatrix, rotateZMatrix));

	const Vector3 forward = { rotateMatrix.m[2][0], rotateMatrix.m[2][1], rotateMatrix.m[2][2] };
	const Vector3 right = { rotateMatrix.m[0][0], rotateMatrix.m[0][1], rotateMatrix.m[0][2] };

	Vector3 moveDir = {};
	if ( input.PushKey(DIK_W) ) moveDir += forward;
	if ( input.PushKey(DIK_S) ) moveDir -= forward;
	if ( input.PushKey(DIK_D) ) moveDir += right;
	if ( input.PushKey(DIK_A) ) moveDir -= right;

	if ( Dot(moveDir, moveDir) > 0.0f )
	{
		const Vector3 normalized = Normalize(moveDir);
		pose.translation += normalized * ( c.moveSpeed * deltaTime );
	}

	if ( input.TriggerKey(DIK_Q) )
	{
		isCursorLocked_ = !isCursorLocked_;
	}
	if ( isCursorLocked_ )
	{
		// このカメラを操作しているウィンドウのクライアント中央へ(固定の画面座標は使わない)
		input.CenterCursor();
	}
}

/// <summary>
/// Pivot(周回)モード: 注視点を中心に、rotationを軌道角として周回する。
/// 位置は毎フレーム pivot - forward * distance で再計算するため、常に注視点を向く
/// </summary>
void Camera::UpdatePivot(InputManager& input)
{
	const CameraControls& c = settings_.controls;
	CameraPose& pose = debugPose_;

	// 左ボタンドラッグ中だけ軌道回転
	if ( orbitDragEnabled_ && input.PushMouseButton(0) )
	{
		const Vector2 mouseDelta = input.GetMouseDelta();
		pose.rotation.y += mouseDelta.x * c.rotSensitivity;
		pose.rotation.x += mouseDelta.y * c.rotSensitivity;
		pose.rotation.x = std::clamp(pose.rotation.x, -c.pitchLimit, c.pitchLimit);
	}

	// ホイールでズーム。距離に比例して変わる(近いほど細かく、遠いほど大きく動く)。奥へ回す(負値)ほど離れる
	pivotDistance_ *= std::exp(-input.GetWheelDelta() * c.zoomSensitivity);
	pivotDistance_ = std::clamp(pivotDistance_, c.minPivotDistance, c.maxPivotDistance);

	// 注視点から forward の逆方向に distance だけ離れた位置がカメラ位置になる
	const Vector3 forward = CalculateForward(pose);
	pose.translation = pivotPoint_ - forward * pivotDistance_;
}

void Camera::UpdateDebugCamera(InputManager& input, float deltaTime)
{
	// Fキーでモード切り替え
	if ( settings_.controls.allowModeSwitch && input.TriggerKey(DIK_F) )
	{
		if ( mode_ == DebugCameraMode::FreeRoam )
		{
			// 現在の視線の先を注視点にしてPivotモードへ入る
			const Vector3 forward = CalculateForward(debugPose_);
			pivotDistance_ = settings_.controls.pivotDistance;
			pivotPoint_ = debugPose_.translation + forward * pivotDistance_;
			mode_ = DebugCameraMode::Pivot;
		} else
		{
			// FreeRoamに戻る。rotation / translation はPivot時点の値をそのまま引き継ぐ
			mode_ = DebugCameraMode::FreeRoam;
		}
	}

	if ( mode_ == DebugCameraMode::FreeRoam )
	{
		UpdateFreeRoam(input, deltaTime);
	} else
	{
		UpdatePivot(input);
	}
}

void Camera::Update(InputManager& input, float clientWidth, float clientHeight, float deltaTime)
{
	if ( settings_.controls.allowToggle && input.TriggerKey(DIK_F1) )
	{
		useDebugCamera_ = !useDebugCamera_;
	}

	if ( useDebugCamera_ )
	{
		UpdateDebugCamera(input, deltaTime);
	}

	UpdateMatrices(clientWidth, clientHeight);
}

void Camera::UpdateMatrices(float clientWidth, float clientHeight)
{
	const CameraPose& pose = GetActivePose();
	const CameraProjection& p = settings_.projection;

	view_ = Inverse(MakeAffineMatrix(pose.scale, pose.rotation, pose.translation));

	// アスペクト: 固定値が指定されていればそれ、無ければクライアント領域の比(高さ0のときは16:9)
	float aspect = p.aspect;
	if ( aspect <= 0.0f )
	{
		aspect = ( clientHeight > 0.0f ) ? clientWidth / clientHeight : 16.0f / 9.0f;
	}

	if ( p.orthographic )
	{
		const float halfHeight = p.orthoHeight * 0.5f;
		const float halfWidth = halfHeight * aspect;
		projection_ = MakeOrthoGraphicMatrix(-halfWidth, halfHeight, halfWidth, -halfHeight, p.nearClip, p.farClip);
	} else
	{
		projection_ = MakePerspectiveFovMatrix(p.fovY, aspect, p.nearClip, p.farClip);
	}
	viewProjection_ = Multiply(view_, projection_);
}

void Camera::DrawUi()
{
#ifdef USE_IMGUI
	if ( ImGui::Button("Use Camera") )
	{
		useDebugCamera_ = false;
	}
	ImGui::SameLine();
	if ( ImGui::Button("Use Debug Camera") )
	{
		useDebugCamera_ = true;
	}
	ImGui::SameLine();
	if ( ImGui::Button("Reset") )
	{
		Reset();
	}
	ImGui::TextWrapped("F1: toggle camera / WASD: move / Hold right click: look / Q: lock cursor / F: pivot mode (left drag: orbit, wheel: zoom)");

	if ( useDebugCamera_ )
	{
		ImGui::Text("Debug Camera (%s)", mode_ == DebugCameraMode::FreeRoam ? "FreeRoam" : "Pivot");
	} else
	{
		ImGui::Text("Camera");
	}
	CameraPose& pose = GetActivePose();
	ImGui::DragFloat3("Camera Scale", &pose.scale.x, 0.1f);
	ImGui::DragFloat3("Camera Rotate", &pose.rotation.x, 0.01f);
	ImGui::DragFloat3("Camera Translate", &pose.translation.x, 0.1f);

	CameraProjection& p = settings_.projection;
	if ( ImGui::TreeNode("Projection") )
	{
		float fovDegrees = p.fovY * kRadToDeg;
		if ( ImGui::SliderFloat("FOV (deg)", &fovDegrees, 5.0f, 150.0f) )
		{
			p.fovY = fovDegrees * kDegToRad;
		}
		ImGui::DragFloat("Near", &p.nearClip, 0.01f, 0.001f, p.farClip - 0.001f);
		ImGui::DragFloat("Far", &p.farClip, 1.0f, p.nearClip + 0.001f, 100000.0f);
		ImGui::Checkbox("Orthographic", &p.orthographic);
		if ( p.orthographic )
		{
			ImGui::DragFloat("Ortho Height", &p.orthoHeight, 0.1f, 0.1f, 10000.0f);
		}
		ImGui::DragFloat("Aspect (0 = window)", &p.aspect, 0.01f, 0.0f, 10.0f);
		ImGui::TreePop();
	}

	CameraControls& c = settings_.controls;
	if ( ImGui::TreeNode("Controls") )
	{
		ImGui::DragFloat("Move Speed (/s)", &c.moveSpeed, 0.1f, 0.0f, 1000.0f);
		ImGui::DragFloat("Rotate Sensitivity", &c.rotSensitivity, 0.0005f, 0.0f, 0.1f, "%.4f");
		ImGui::DragFloat("Zoom Sensitivity", &c.zoomSensitivity, 0.0001f, 0.0f, 0.05f, "%.4f");
		ImGui::DragFloat("Pitch Limit", &c.pitchLimit, 0.01f, 0.1f, 1.57f);
		ImGui::DragFloat("Pivot Distance", &pivotDistance_, 0.1f, c.minPivotDistance, c.maxPivotDistance);
		ImGui::DragFloat3("Pivot Point", &pivotPoint_.x, 0.1f);
		ImGui::Checkbox("Allow F1 toggle", &c.allowToggle);
		ImGui::Checkbox("Allow F mode switch", &c.allowModeSwitch);
		ImGui::TreePop();
	}
#endif
}
