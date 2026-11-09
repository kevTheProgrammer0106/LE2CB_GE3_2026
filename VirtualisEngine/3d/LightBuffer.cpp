#include "LightBuffer.h"

#include "DirectXCommon.h"

#ifdef USE_IMGUI
#include "externals/imgui/imgui.h"
#endif

void LightBuffer::Initialize(DirectXCommon& dx)
{
	resource_ = dx.CreateBufferResource(sizeof(DirectionalLight));
	resource_.Get()->Map(0, nullptr, reinterpret_cast< void** >( &data_ ));
	data_->color = { 1.0f, 1.0f, 1.0f, 1.0f };
	data_->direction = { 0.0f, -1.0f, 0.0f };
	data_->intensity = 1.0f;
}

void LightBuffer::DrawUi()
{
#ifdef USE_IMGUI
	ImGui::Text("Directional Light Settings");
	ImGui::ColorEdit4("Light Color", &data_->color.x);
	ImGui::DragFloat3("Light Direction", &data_->direction.x, 0.1f);
	ImGui::DragFloat("Light Intensity", &data_->intensity, 0.1f);
#endif
}