#include "ShapeGeometry.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace
{
	constexpr float kPi = std::numbers::pi_v<float>;

	Vector4 P(float x, float y, float z)
	{
		return { x, y, z, 1.0f };
	}

	VertexData MakeVertex(const Vector4& position, float u, float v, const Vector3& normal)
	{
		return { position, { u, v }, normal };
	}

	Vector3 Cross(const Vector3& a, const Vector3& b)
	{
		return { a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x };
	}

	//----------------------------------------------------------------
	// 箱(一辺1)
	//----------------------------------------------------------------
	ShapeGeometry CreateBox()
	{
		ShapeGeometry g;

		struct Face
		{
			const char* name;
			Vector3 normal;
			Vector3 uAxis; // テクスチャの右方向
			Vector3 vAxis; // テクスチャの上方向
		};
		const Face faces[] = {
			{ "+X", { 1.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, -1.0f }, { 0.0f, 1.0f, 0.0f } },
			{ "-X", { -1.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 1.0f }, { 0.0f, 1.0f, 0.0f } },
			{ "+Y", { 0.0f, 1.0f, 0.0f }, { 1.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, -1.0f } },
			{ "-Y", { 0.0f, -1.0f, 0.0f }, { 1.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 1.0f } },
			{ "+Z", { 0.0f, 0.0f, 1.0f }, { -1.0f, 0.0f, 0.0f }, { 0.0f, 1.0f, 0.0f } },
			{ "-Z", { 0.0f, 0.0f, -1.0f }, { 1.0f, 0.0f, 0.0f }, { 0.0f, 1.0f, 0.0f } },
		};

		for ( const Face& f : faces )
		{
			const Vector3 c = { f.normal.x * 0.5f, f.normal.y * 0.5f, f.normal.z * 0.5f };
			auto corner = [&] (float su, float sv, float u, float v)
			{
				return MakeVertex(
					P(c.x + f.uAxis.x * 0.5f * su + f.vAxis.x * 0.5f * sv,
						c.y + f.uAxis.y * 0.5f * su + f.vAxis.y * 0.5f * sv,
						c.z + f.uAxis.z * 0.5f * su + f.vAxis.z * 0.5f * sv),
					u, v, f.normal);
			};
			const VertexData tl = corner(-1.0f, 1.0f, 0.0f, 0.0f);
			const VertexData tr = corner(1.0f, 1.0f, 1.0f, 0.0f);
			const VertexData bl = corner(-1.0f, -1.0f, 0.0f, 1.0f);
			const VertexData br = corner(1.0f, -1.0f, 1.0f, 1.0f);

			ShapePartInfo part;
			part.name = f.name;
			part.firstVertex = static_cast< uint32_t >( g.vertices.size() );
			part.vertexCount = 6;
			part.pivot = c;
			g.parts.push_back(part);

			g.vertices.insert(g.vertices.end(), { tl, tr, bl, bl, tr, br });
		}
		return g;
	}

	//----------------------------------------------------------------
	// 線(長さ1。X方向)
	//----------------------------------------------------------------
	ShapeGeometry CreateLine()
	{
		ShapeGeometry g;
		g.isLine = true;

		const Vector3 normal = { 0.0f, 0.0f, -1.0f };
		g.vertices.push_back(MakeVertex(P(-0.5f, 0.0f, 0.0f), 0.0f, 0.0f, normal));
		g.vertices.push_back(MakeVertex(P(0.5f, 0.0f, 0.0f), 1.0f, 1.0f, normal));

		// 端点は1頂点のパーツ。pivotを自分の位置にして、回転・拡大では動かないようにする
		g.parts.push_back({ "Start", 0, 1, { -0.5f, 0.0f, 0.0f } });
		g.parts.push_back({ "End", 1, 1, { 0.5f, 0.0f, 0.0f } });
		return g;
	}

	//----------------------------------------------------------------
	// 三角形(1×1に収まる。XY平面)
	//----------------------------------------------------------------
	ShapeGeometry CreateTriangle()
	{
		ShapeGeometry g;
		g.recomputeNormals = true;

		const Vector3 normal = { 0.0f, 0.0f, -1.0f }; // カメラ(-Z側)を向く
		g.vertices.push_back(MakeVertex(P(0.0f, 0.5f, 0.0f), 0.5f, 0.0f, normal));
		g.vertices.push_back(MakeVertex(P(-0.5f, -0.5f, 0.0f), 0.0f, 1.0f, normal));
		g.vertices.push_back(MakeVertex(P(0.5f, -0.5f, 0.0f), 1.0f, 1.0f, normal));

		g.parts.push_back({ "A", 0, 1, { 0.0f, 0.5f, 0.0f } });
		g.parts.push_back({ "B", 1, 1, { -0.5f, -0.5f, 0.0f } });
		g.parts.push_back({ "C", 2, 1, { 0.5f, -0.5f, 0.0f } });
		return g;
	}

	//----------------------------------------------------------------
	// 球(半径0.5)。緯度・経度の分割(AssetCacheの球と同じ作り)
	//----------------------------------------------------------------
	ShapeGeometry CreateSphere(int segments)
	{
		ShapeGeometry g;
		const uint32_t n = static_cast< uint32_t >( std::max(segments, 4) ); // 半球に分けるので最低4
		constexpr float kRadius = 0.5f;

		const float lonEvery = ( 2.0f * kPi ) / static_cast< float >( n ); // 経度分割1つ分の角度
		const float latEvery = kPi / static_cast< float >( n );            // 緯度分割1つ分の角度
		const float step = 1.0f / static_cast< float >( n );

		auto point = [&] (float lat, float lon, float u, float v)
		{
			const Vector3 normal = { std::cos(lat) * std::cos(lon), std::sin(lat), std::cos(lat) * std::sin(lon) };
			return MakeVertex(P(normal.x * kRadius, normal.y * kRadius, normal.z * kRadius), u, v, normal);
		};

		const uint32_t half = n / 2; // 緯度インデックスがhalf未満 = 南半球
		for ( uint32_t latIndex = 0; latIndex < n; ++latIndex )
		{
			const float lat = -kPi / 2.0f + latEvery * static_cast< float >(latIndex);
			for ( uint32_t lonIndex = 0; lonIndex < n; ++lonIndex )
			{
				const float u = static_cast< float >(lonIndex) / static_cast< float >(n);
				const float v = 1.0f - static_cast< float >(latIndex) / static_cast< float >(n);
				const float lon = static_cast< float >(lonIndex) * lonEvery;

				const VertexData a = point(lat, lon, u, v);
				const VertexData b = point(lat + latEvery, lon, u, v - step);
				const VertexData c = point(lat, lon + lonEvery, u + step, v);
				const VertexData d = point(lat + latEvery, lon + lonEvery, u + step, v - step);
				g.vertices.insert(g.vertices.end(), { a, b, c, b, d, c });
			}
		}

		const uint32_t southCount = half * n * 6;
		const uint32_t total = static_cast< uint32_t >( g.vertices.size() );
		g.parts.push_back({ "South", 0, southCount, { 0.0f, 0.0f, 0.0f } });
		g.parts.push_back({ "North", southCount, total - southCount, { 0.0f, 0.0f, 0.0f } });
		return g;
	}

	//----------------------------------------------------------------
	// 四角形(2D版の箱。一辺1。XY平面)
	//----------------------------------------------------------------
	ShapeGeometry CreateQuad()
	{
		ShapeGeometry g;
		const Vector3 normal = { 0.0f, 0.0f, -1.0f }; // カメラ(-Z側)を向く
		const VertexData tl = MakeVertex(P(-0.5f, 0.5f, 0.0f), 0.0f, 0.0f, normal);
		const VertexData tr = MakeVertex(P(0.5f, 0.5f, 0.0f), 1.0f, 0.0f, normal);
		const VertexData bl = MakeVertex(P(-0.5f, -0.5f, 0.0f), 0.0f, 1.0f, normal);
		const VertexData br = MakeVertex(P(0.5f, -0.5f, 0.0f), 1.0f, 1.0f, normal);
		g.vertices.insert(g.vertices.end(), { tl, tr, bl, bl, tr, br });
		g.parts.push_back({ "Face", 0, 6, { 0.0f, 0.0f, 0.0f } });
		return g;
	}

	//----------------------------------------------------------------
	// 円(2D版の球。直径1。XY平面。中心から扇形に三角形を並べる)
	//----------------------------------------------------------------
	ShapeGeometry CreateCircle(int segments)
	{
		ShapeGeometry g;
		const uint32_t n = static_cast< uint32_t >( std::max(segments, ShapeFactory::kMinSegments) );
		constexpr float kRadius = 0.5f;
		const Vector3 normal = { 0.0f, 0.0f, -1.0f };
		const float every = ( 2.0f * kPi ) / static_cast< float >( n );

		for ( uint32_t i = 0; i < n; ++i )
		{
			const float a0 = every * static_cast< float >(i);
			const float a1 = every * static_cast< float >(i + 1);
			// UVは、円がちょうど収まる正方形に貼る
			const VertexData center = MakeVertex(P(0.0f, 0.0f, 0.0f), 0.5f, 0.5f, normal);
			const VertexData e0 = MakeVertex(P(std::cos(a0) * kRadius, std::sin(a0) * kRadius, 0.0f),
				0.5f + std::cos(a0) * 0.5f, 0.5f - std::sin(a0) * 0.5f, normal);
			const VertexData e1 = MakeVertex(P(std::cos(a1) * kRadius, std::sin(a1) * kRadius, 0.0f),
				0.5f + std::cos(a1) * 0.5f, 0.5f - std::sin(a1) * 0.5f, normal);
			// 反時計回りで並べる(カメラ側から見て表になる向き)
			g.vertices.insert(g.vertices.end(), { center, e1, e0 });
		}
		g.parts.push_back({ "Disc", 0, static_cast< uint32_t >( g.vertices.size() ), { 0.0f, 0.0f, 0.0f } });
		return g;
	}

	//----------------------------------------------------------------
	// 円錐(底面の半径0.5・高さ1。頂点が+Y、底面が-Y)
	//----------------------------------------------------------------
	ShapeGeometry CreateCone(int segments)
	{
		ShapeGeometry g;
		const uint32_t n = static_cast< uint32_t >( std::max(segments, ShapeFactory::kMinSegments) );
		constexpr float kRadius = 0.5f;
		constexpr float kHeight = 1.0f;
		const float top = kHeight * 0.5f;
		const float bottom = -kHeight * 0.5f;

		// 側面の法線(斜面の向き)
		auto sideNormal = [&] (float angle)
		{
			const float x = std::cos(angle) * kHeight;
			const float y = kRadius;
			const float z = std::sin(angle) * kHeight;
			return Normalize({ x, y, z });
		};

		const float every = ( 2.0f * kPi ) / static_cast< float >( n );

		// 側面: 頂点 → 底面の縁(i+1) → 底面の縁(i)
		for ( uint32_t i = 0; i < n; ++i )
		{
			const float a0 = every * static_cast< float >(i);
			const float a1 = every * static_cast< float >(i + 1);
			const float u0 = static_cast< float >(i) / static_cast< float >(n);
			const float u1 = static_cast< float >(i + 1) / static_cast< float >(n);

			const VertexData apex = MakeVertex(P(0.0f, top, 0.0f), ( u0 + u1 ) * 0.5f, 0.0f, sideNormal(( a0 + a1 ) * 0.5f));
			const VertexData b0 = MakeVertex(P(std::cos(a0) * kRadius, bottom, std::sin(a0) * kRadius), u0, 1.0f, sideNormal(a0));
			const VertexData b1 = MakeVertex(P(std::cos(a1) * kRadius, bottom, std::sin(a1) * kRadius), u1, 1.0f, sideNormal(a1));
			g.vertices.insert(g.vertices.end(), { apex, b1, b0 });
		}
		const uint32_t sideCount = static_cast< uint32_t >( g.vertices.size() );

		// 底面: 中心 → 縁(i) → 縁(i+1)。UVは円形に貼る
		const Vector3 down = { 0.0f, -1.0f, 0.0f };
		for ( uint32_t i = 0; i < n; ++i )
		{
			const float a0 = every * static_cast< float >(i);
			const float a1 = every * static_cast< float >(i + 1);

			const VertexData center = MakeVertex(P(0.0f, bottom, 0.0f), 0.5f, 0.5f, down);
			const VertexData e0 = MakeVertex(P(std::cos(a0) * kRadius, bottom, std::sin(a0) * kRadius),
				0.5f + std::cos(a0) * 0.5f, 0.5f + std::sin(a0) * 0.5f, down);
			const VertexData e1 = MakeVertex(P(std::cos(a1) * kRadius, bottom, std::sin(a1) * kRadius),
				0.5f + std::cos(a1) * 0.5f, 0.5f + std::sin(a1) * 0.5f, down);
			g.vertices.insert(g.vertices.end(), { center, e0, e1 });
		}
		const uint32_t total = static_cast< uint32_t >( g.vertices.size() );

		g.parts.push_back({ "Side", 0, sideCount, { 0.0f, 0.0f, 0.0f } });
		g.parts.push_back({ "Base", sideCount, total - sideCount, { 0.0f, bottom, 0.0f } });
		return g;
	}
}

