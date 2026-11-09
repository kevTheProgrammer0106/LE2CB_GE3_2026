#define NOMINMAX
#include "Collider.h"

#include <algorithm>
#include <cmath>

#ifdef USE_IMGUI
#include "externals/imgui/imgui.h"
#endif

namespace
{
	float Dot(const Vector3& a, const Vector3& b)
	{
		return a.x * b.x + a.y * b.y + a.z * b.z;
	}

	Vector3 Sub(const Vector3& a, const Vector3& b)
	{
		return { a.x - b.x, a.y - b.y, a.z - b.z };
	}

	// 行列の第i行(行ベクトル規約では、ローカルのi軸がワールドでどちらを向き・どれだけ伸びるか)
	Vector3 Row(const Matrix4x4& m, int i)
	{
		return { m.m[i][0], m.m[i][1], m.m[i][2] };
	}
}

//===================================================================
// BoxCollider
//===================================================================
void BoxCollider::FitToBounds(const AABB& localBounds, float minThickness)
{
	center = {
		( localBounds.min.x + localBounds.max.x ) * 0.5f,
		( localBounds.min.y + localBounds.max.y ) * 0.5f,
		( localBounds.min.z + localBounds.max.z ) * 0.5f,
	};
	size = {
		std::max(localBounds.max.x - localBounds.min.x, minThickness),
		std::max(localBounds.max.y - localBounds.min.y, minThickness),
		std::max(localBounds.max.z - localBounds.min.z, minThickness),
	};
}

void BoxCollider::DrawUi()
{
#ifdef USE_IMGUI
	if ( ImGui::CollapsingHeader("Box Collider") )
	{
		ImGui::PushID("BoxCollider");
		ImGui::Checkbox("Enabled", &enabled);
		ImGui::Checkbox("Auto Fit (match mesh)", &autoFit);
		if ( autoFit )
		{
			// 自動のときは値を読むだけ(ImGuiのバージョンに依らないよう、無効化ではなく文字で表示する)
			ImGui::Text("Center: %.2f, %.2f, %.2f", center.x, center.y, center.z);
			ImGui::Text("Size:   %.2f, %.2f, %.2f", size.x, size.y, size.z);
		} else
		{
			ImGui::DragFloat3("Center", &center.x, 0.02f);
			ImGui::DragFloat3("Size", &size.x, 0.02f, 0.0f, 1000.0f);
		}
		ImGui::PopID();
	}
#endif
}

//===================================================================
// 補助
//===================================================================
AABB ComputeBounds(const VertexData* vertices, size_t count)
{
	AABB bounds;
	if ( vertices == nullptr || count == 0 )
	{
		return bounds;
	}
	bounds.min = { vertices[0].position.x, vertices[0].position.y, vertices[0].position.z };
	bounds.max = bounds.min;
	for ( size_t i = 1; i < count; ++i )
	{
		const Vector4& p = vertices[i].position;
		bounds.min.x = std::min(bounds.min.x, p.x);
		bounds.min.y = std::min(bounds.min.y, p.y);
		bounds.min.z = std::min(bounds.min.z, p.z);
		bounds.max.x = std::max(bounds.max.x, p.x);
		bounds.max.y = std::max(bounds.max.y, p.y);
		bounds.max.z = std::max(bounds.max.z, p.z);
	}
	return bounds;
}

Ray ScreenPointToRay(const Matrix4x4& viewProjection, float pixelX, float pixelY, float clientWidth, float clientHeight)
{
	// ピクセル → NDC(-1〜1、上が+)
	const float ndcX = ( pixelX / clientWidth ) * 2.0f - 1.0f;
	const float ndcY = 1.0f - ( pixelY / clientHeight ) * 2.0f;

	// viewProjectionの逆行列で、近い面(z=0)と遠い面(z=1)の点をワールドに戻す(DirectXの深度は0〜1)
	const Matrix4x4 inverse = Inverse(viewProjection);
	const Vector4 nearPoint = Transform({ ndcX, ndcY, 0.0f, 1.0f }, inverse);
	const Vector4 farPoint = Transform({ ndcX, ndcY, 1.0f, 1.0f }, inverse);

	Ray ray;
	ray.origin = { nearPoint.x, nearPoint.y, nearPoint.z };
	ray.direction = Normalize({ farPoint.x - nearPoint.x, farPoint.y - nearPoint.y, farPoint.z - nearPoint.z });
	return ray;
}

