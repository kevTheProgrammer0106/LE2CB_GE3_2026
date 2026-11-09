#pragma once

#include <string>
#include <unordered_map>
#include <vector>

#include "WorldTransform.h" // Vector2/3/4, VertexData, MaterialData

//===================================================================
// 読み込み時の設定値(モデルごとに変えられる)
// 既定値は、これまで ModelLoader に直書きされていた挙動と同じ
//===================================================================
struct ModelImportSettings
{
	float scale = 1.0f;                   // 全頂点に掛ける倍率
	Vector3 offset{ 0.0f, 0.0f, 0.0f };   // 倍率の後に足す平行移動(ピボット補正)
	bool centerOnOrigin = false;          // trueなら、バウンディングボックスの中心を原点に移す

	bool flipX = true;                    // X座標・法線Xを反転(右手系のobj → 左手系のDirectX)
	bool flipV = true;                    // VをV=1-Vに反転(DirectXは左上が原点)
	bool flipWinding = true;              // 三角形の頂点順を逆にする(flipXで裏返る回り順を戻す)
	bool generateMissingNormals = true;   // vnが無い頂点には面法線を入れる

	std::string textureOverride;          // 空でなければ、mtlのテクスチャを無視してこれを貼る
};

//===================================================================
// 読み込んだ結果
//===================================================================
struct Bounds
{
	Vector3 min{ 0.0f, 0.0f, 0.0f };
	Vector3 max{ 0.0f, 0.0f, 0.0f };
	bool valid = false; // 頂点が1つも無ければ false

	Vector3 Center() const
	{
		return { ( min.x + max.x ) * 0.5f, ( min.y + max.y ) * 0.5f, ( min.z + max.z ) * 0.5f };
	}
	Vector3 Size() const
	{
		return { max.x - min.x, max.y - min.y, max.z - min.z };
	}
};

struct SubMeshData
{
	std::string name;                // oで指定された名前(無ければ空)
	std::vector<VertexData> vertices;
	MaterialData material;
};

struct MultiMeshModelData
{
	std::vector<SubMeshData> subMeshes;
	Bounds bounds; // 全サブメッシュをまとめた範囲(設定の適用後)
};

/// <summary>
/// objファイルを読み込む。単一メッシュも複数メッシュも同じ関数で扱う
///  - oまたはusemtlが変わるたびにサブメッシュを分ける(頂点が1つも無いサブメッシュは作らない)
///  - 4頂点以上の面は三角形に分割する
///  - 開けない・中身が空のときは、ログを出して空の結果を返す(assertで止めないので、足りないモデルをまとめて確認できる)
/// </summary>
MultiMeshModelData LoadModel(const std::string& directoryPath, const std::string& filename, const ModelImportSettings& settings = {});

/// mtlファイルを読み込み、マテリアル名 → マテリアルの表を返す。firstNameには最初のマテリアル名が入る
std::unordered_map<std::string, MaterialData> LoadMaterialTemplateFile(
	const std::string& directoryPath, const std::string& filename, std::string* firstName = nullptr);