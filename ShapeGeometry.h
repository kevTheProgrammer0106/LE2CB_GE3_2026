#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "WorldTransform.h"

//===================================================================
// プリミティブ図形の頂点生成(CPUだけ。GPUには依存しない)
//
// どの図形も「1×1×1に収まる大きさ」で作る(箱は一辺1、球は直径1、円錐は底面直径1・高さ1)。
// 大きさは ShapeObject の Transform(Scale) で決める。
//
// 図形は「パーツ」に分かれている。パーツ = 頂点のまとまり。
//   Transform All : 図形全体(全パーツ)に掛かる、ShapeObject の Transform
//   Individual    : パーツごとの Transform。パーツの中心(pivot)を基準に 拡大 → 回転 → 平行移動 する
//
//   Box      : 面6枚(+X -X +Y -Y +Z -Z)
//   Line     : 端点2つ(Start / End)             … 点は回転・拡大しても動かないので、実質「端点の移動」
//   Triangle : 頂点3つ(A / B / C)               … 同上
//   Sphere   : 半球2つ(South / North)
//   Cone     : 側面(Side)と底面(Base)
//===================================================================

enum class ShapeType : int
{
	Box = 0,
	Line,
	Triangle,
	Sphere,
	Cone,

	Count_
};

// ImGuiのコンボ・名前表(ShapeTypeと並びを同期させること)
inline constexpr const char* kShapeTypeNames[] = { "Box", "Line", "Triangle", "Sphere", "Cone" };
inline constexpr int kShapeTypeCount = static_cast< int >( ShapeType::Count_ );
static_assert( sizeof(kShapeTypeNames) / sizeof(kShapeTypeNames[0]) == kShapeTypeCount,
	"kShapeTypeNames must match ShapeType" );

struct ShapePartInfo
{
	std::string name;
	uint32_t firstVertex = 0; // 頂点配列の中で、このパーツが始まる位置
	uint32_t vertexCount = 0;
	Vector3 pivot{ 0.0f, 0.0f, 0.0f }; // 拡大・回転の中心(図形のローカル座標)
};

struct ShapeGeometry
{
	std::vector<VertexData> vertices;
	std::vector<ShapePartInfo> parts;
	bool isLine = false;           // trueなら LINELIST、falseなら TRIANGLELIST で描く
	bool recomputeNormals = false; // trueならパーツ変形のあとに面法線を作り直す(三角形用)
};

class ShapeFactory
{
public:
	ShapeFactory() = delete;

	// 球・円錐の分割数の範囲(GPUバッファは最大数ぶんを最初に確保するので、描画中に増やしても作り直しは不要)
	static constexpr int kMinSegments = 3;
	static constexpr int kMaxSegments = 32;

	/// 球と円錐だけ分割数を持つ
	static bool UsesSegments(ShapeType type);
	static int DefaultSegments(ShapeType type);

	/// 頂点とパーツを作る(segmentsは球・円錐以外では無視される)
	static ShapeGeometry Create(ShapeType type, int segments);
	/// その図形が取りうる最大の頂点数(バッファの大きさを決めるのに使う)
	static size_t MaxVertexCount(ShapeType type);

	/// 基本の頂点に、パーツごとの変形を掛けて out に書く。out は base.vertices.size() 個ぶんの領域
	/// individual が false のときは、パーツの変形を無視して基本の形のまま書く
	static void BuildVertices(const ShapeGeometry& base, const std::vector<TransformData>& partTransforms,
		bool individual, VertexData* out);
};