//===================================================================
// ShapeFactory
//===================================================================
bool ShapeFactory::UsesSegments(ShapeType type, bool flat)
{
	( void ) flat; // 球 → 円でも分割数を使う
	return type == ShapeType::Sphere || type == ShapeType::Cone;
}

bool ShapeFactory::HasFlatVersion(ShapeType type)
{
	return type == ShapeType::Box || type == ShapeType::Sphere;
}

const char* ShapeFactory::GetDisplayName(ShapeType type, bool flat)
{
	if ( flat )
	{
		if ( type == ShapeType::Box )
		{
			return "Quad";
		}
		if ( type == ShapeType::Sphere )
		{
			return "Circle";
		}
	}
	return kShapeTypeNames[static_cast< int >( type )];
}

int ShapeFactory::DefaultSegments(ShapeType type)
{
	switch ( type )
	{
		case ShapeType::Sphere: return 16;
		case ShapeType::Cone: return 24;
		default: return 0;
	}
}

ShapeGeometry ShapeFactory::Create(ShapeType type, int segments, bool flat)
{
	segments = std::clamp(segments, kMinSegments, kMaxSegments);
	switch ( type )
	{
		case ShapeType::Box: return flat ? CreateQuad() : CreateBox();
		case ShapeType::Line: return CreateLine();
		case ShapeType::Triangle: return CreateTriangle();
		case ShapeType::Sphere: return flat ? CreateCircle(segments) : CreateSphere(segments);
		case ShapeType::Cone: return CreateCone(segments);
		default: break;
	}
	return CreateBox();
}

