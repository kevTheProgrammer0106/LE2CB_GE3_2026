#include "ShapeObject.h"

#include <algorithm>
#include <cassert>
#include <cstdio>

#include "DirectXCommon.h"
#include "PipelineManager.h"

#ifdef USE_IMGUI
#include "externals/imgui/imgui.h"
#endif

namespace
{
	constexpr TransformData kIdentityTransform{ { 1.0f, 1.0f, 1.0f, 0.0f }, { 0.0f, 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f, 0.0f } };
}

void ShapeObject::Initialize(DirectXCommon& dx, ShapeType type, const std::string& objectName)
{
	type_ = type;
	name = objectName;
	segments_ = ShapeFactory::DefaultSegments(type);
	geometry_ = ShapeFactory::Create(type_, segments_);
	partTransforms.assign(geometry_.parts.size(), kIdentityTransform);

	// 頂点バッファ: 分割数を最大にしたときの大きさで1回だけ確保する(描画中に作り直さない)
	const size_t capacity = ShapeFactory::MaxVertexCount(type_);
	vertexResource_ = dx.CreateBufferResource(sizeof(VertexData) * capacity);
	HRESULT hr = vertexResource_.Get()->Map(0, nullptr, reinterpret_cast< void** >( &mappedVertices_ ));
	assert(SUCCEEDED(hr) && mappedVertices_ != nullptr);
	( void ) hr;

	vbv_.BufferLocation = vertexResource_.Get()->GetGPUVirtualAddress();
	vbv_.StrideInBytes = sizeof(VertexData);
	vbv_.SizeInBytes = 0; // Update()で頂点数に合わせる

	materialResource_ = dx.CreateBufferResource(sizeof(Material));
	materialResource_.Get()->Map(0, nullptr, reinterpret_cast< void** >( &materialData_ ));
	*materialData_ = {};
	materialData_->color = { 1.0f, 1.0f, 1.0f, 1.0f };
	// 線は面が無いので陰影を付けない
	materialData_->lightingMode = geometry_.isLine ? LightingMode::None : LightingMode::HalfLambert;
	materialData_->uvTransform = MakeIdentityMatrix();

	wvpResource_ = dx.CreateBufferResource(sizeof(TransformationMatrix));
	wvpResource_.Get()->Map(0, nullptr, reinterpret_cast< void** >( &wvpData_ ));
	wvpData_->WVP = MakeIdentityMatrix();
	wvpData_->World = MakeIdentityMatrix();

	Update(MakeIdentityMatrix()); // 最初のフレームの前に頂点を入れておく
}

void ShapeObject::RebuildGeometry()
{
	geometry_ = ShapeFactory::Create(type_, segments_);
	// パーツ数は変わらないが、念のため数を合わせる(増えた分は初期値)
	partTransforms.resize(geometry_.parts.size(), kIdentityTransform);
}

void ShapeObject::Update(const Matrix4x4& viewProjection)
{
	// パーツごとの変形を掛けた頂点をバッファへ(球でも最大6144頂点程度なので毎フレーム書き直してよい)
	ShapeFactory::BuildVertices(geometry_, partTransforms, useIndividual, mappedVertices_);
	vertexCount_ = static_cast< UINT >( geometry_.vertices.size() );
	vbv_.SizeInBytes = vertexCount_ * sizeof(VertexData);

	// Transform All
	const Matrix4x4 world = MakeAffineMatrix(transform.scale, transform.rotate, transform.translate);
	wvpData_->WVP = Multiply(world, viewProjection);
	wvpData_->World = world;

	Matrix4x4 uv = MakeScaleMatrix(uvTransform.scale);
	uv = Multiply(uv, MakeRotateZMatrix(uvTransform.rotate.z));
	uv = Multiply(uv, MakeTranslateMatrix(uvTransform.translate));
	materialData_->uvTransform = uv;
}

void ShapeObject::Draw(ID3D12GraphicsCommandList* commandList, const PipelineManager& pipelines, const AssetCache& assets,
	D3D12_GPU_VIRTUAL_ADDRESS lightAddress, ParticleBlendMode blendMode) const
{
	if ( !visible || vertexCount_ == 0 )
	{
		return;
	}

	if ( geometry_.isLine )
	{
		commandList->SetPipelineState(pipelines.GetLinePso());
		commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_LINELIST);
	} else
	{
		commandList->SetPipelineState(pipelines.GetModelPso(blendMode));
		commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	}

	const TextureResource& texture = ( textureIndex == 1 ) ? assets.GetSpriteTexture() : assets.GetWhiteTexture();

	commandList->SetGraphicsRootConstantBufferView(0, materialResource_.Get()->GetGPUVirtualAddress());
	commandList->SetGraphicsRootConstantBufferView(1, wvpResource_.Get()->GetGPUVirtualAddress());
	commandList->SetGraphicsRootDescriptorTable(2, texture.srvGpu);
	commandList->SetGraphicsRootConstantBufferView(3, lightAddress);
	commandList->IASetVertexBuffers(0, 1, &vbv_);
	commandList->DrawInstanced(vertexCount_, 1, 0, 0);

	if ( geometry_.isLine )
	{
		// 後に続く描画は三角形のつもりでいるので戻しておく
		commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	}
}

