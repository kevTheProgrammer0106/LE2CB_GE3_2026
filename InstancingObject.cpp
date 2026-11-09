#define NOMINMAX
#include "InstancingObject.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstring>

#include "DirectXCommon.h"
#include "PipelineManager.h"

#ifdef USE_IMGUI
#include "externals/imgui/imgui.h"
#endif

void InstancingObject::Initialize(DirectXCommon& dx)
{
	// 板ポリ(三角形2枚)
	const VertexData vertices[] = {
		{ { 1.0f, 1.0f, 0.0f, 1.0f }, { 0.0f, 0.0f }, { 0.0f, 0.0f, 1.0f } },
		{ { -1.0f, 1.0f, 0.0f, 1.0f }, { 1.0f, 0.0f }, { 0.0f, 0.0f, 1.0f } },
		{ { 1.0f, -1.0f, 0.0f, 1.0f }, { 0.0f, 1.0f }, { 0.0f, 0.0f, 1.0f } },
		{ { 1.0f, -1.0f, 0.0f, 1.0f }, { 0.0f, 1.0f }, { 0.0f, 0.0f, 1.0f } },
		{ { -1.0f, 1.0f, 0.0f, 1.0f }, { 1.0f, 0.0f }, { 0.0f, 0.0f, 1.0f } },
		{ { -1.0f, -1.0f, 0.0f, 1.0f }, { 1.0f, 1.0f }, { 0.0f, 0.0f, 1.0f } },
	};
	vertexCount_ = static_cast< UINT >( std::size(vertices) );

	vertexResource_ = dx.CreateBufferResource(sizeof(vertices));
	VertexData* mapped = nullptr;
	HRESULT hr = vertexResource_.Get()->Map(0, nullptr, reinterpret_cast< void** >( &mapped ));
	assert(SUCCEEDED(hr) && mapped != nullptr);
	( void ) hr;
	std::memcpy(mapped, vertices, sizeof(vertices));

	vbv_.BufferLocation = vertexResource_.Get()->GetGPUVirtualAddress();
	vbv_.SizeInBytes = static_cast< UINT >( sizeof(vertices) );
	vbv_.StrideInBytes = sizeof(VertexData);

	// 専用マテリアル
	materialResource_ = dx.CreateBufferResource(sizeof(Material));
	materialResource_.Get()->Map(0, nullptr, reinterpret_cast< void** >( &materialData_ ));
	*materialData_ = {};
	materialData_->color = { 1.0f, 1.0f, 1.0f, 1.0f };
	materialData_->lightingMode = LightingMode::None;
	materialData_->uvTransform = MakeIdentityMatrix();

	// インスタンスごとの行列バッファ
	instanceBuffer_ = dx.CreateBufferResource(sizeof(TransformationMatrix) * kNumInstance);
	instanceBuffer_.Get()->Map(0, nullptr, reinterpret_cast< void** >( &data_ ));

	for ( uint32_t i = 0; i < kNumInstance; ++i )
	{
		data_[i].WVP = MakeIdentityMatrix();
		data_[i].World = MakeIdentityMatrix();

		transforms_[i].scale = { 1.0f, 1.0f, 1.0f };
		transforms_[i].rotate = { 0.0f, 0.0f, 0.0f };
		transforms_[i].translate = { i * 0.1f, i * 0.1f, i * 0.1f };
	}

	// StructuredBuffer用のSRV
	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
	srvDesc.Format = DXGI_FORMAT_UNKNOWN;
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
	srvDesc.Buffer.FirstElement = 0;
	srvDesc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;
	srvDesc.Buffer.NumElements = kNumInstance;
	srvDesc.Buffer.StructureByteStride = sizeof(TransformationMatrix);

	const uint32_t srvIndex = dx.AllocateSrvIndex();
	dx.GetDevice()->CreateShaderResourceView(instanceBuffer_.Get(), &srvDesc, dx.GetSRVCPUDescriptorHandle(srvIndex));
	srvGpu_ = dx.GetSRVGPUDescriptorHandle(srvIndex);
}

void InstancingObject::Update(const Matrix4x4& viewProjection)
{
	for ( uint32_t i = 0; i < kNumInstance; ++i )
	{
		Matrix4x4 world = MakeAffineMatrix(transforms_[i].scale, transforms_[i].rotate, transforms_[i].translate);
		data_[i].WVP = Multiply(world, viewProjection);
		data_[i].World = world;
	}
}

void InstancingObject::Draw(ID3D12GraphicsCommandList* commandList, const PipelineManager& pipelines,
	D3D12_GPU_DESCRIPTOR_HANDLE textureSrv, D3D12_GPU_VIRTUAL_ADDRESS lightAddress) const
{
	if ( !enabled )
	{
		return;
	}

	commandList->SetPipelineState(pipelines.GetInstancingPso(ParticleBlendMode::Normal));
	commandList->SetGraphicsRootConstantBufferView(0, materialResource_.Get()->GetGPUVirtualAddress());
	// Particle.VSは使わないが、全ルートパラメータを設定済みにしておく(未設定のまま描くとデバッグレイヤーが止まるため)
	commandList->SetGraphicsRootConstantBufferView(1, instanceBuffer_.Get()->GetGPUVirtualAddress());
	commandList->SetGraphicsRootDescriptorTable(2, textureSrv);
	commandList->SetGraphicsRootConstantBufferView(3, lightAddress);
	commandList->SetGraphicsRootDescriptorTable(4, srvGpu_);
	commandList->IASetVertexBuffers(0, 1, &vbv_);
	commandList->DrawInstanced(vertexCount_, kNumInstance, 0, 0);
}

AABB InstancingObject::GetWorldBounds() const
{
	AABB bounds;
	for ( uint32_t i = 0; i < kNumInstance; ++i )
	{
		const TransformData& t = transforms_[i];
		const float hx = std::fabs(t.scale.x); // 板ポリは -1〜1 なので、半径 = 拡大率
		const float hy = std::fabs(t.scale.y);
		const Vector3 lo = { t.translate.x - hx, t.translate.y - hy, t.translate.z - 0.05f };
		const Vector3 hi = { t.translate.x + hx, t.translate.y + hy, t.translate.z + 0.05f };
		if ( i == 0 )
		{
			bounds = { lo, hi };
			continue;
		}
		bounds.min = { std::min(bounds.min.x, lo.x), std::min(bounds.min.y, lo.y), std::min(bounds.min.z, lo.z) };
		bounds.max = { std::max(bounds.max.x, hi.x), std::max(bounds.max.y, hi.y), std::max(bounds.max.z, hi.z) };
	}
	return bounds;
}

void InstancingObject::DrawUi()
{
#ifdef USE_IMGUI
	ImGui::Checkbox("Instancing Demo", &enabled);
#endif
}