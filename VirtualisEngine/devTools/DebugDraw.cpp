#define NOMINMAX
#include "DebugDraw.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <numbers>

#include "AssetCache.h"
#include "DirectXCommon.h"
#include "PipelineManager.h"

void DebugDraw::Initialize(DirectXCommon& dx)
{
	// 頂点バッファ(最大本数ぶんを最初に1回だけ確保する)
	const size_t bytes = sizeof(DebugVertex) * kMaxLines * 2;
	vertexResource_ = dx.CreateBufferResource(bytes);
	HRESULT hr = vertexResource_.Get()->Map(0, nullptr, reinterpret_cast< void** >( &mapped_ ));
	assert(SUCCEEDED(hr) && mapped_ != nullptr);
	( void ) hr;

	vbv_.BufferLocation = vertexResource_.Get()->GetGPUVirtualAddress();
	vbv_.StrideInBytes = sizeof(DebugVertex);
	vbv_.SizeInBytes = 0;

	materialResource_ = dx.CreateBufferResource(sizeof(Material));
	Material* material = nullptr;
	materialResource_.Get()->Map(0, nullptr, reinterpret_cast< void** >( &material ));
	*material = {};
	material->color = { 1.0f, 1.0f, 1.0f, 1.0f };
	material->lightingMode = LightingMode::None;
	material->uvTransform = MakeIdentityMatrix();

	wvpResource_ = dx.CreateBufferResource(sizeof(TransformationMatrix));
	wvpResource_.Get()->Map(0, nullptr, reinterpret_cast< void** >( &wvpData_ ));
	wvpData_->WVP = MakeIdentityMatrix();
	wvpData_->World = MakeIdentityMatrix();
}

void DebugDraw::Begin(const Matrix4x4& viewProjection)
{
	vertexCount_ = 0;
	wvpData_->WVP = viewProjection; // 頂点はワールド座標で積むので、WVP = viewProjection
	wvpData_->World = MakeIdentityMatrix();
}

void DebugDraw::Line(const Vector3& a, const Vector3& b, const Vector4& color)
{
	if ( vertexCount_ + 2 > kMaxLines * 2 )
	{
		return; // 満杯。足りない分は捨てる
	}
	mapped_[vertexCount_++] = { { a.x, a.y, a.z, 1.0f }, color };
	mapped_[vertexCount_++] = { { b.x, b.y, b.z, 1.0f }, color };
}

void DebugDraw::Box(const AABB& box, const Vector4& color)
{
	// 角の番号のビット: x = bit0, y = bit1, z = bit2
	Vector3 corners[8];
	for ( int i = 0; i < 8; ++i )
	{
		corners[i] = { ( i & 1 ) ? box.max.x : box.min.x, ( i & 2 ) ? box.max.y : box.min.y, ( i & 4 ) ? box.max.z : box.min.z };
	}

	// 12本の辺: ちょうど1ビットだけ違う角どうしをつなぐ
	for ( int i = 0; i < 8; ++i )
	{
		for ( int bit = 1; bit <= 4; bit <<= 1 )
		{
			if ( ( i & bit ) == 0 )
			{
				Line(corners[i], corners[i | bit], color);
			}
		}
	}
}

void DebugDraw::BoxAt(const Vector3& center, float halfX, float halfY, float halfZ, const Vector4& color)
{
	AABB box;
	box.min = { center.x - halfX, center.y - halfY, center.z - halfZ };
	box.max = { center.x + halfX, center.y + halfY, center.z + halfZ };
	Box(box, color);
}

void DebugDraw::ColliderBox(const BoxCollider& collider, const Matrix4x4& world, const Vector4& color)
{
	Vector3 corners[8];
	GetBoxCorners(collider, world, corners);
	for ( int i = 0; i < 8; ++i )
	{
		for ( int bit = 1; bit <= 4; bit <<= 1 )
		{
			if ( ( i & bit ) == 0 )
			{
				Line(corners[i], corners[i | bit], color);
			}
		}
	}
}