void ShapeObject::DrawUi()
{
#ifdef USE_IMGUI
	ImGui::PushID(this);

	// 名前
	char nameBuffer[64];
	std::snprintf(nameBuffer, sizeof(nameBuffer), "%s", name.c_str());
	if ( ImGui::InputText("Name", nameBuffer, sizeof(nameBuffer)) )
	{
		name = nameBuffer;
	}
	ImGui::Text("Type: %s   Vertices: %d", kShapeTypeNames[static_cast< int >( type_ )], static_cast< int >( vertexCount_ ));
	ImGui::Checkbox("Visible", &visible);

	// --- Transform All ---
	if ( ImGui::CollapsingHeader("Transform All (every vertex)", ImGuiTreeNodeFlags_DefaultOpen) )
	{
		ImGui::DragFloat3("Scale", &transform.scale.x, 0.05f);
		ImGui::DragFloat3("Rotate", &transform.rotate.x, 0.02f);
		ImGui::DragFloat3("Translate", &transform.translate.x, 0.05f);
		if ( ImGui::Button("Reset Transform All") )
		{
			transform = kIdentityTransform;
		}

		if ( ShapeFactory::UsesSegments(type_) )
		{
			int segments = segments_;
			if ( ImGui::SliderInt("Segments", &segments, ShapeFactory::kMinSegments, ShapeFactory::kMaxSegments) )
			{
				segments_ = segments;
				RebuildGeometry();
			}
		}
	}

	// --- Individual ---
	if ( ImGui::CollapsingHeader("Individual Transforms (per part)", ImGuiTreeNodeFlags_DefaultOpen) )
	{
		ImGui::Checkbox("Use Individual Transforms", &useIndividual);
		ImGui::TextDisabled("Applied around each part's pivot, then Transform All is applied.");

		for ( size_t i = 0; i < geometry_.parts.size() && i < partTransforms.size(); ++i )
		{
			const ShapePartInfo& part = geometry_.parts[i];
			TransformData& t = partTransforms[i];

			ImGui::PushID(static_cast< int >(i));
			if ( ImGui::TreeNode(part.name.c_str()) )
			{
				ImGui::DragFloat3("Translate", &t.translate.x, 0.02f);
				// 1頂点のパーツ(端点・三角形の頂点)は、回転・拡大しても動かないので出さない
				if ( part.vertexCount > 1 )
				{
					ImGui::DragFloat3("Rotate", &t.rotate.x, 0.02f);
					ImGui::DragFloat3("Scale", &t.scale.x, 0.02f);
				}
				if ( ImGui::Button("Reset Part") )
				{
					t = kIdentityTransform;
				}
				ImGui::TreePop();
			}
			ImGui::PopID();
		}
		if ( ImGui::Button("Reset All Parts") )
		{
			std::fill(partTransforms.begin(), partTransforms.end(), kIdentityTransform);
		}
	}

	// --- Material ---
	if ( ImGui::CollapsingHeader("Material") )
	{
		ImGui::ColorEdit4("Color", &materialData_->color.x);

		int lighting = static_cast< int >( materialData_->lightingMode );
		const char* lightingItems[] = { "None", "Half Lambert", "Lambert" };
		if ( ImGui::Combo("Lighting Mode", &lighting, lightingItems, IM_ARRAYSIZE(lightingItems)) )
		{
			materialData_->lightingMode = static_cast< LightingMode >( lighting );
		}

		const char* textureItems[] = { "White", "UV Checker" };
		ImGui::Combo("Texture", &textureIndex, textureItems, IM_ARRAYSIZE(textureItems));

		ImGui::DragFloat2("UV Translate", &uvTransform.translate.x, 0.1f, -10.0f, 10.0f);
		ImGui::DragFloat2("UV Scale", &uvTransform.scale.x, 0.1f, -10.0f, 10.0f);
		ImGui::SliderAngle("UV Rotate", &uvTransform.rotate.z);
	}

	ImGui::PopID();
#endif
}

void ShapeObject::Shutdown()
{
	mappedVertices_ = nullptr;
	materialData_ = nullptr;
	wvpData_ = nullptr;
	vertexCount_ = 0;
	vertexResource_.Reset();
	materialResource_.Reset();
	wvpResource_.Reset();
}