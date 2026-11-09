#include "RenderObject.h"

#include "DirectXCommon.h"

#ifdef USE_IMGUI
#include "externals/imgui/imgui.h"
#endif

void RenderObject::Initialize(DirectXCommon& dx, ModelNode node)
{
	modelNode = node;

	materialResource_ = dx.CreateBufferResource(sizeof(Material));
	materialResource_.Get()->Map(0, nullptr, reinterpret_cast< void** >( &materialData_ ));
	*materialData_ = {};
	materialData_->color = { 1.0f, 1.0f, 1.0f, 1.0f };
	materialData_->lightingMode = LightingMode::HalfLambert;
	materialData_->uvTransform = MakeIdentityMatrix();

	wvpResource_ = dx.CreateBufferResource(sizeof(TransformationMatrix));
	wvpResource_.Get()->Map(0, nullptr, reinterpret_cast< void** >( &wvpData_ ));
	wvpData_->WVP = MakeIdentityMatrix();
	wvpData_->World = MakeIdentityMatrix();
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

	const ModelResource& model = assets.GetModel(modelNode);
	for ( const auto& sub : model.subMeshes )
	{
		commandList->IASetVertexBuffers(0, 1, &sub.vbv);
		commandList->SetGraphicsRootDescriptorTable(2, sub.texture.srvGpu);
		commandList->DrawInstanced(sub.vertexCount, 1, 0, 0);
	}
}

void RenderObject::DrawUi(const char* label)
{
#ifdef USE_IMGUI
	ImGui::PushID(label);
	ImGui::Text("%s", label);

	int currentModel = static_cast< int >( modelNode );
	if ( ImGui::Combo("Model", &currentModel, kModelNodeItems, IM_ARRAYSIZE(kModelNodeItems)) )
	{
		modelNode = static_cast< ModelNode >( currentModel );
	}

	ImGui::DragFloat3("Scale", &transform.scale.x, 0.1f);
	ImGui::DragFloat3("Rotate", &transform.rotate.x, 0.1f);
	ImGui::DragFloat3("Translate", &transform.translate.x, 0.1f);

	ImGui::DragFloat2("UV Translate", &uvTransform.translate.x, 0.1f, -10.0f, 10.0f);
	ImGui::DragFloat2("UV Scale", &uvTransform.scale.x, 0.1f, -10.0f, 10.0f);
	ImGui::SliderAngle("UV Rotate", &uvTransform.rotate.z);

	ImGui::ColorEdit4("Color", &materialData_->color.x);

	int currentMode = static_cast< int >( materialData_->lightingMode );
	const char* items[] = { "None", "Half Lambert", "Lambert" };
	if ( ImGui::Combo("Lighting Mode", &currentMode, items, IM_ARRAYSIZE(items)) )
	{
		materialData_->lightingMode = static_cast< LightingMode >( currentMode );
	}

	ImGui::PopID();
#else
	( void ) label;
#endif
}