size_t ShapeFactory::MaxVertexCount(ShapeType type)
{
	return Create(type, kMaxSegments).vertices.size();
}

void ShapeFactory::BuildVertices(const ShapeGeometry& base, const std::vector<TransformData>& partTransforms,
	bool individual, VertexData* out)
{
	std::copy(base.vertices.begin(), base.vertices.end(), out);

	if ( individual )
	{
		for ( size_t i = 0; i < base.parts.size() && i < partTransforms.size(); ++i )
		{
			const ShapePartInfo& part = base.parts[i];
			const TransformData& t = partTransforms[i];

			// 拡大 → 回転(平行移動は後で足す)
			const Matrix4x4 scaleRotate = MakeAffineMatrix(t.scale, t.rotate, { 0.0f, 0.0f, 0.0f, 0.0f });
			// 法線は回転だけ
			const Matrix4x4 rotateOnly = MakeAffineMatrix({ 1.0f, 1.0f, 1.0f, 0.0f }, t.rotate, { 0.0f, 0.0f, 0.0f, 0.0f });

			for ( uint32_t v = part.firstVertex; v < part.firstVertex + part.vertexCount; ++v )
			{
				VertexData& vertex = out[v];

				// pivotからの相対位置に変形を掛け、pivotに戻して平行移動を足す(行ベクトル × 行列)
				const float dx = vertex.position.x - part.pivot.x;
				const float dy = vertex.position.y - part.pivot.y;
				const float dz = vertex.position.z - part.pivot.z;
				const Matrix4x4& m = scaleRotate;
				vertex.position.x = part.pivot.x + t.translate.x + dx * m.m[0][0] + dy * m.m[1][0] + dz * m.m[2][0];
				vertex.position.y = part.pivot.y + t.translate.y + dx * m.m[0][1] + dy * m.m[1][1] + dz * m.m[2][1];
				vertex.position.z = part.pivot.z + t.translate.z + dx * m.m[0][2] + dy * m.m[1][2] + dz * m.m[2][2];

				const Vector3 n = vertex.normal;
				const Matrix4x4& r = rotateOnly;
				vertex.normal = {
					n.x * r.m[0][0] + n.y * r.m[1][0] + n.z * r.m[2][0],
					n.x * r.m[0][1] + n.y * r.m[1][1] + n.z * r.m[2][1],
					n.x * r.m[0][2] + n.y * r.m[1][2] + n.z * r.m[2][2],
				};
			}
		}
	}

	// 三角形: 頂点が動くと面の向きが変わるので、法線を作り直す
	if ( base.recomputeNormals && base.vertices.size() >= 3 )
	{
		const Vector4& p0 = out[0].position;
		const Vector4& p1 = out[1].position;
		const Vector4& p2 = out[2].position;
		const Vector3 e1 = { p1.x - p0.x, p1.y - p0.y, p1.z - p0.z };
		const Vector3 e2 = { p2.x - p0.x, p2.y - p0.y, p2.z - p0.z };
		const Vector3 c = Cross(e2, e1); // 初期形状でカメラ側(-Z)を向く順序
		const float length = std::sqrt(c.x * c.x + c.y * c.y + c.z * c.z);
		if ( length > 1e-6f )
		{
			const Vector3 normal = { c.x / length, c.y / length, c.z / length };
			for ( size_t i = 0; i < 3; ++i )
			{
				out[i].normal = normal;
			}
		}
	}
}