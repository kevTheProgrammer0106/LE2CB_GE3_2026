#include "VirtualisEngine/Graphics/DebugDraw.h"
#include "VirtualisEngine/Graphics/GpuBuffers.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <numbers>

#include "VirtualisEngine/Graphics/AssetCache.h"
#include "VirtualisEngine/Graphics/DirectXCommon.h"
#include "VirtualisEngine/Graphics/PipelineManager.h"

void DebugDraw::Initialize(DirectXCommon& dx)
{
	// 頂点バッファ(最大本数ぶんを最初に1回だけ確保する)
	const size_t bytes = sizeof(DebugVertex) * kMaxLines * 2;
	dx.CreateMappedBuffer(vertexResource_, bytes, &mapped_);

	vbv_.BufferLocation = vertexResource_.Get()->GetGPUVirtualAddress();
	vbv_.StrideInBytes = sizeof(DebugVertex);
	vbv_.SizeInBytes = 0;

	CreateMaterialBuffer(dx, materialResource_, LightingMode::None);

	wvpData_ = CreateTransformBuffer(dx, wvpResource_);
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
	segments = (std::max)(segments, 3);
	const float step = 2.0f * kPi / static_cast< float >( segments );
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

	const float head = (std::min)(len * 0.2f, 2.0f);
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

void DebugDraw::Grid(float halfWidth, uint32_t subdivision, const Vector4& color, const Vector4& centerColor, float y)
{
	subdivision = (std::max)(subdivision, 1u);
	const float every = ( halfWidth * 2.0f ) / static_cast< float >( subdivision ); // 1つ分の長さ

	// 奥から手前への線(Z方向)と、左から右への線(X方向)を順に引く
	for ( uint32_t i = 0; i <= subdivision; ++i )
	{
		const float offset = -halfWidth + every * static_cast< float >( i );
		const bool isCenter = ( i * 2 == subdivision ); // 原点を通る線
		const Vector4& c = isCenter ? centerColor : color;

		Line({ offset, y, -halfWidth }, { offset, y, halfWidth }, c);
		Line({ -halfWidth, y, offset }, { halfWidth, y, offset }, c);
	}
}

void DebugDraw::WireSphere(const Sphere& sphere, const Vector4& color, uint32_t subdivision)
{
	subdivision = (std::max)(subdivision, 3u);
	const float pi = kPi;
	const float lonEvery = ( 2.0f * pi ) / static_cast< float >( subdivision ); // 経度1つ分
	const float latEvery = pi / static_cast< float >( subdivision );            // 緯度1つ分

	// 単位球の点を、中心と半径でワールドへ移す
	auto point = [&sphere](float lat, float lon) -> Vector3
	{
		return {
			sphere.center.x + sphere.radius * std::cos(lat) * std::cos(lon),
			sphere.center.y + sphere.radius * std::sin(lat),
			sphere.center.z + sphere.radius * std::cos(lat) * std::sin(lon),
		};
	};

	// 緯度の方向に分割 -π/2 ~ π/2
	for ( uint32_t latIndex = 0; latIndex < subdivision; ++latIndex )
	{
		const float lat = -pi / 2.0f + latEvery * static_cast< float >( latIndex );
		// 経度の方向に分割 0 ~ 2π
		for ( uint32_t lonIndex = 0; lonIndex < subdivision; ++lonIndex )
		{
			const float lon = static_cast< float >( lonIndex ) * lonEvery;
			const Vector3 a = point(lat, lon);
			const Vector3 b = point(lat + latEvery, lon);
			const Vector3 c = point(lat, lon + lonEvery);
			Line(a, b, color); // ab: 経線
			Line(a, c, color); // ac: 緯線
		}
	}
}

void DebugDraw::WirePlane(const Plane& plane, const Vector4& color, float halfSize)
{
	const Vector3 center = Multiply(plane.distance, plane.normal);
	Vector3 perpendiculars[4];
	perpendiculars[0] = Normalize(Perpendicular(plane.normal));
	perpendiculars[1] = { -perpendiculars[0].x, -perpendiculars[0].y, -perpendiculars[0].z };
	perpendiculars[2] = Cross(plane.normal, perpendiculars[0]);
	perpendiculars[3] = { -perpendiculars[2].x, -perpendiculars[2].y, -perpendiculars[2].z };

	Vector3 points[4];
	for ( int i = 0; i < 4; ++i )
	{
		points[i] = Add(center, Multiply(halfSize, perpendiculars[i]));
	}
	// 0 → 2 → 1 → 3 → 0 の順に結ぶと四角になる
	Line(points[0], points[2], color);
	Line(points[2], points[1], color);
	Line(points[1], points[3], color);
	Line(points[3], points[0], color);
}

void DebugDraw::WireTriangle(const Triangle& triangle, const Vector4& color)
{
	Line(triangle.vertices[0], triangle.vertices[1], color);
	Line(triangle.vertices[1], triangle.vertices[2], color);
	Line(triangle.vertices[2], triangle.vertices[0], color);
}

void DebugDraw::WireSegment(const Segment& segment, const Vector4& color)
{
	Line(segment.origin, Add(segment.origin, segment.diff), color);
}

void DebugDraw::Bezier(const Vector3& p0, const Vector3& p1, const Vector3& p2, const Vector4& color,
	float pointRadius, const Vector4& pointColor, uint32_t curveDivision)
{
	curveDivision = (std::max)(curveDivision, 1u);
	Vector3 previous = p0;
	for ( uint32_t i = 1; i <= curveDivision; ++i )
	{
		const float t = static_cast< float >( i ) / static_cast< float >( curveDivision );
		const Vector3 next = CreateBezier(p0, p1, p2, t);
		Line(previous, next, color);
		previous = next;
	}

	// コントロールポイント
	if ( pointRadius > 0.0f )
	{
		WireSphere({ p0, pointRadius }, pointColor, 8);
		WireSphere({ p1, pointRadius }, pointColor, 8);
		WireSphere({ p2, pointRadius }, pointColor, 8);
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
