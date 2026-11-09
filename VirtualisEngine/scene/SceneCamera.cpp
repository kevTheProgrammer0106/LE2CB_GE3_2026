#include "SceneCamera.h"

#include "InputManager.h"

#ifdef USE_IMGUI
#include "externals/imgui/imgui.h"
#endif

#ifdef USE_IMGUI
namespace
{
	// Camera / DebugCamera 共通のTransform編集UI
	template <typename CameraType>
	void DrawCameraTransform(CameraType& cam)
	{
		Vector4 scale = cam.GetScale();
		if ( ImGui::DragFloat3("Camera Scale", &scale.x, 0.1f) )
		{
			cam.SetScale(scale);
		}
		Vector4 rotation = cam.GetRotation();
		if ( ImGui::DragFloat3("Camera Rotate", &rotation.x, 0.1f) )
		{
			cam.SetRotation(rotation);
		}
		Vector4 translation = cam.GetTranslation();
		if ( ImGui::DragFloat3("Camera Translate", &translation.x, 0.1f) )
		{
			cam.SetTranslation(translation);
		}
	}
}
#endif

void SceneCamera::Initialize()
{
	camera_.Initialize();
	debugCamera_.Initialize();
	useDebugCamera_ = false;
}

void SceneCamera::Update(InputManager& input)
{
	if ( input.TriggerKey(DIK_F1) )
	{
		useDebugCamera_ = !useDebugCamera_;
	}

	// Update()はview/projectionを更新するために呼ぶだけで、worldMatrix引数は使わない
	// (各オブジェクトのWVPは自前で組み立てるため)
	if ( useDebugCamera_ )
	{
		debugCamera_.Update(MakeIdentityMatrix(), input);
		view_ = debugCamera_.GetViewMatrix();
		projection_ = debugCamera_.GetProjectionMatrix();
	} else
	{
		camera_.Update(MakeIdentityMatrix());
		view_ = camera_.GetViewMatrix();
		projection_ = camera_.GetProjectionMatrix();
	}
	viewProjection_ = Multiply(view_, projection_);
}

void SceneCamera::DrawUi()
{
#ifdef USE_IMGUI
	if ( ImGui::Button("Use Camera") )
	{
		useDebugCamera_ = false;
	}
	if ( ImGui::Button("Use Debug Camera") )
	{
		useDebugCamera_ = true;
	}
	ImGui::Text("F1: toggle camera / WASD: move / Hold right click: look / Q: lock cursor / F: pivot mode");

	if ( !useDebugCamera_ )
	{
		ImGui::Text("Camera Settings");
		DrawCameraTransform(camera_);
	} else
	{
		ImGui::Text("Debug Camera Settings");
		DrawCameraTransform(debugCamera_);
	}
#endif
}