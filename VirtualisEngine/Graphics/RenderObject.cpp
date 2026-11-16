#include "VirtualisEngine/Graphics/RenderObject.h"
#include "VirtualisEngine/Graphics/GpuBuffers.h"

#include "VirtualisEngine/Graphics/DirectXCommon.h"

#ifdef USE_IMGUI
#include "externals/imgui/imgui.h"
#endif

void RenderObject::Initialize(DirectXCommon& dx, const std::string& name)
{
	modelName = name;

	materialData_ = CreateMaterialBuffer(dx, materialResource_, LightingMode::HalfLambert);

	wvpData_ = CreateTransformBuffer(dx, wvpResource_);
}

void RenderObject::Update(const Matrix4x4& viewProjection)
{
	Matrix4x4 world = MakeAffineMatrix(transform.scale, transform.rotate, transform.translate);
	wvpData_->WVP = Multiply(world, viewProjection);
	wvpData_->World = world;

	Matrix4x4 uv = MakeScaleMatrix(uvTransform.scale);
	uv = Multiply(uv, MakeRotateZMatrix(uvTransform.rotate.z));
	uv = Multiply(uv, MakeTranslateMatrix(uvTransform.translate));
	materialData_->uvTransform = uv;
}

void RenderObject::Draw(ID3D12GraphicsCommandList* commandList, const AssetCache& assets, D3D12_GPU_VIRTUAL_ADDRESS lightAddress) const
{
	commandList->SetGraphicsRootConstantBufferView(0, materialResource_.Get()->GetGPUVirtualAddress());
	commandList->SetGraphicsRootConstantBufferView(1, wvpResource_.Get()->GetGPUVirtualAddress());
	commandList->SetGraphicsRootConstantBufferView(3, lightAddress);

	const ModelResource& model = assets.GetModel(modelName);
	for ( const auto& sub : model.subMeshes )
	{
		commandList->IASetVertexBuffers(0, 1, &sub.vbv);
		commandList->SetGraphicsRootDescriptorTable(2, sub.texture.srvGpu);
		commandList->DrawInstanced(sub.vertexCount, 1, 0, 0);
	}
}

void RenderObject::DrawUi(const char* label, const AssetCache& assets)
{
#ifdef USE_IMGUI
	ImGui::PushID(label);
	ImGui::Text("%s", label);

	if ( ImGui::BeginCombo("Model", modelName.c_str()) )
	{
		for ( const std::string& name : assets.GetModelNames() )
		{
			const bool isSelected = ( name == modelName );
			if ( ImGui::Selectable(name.c_str(), isSelected) )
			{
				modelName = name;
			}
		}
		ImGui::EndCombo();
	}

	ImGui::DragFloat3("Scale", &transform.scale.x, 0.1f);
	ImGui::DragFloat3("Rotate", &transform.rotate.x, 0.1f);
	ImGui::DragFloat3("Translate", &transform.translate.x, 0.1f);

	ImGui::DragFloat2("UV Translate", &uvTransform.translate.x, 0.1f, -10.0f, 10.0f);
	ImGui::DragFloat2("UV Scale", &uvTransform.scale.x, 0.1f, -10.0f, 10.0f);
	ImGui::SliderAngle("UV Rotate", &uvTransform.rotate.z);

	ImGui::ColorEdit4("Color", &materialData_->color.x);
	ImGui::SliderFloat("Alpha Cutoff", &materialData_->alphaCutoff, 0.0f, 1.0f);

	int currentMode = static_cast< int >( materialData_->lightingMode );
	const char* items[] = { "None", "Half Lambert", "Lambert" };
	if ( ImGui::Combo("Lighting Mode", &currentMode, items, IM_ARRAYSIZE(items)) )
	{
		materialData_->lightingMode = static_cast< LightingMode >( currentMode );
	}

	ImGui::PopID();
#else
	( void ) label;
	( void ) assets;
#endif
}