bool RayIntersectsBox(const Ray& ray, const BoxCollider& box, const Matrix4x4& world, float* outDistance)
{
	if ( !box.enabled )
	{
		return false;
	}

	// ローカルの各軸のワールドでの向き(長さ = 拡大率)と、原点
	const Vector3 axes[3] = { Row(world, 0), Row(world, 1), Row(world, 2) };
	const Vector3 origin = Row(world, 3);
	const Vector3 toOrigin = Sub(ray.origin, origin);

	const float localCenter[3] = { box.center.x, box.center.y, box.center.z };
	const float localHalf[3] = { box.size.x * 0.5f, box.size.y * 0.5f, box.size.z * 0.5f };

	float tMin = 0.0f;
	float tMax = 1e30f;

	for ( int i = 0; i < 3; ++i )
	{
		const float lengthSq = Dot(axes[i], axes[i]);
		if ( lengthSq < 1e-12f )
		{
			return false; // その軸が潰れている(拡大率0)
		}

		// レイの始点と向きを、ローカル座標のi軸に直す(軸が直交しているので内積で済む)
		const float o = Dot(toOrigin, axes[i]) / lengthSq;
		const float d = Dot(ray.direction, axes[i]) / lengthSq;
		const float lo = localCenter[i] - localHalf[i];
		const float hi = localCenter[i] + localHalf[i];

		if ( std::fabs(d) < 1e-9f )
		{
			// この軸と平行: 範囲の外にあれば当たらない
			if ( o < lo || o > hi )
			{
				return false;
			}
			continue;
		}

		float t1 = ( lo - o ) / d;
		float t2 = ( hi - o ) / d;
		if ( t1 > t2 )
		{
			std::swap(t1, t2);
		}
		tMin = std::max(tMin, t1);
		tMax = std::min(tMax, t2);
		if ( tMin > tMax )
		{
			return false;
		}
	}

	if ( outDistance )
	{
		*outDistance = tMin; // 始点が箱の中なら0
	}
	return true;
}

void GetBoxCorners(const BoxCollider& box, const Matrix4x4& world, Vector3 outCorners[8])
{
	const Vector3 half = { box.size.x * 0.5f, box.size.y * 0.5f, box.size.z * 0.5f };
	for ( int i = 0; i < 8; ++i )
	{
		// ローカルの角 → ワールド(行ベクトル × 行列)
		const float x = box.center.x + ( ( i & 1 ) ? half.x : -half.x );
		const float y = box.center.y + ( ( i & 2 ) ? half.y : -half.y );
		const float z = box.center.z + ( ( i & 4 ) ? half.z : -half.z );
		outCorners[i] = {
			x * world.m[0][0] + y * world.m[1][0] + z * world.m[2][0] + world.m[3][0],
			x * world.m[0][1] + y * world.m[1][1] + z * world.m[2][1] + world.m[3][1],
			x * world.m[0][2] + y * world.m[1][2] + z * world.m[2][2] + world.m[3][2],
		};
	}
}

AABB GetWorldAABB(const BoxCollider& box, const Matrix4x4& world)
{
	Vector3 corners[8];
	GetBoxCorners(box, world, corners);

	AABB bounds;
	bounds.min = corners[0];
	bounds.max = corners[0];
	for ( int i = 1; i < 8; ++i )
	{
		bounds.min.x = std::min(bounds.min.x, corners[i].x);
		bounds.min.y = std::min(bounds.min.y, corners[i].y);
		bounds.min.z = std::min(bounds.min.z, corners[i].z);
		bounds.max.x = std::max(bounds.max.x, corners[i].x);
		bounds.max.y = std::max(bounds.max.y, corners[i].y);
		bounds.max.z = std::max(bounds.max.z, corners[i].z);
	}
	return bounds;
}