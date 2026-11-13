#include "Engine/Graphics/Sprite.h"
#include "Engine/Graphics/GpuBuffers.h"

#include "Engine/Graphics/DirectXCommon.h"
#include "Engine/Graphics/PipelineManager.h"

#ifdef USE_IMGUI
#include "externals/imgui/imgui.h"
#endif

void Sprite::Initialize(DirectXCommon& dx, D3D12_GPU_DESCRIPTOR_HANDLE textureSrv)
{
	constexpr UINT kVertexCount = 4;
	constexpr UINT kIndexCount = 6;

	textureSrv_ = textureSrv;

	// マテリアル(スプライトは基本的にLightingの影響を受けないが、元の挙動に合わせてHalfLambertのまま)
	materialData_ = CreateMaterialBuffer(dx, materialResource_, LightingMode::HalfLambert);

	// 頂点バッファ(4頂点・重複なし。インデックスバッファで三角形2枚を組む)
	VertexData* vertices = nullptr;
	dx.CreateMappedBuffer(vertexResource_, sizeof(VertexData) * kVertexCount, &vertices);
	vbv_.BufferLocation = vertexResource_.Get()->GetGPUVirtualAddress();
	vbv_.SizeInBytes = sizeof(VertexData) * kVertexCount;
	vbv_.StrideInBytes = sizeof(VertexData);

	vertices[0] = { { 0.0f, 360.0f, 0.0f, 1.0f }, { 0.0f, 1.0f }, { 0.0f, 0.0f, -1.0f } }; // 左下
	vertices[1] = { { 0.0f, 0.0f, 0.0f, 1.0f }, { 0.0f, 0.0f }, { 0.0f, 0.0f, -1.0f } }; // 左上
	vertices[2] = { { 640.0f, 0.0f, 0.0f, 1.0f }, { 1.0f, 0.0f }, { 0.0f, 0.0f, -1.0f } }; // 右上
	vertices[3] = { { 640.0f, 360.0f, 0.0f, 1.0f }, { 1.0f, 1.0f }, { 0.0f, 0.0f, -1.0f } }; // 右下

	// インデックスバッファ
	// 三角形1枚目: 左下(0)→左上(1)→右下(3) / 三角形2枚目: 左上(1)→右上(2)→右下(3)
	uint32_t* indices = nullptr;
	dx.CreateMappedBuffer(indexResource_, sizeof(uint32_t) * kIndexCount, &indices);
	ibv_.BufferLocation = indexResource_.Get()->GetGPUVirtualAddress();
	ibv_.SizeInBytes = sizeof(uint32_t) * kIndexCount;
	ibv_.Format = DXGI_FORMAT_R32_UINT;

	indices[0] = 0; indices[1] = 1; indices[2] = 3;
	indices[3] = 1; indices[4] = 2; indices[5] = 3;

	// TransformationMatrix
	transformData_ = CreateTransformBuffer(dx, transformResource_);
}

void Sprite::Update(float clientWidth, float clientHeight)
{
	// スプライトはカメラの影響を受けないので、ビュー行列は単位行列、射影は正射影
	Matrix4x4 world = MakeAffineMatrix(transform.scale, transform.rotate, transform.translate);
	Matrix4x4 view = MakeIdentityMatrix();
	Matrix4x4 projection = MakeOrthoGraphicMatrix(0.0f, 0.0f, clientWidth, clientHeight, 0.0f, 100.0f);
	transformData_->WVP = Multiply(world, Multiply(view, projection));
	transformData_->World = world;

	Matrix4x4 uv = MakeScaleMatrix(uvTransform.scale);
	uv = Multiply(uv, MakeRotateZMatrix(uvTransform.rotate.z));
	uv = Multiply(uv, MakeTranslateMatrix(uvTransform.translate));
	materialData_->uvTransform = uv;
}

void Sprite::Draw(ID3D12GraphicsCommandList* commandList, const PipelineManager& pipelines, D3D12_GPU_VIRTUAL_ADDRESS lightAddress) const
{
	if ( !enabled )
	{
		return;
	}

	commandList->SetPipelineState(pipelines.GetSpritePso());
	commandList->IASetVertexBuffers(0, 1, &vbv_);
	commandList->IASetIndexBuffer(&ibv_);
	commandList->SetGraphicsRootConstantBufferView(0, materialResource_.Get()->GetGPUVirtualAddress());
	commandList->SetGraphicsRootConstantBufferView(1, transformResource_.Get()->GetGPUVirtualAddress());
	commandList->SetGraphicsRootDescriptorTable(2, textureSrv_);
	commandList->SetGraphicsRootConstantBufferView(3, lightAddress);
	commandList->DrawIndexedInstanced(6, 1, 0, 0, 0);
}

void Sprite::DrawUi()
{
#ifdef USE_IMGUI
	if ( ImGui::Button("Enable Sprite") )
	{
		enabled = true;
	}
	if ( ImGui::Button("Disable Sprite") )
	{
		enabled = false;
	}
	if ( enabled )
	{
		ImGui::Text("Sprite Settings");
		ImGui::DragFloat3("Sprite Scale", &transform.scale.x, 0.1f);
		ImGui::DragFloat3("Sprite Rotate", &transform.rotate.x, 0.1f);
		ImGui::DragFloat3("Sprite Translate", &transform.translate.x, 0.1f);
		ImGui::DragFloat2("UV Translate", &uvTransform.translate.x, 0.1f, -10.0f, 10.0f);
		ImGui::DragFloat2("UV Scale", &uvTransform.scale.x, 0.1f, -10.0f, 10.0f);
		ImGui::SliderAngle("UV Rotate", &uvTransform.rotate.z);
	}
#endif
}