void DebugDraw::Circle(const Vector3& center, float radius, const Vector4& color, int segments)
{
	segments = std::max(segments, 3);
	const float step = 2.0f * std::numbers::pi_v<float> / static_cast< float >( segments );
	Vector3 prev = { center.x + radius, center.y, center.z };
	for ( int i = 1; i <= segments; ++i )
	{
		const float a = step * static_cast< float >( i );
		const Vector3 next = { center.x + std::cos(a) * radius, center.y + std::sin(a) * radius, center.z };
		Line(prev, next, color);
		prev = next;
	}
}

void DebugDraw::Arrow(const Vector3& a, const Vector3& b, const Vector4& color)
{
	Line(a, b, color);

	Vector3 d = { b.x - a.x, b.y - a.y, b.z - a.z };
	const float len = std::sqrt(d.x * d.x + d.y * d.y + d.z * d.z);
	if ( len < 0.0001f )
	{
		return;
	}
	d = { d.x / len, d.y / len, d.z / len };

	// 矢じりの2方向: s1 = d × 上(真上を向いているときは右を使う)、s2 = d × s1
	Vector3 s1 = { d.z, 0.0f, -d.x };
	const float s1Len = std::sqrt(s1.x * s1.x + s1.z * s1.z);
	if ( s1Len < 0.0001f )
	{
		s1 = { 1.0f, 0.0f, 0.0f };
	} else
	{
		s1 = { s1.x / s1Len, 0.0f, s1.z / s1Len };
	}
	const Vector3 s2 = { d.y * s1.z - d.z * s1.y, d.z * s1.x - d.x * s1.z, d.x * s1.y - d.y * s1.x };

	const float head = std::min(len * 0.2f, 2.0f);
	const float wing = head * 0.4f;
	const Vector3 base = { b.x - d.x * head, b.y - d.y * head, b.z - d.z * head };
	const float signs[4][2] = { { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 } };
	for ( const auto& sg : signs )
	{
		const Vector3 tip = {
			base.x + ( s1.x * sg[0] + s2.x * sg[1] ) * wing,
			base.y + ( s1.y * sg[0] + s2.y * sg[1] ) * wing,
			base.z + ( s1.z * sg[0] + s2.z * sg[1] ) * wing,
		};
		Line(b, tip, color);
	}
}

void DebugDraw::Draw(ID3D12GraphicsCommandList* commandList, const PipelineManager& pipelines, const AssetCache& assets,
	D3D12_GPU_VIRTUAL_ADDRESS lightAddress) const
{
	if ( vertexCount_ == 0 )
	{
		return;
	}

	commandList->SetPipelineState(pipelines.GetDebugLinePso());
	commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_LINELIST);

	// 全ルートパラメータを設定済みにしておく(未設定のまま描くとデバッグレイヤーが止まるため)
	commandList->SetGraphicsRootConstantBufferView(0, materialResource_.Get()->GetGPUVirtualAddress());
	commandList->SetGraphicsRootConstantBufferView(1, wvpResource_.Get()->GetGPUVirtualAddress());
	commandList->SetGraphicsRootDescriptorTable(2, assets.GetWhiteTexture().srvGpu);
	commandList->SetGraphicsRootConstantBufferView(3, lightAddress);

	D3D12_VERTEX_BUFFER_VIEW view = vbv_;
	view.SizeInBytes = vertexCount_ * sizeof(DebugVertex);
	commandList->IASetVertexBuffers(0, 1, &view);
	commandList->DrawInstanced(vertexCount_, 1, 0, 0);

	// 後に続く描画は三角形のつもりでいるので戻しておく
	commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
}

void DebugDraw::Shutdown()
{
	mapped_ = nullptr;
	wvpData_ = nullptr;
	vertexCount_ = 0;
	vertexResource_.Reset();
	materialResource_.Reset();
	wvpResource_.Reset();
}