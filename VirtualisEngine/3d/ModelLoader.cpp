#include "ModelLoader.h"

#include <algorithm>
#include <fstream>
#include <sstream>

#include "Logger.h"

namespace
{
	// objの"f"行の1トークン("position/texcoord/normal")を分解して取得する。
	// - texcoordやnormalが省略されている("v//vn"や"v"だけ)場合は0(未指定)を返す
	// - 負のindex(相対index。「最後に追加された要素から数える」形式)にも対応する
	// 生のindex値(1始まり、負なら相対)をそのまま返す。0は「未指定」を表す。
	struct RawFaceIndices
	{
		int position = 0;
		int texcoord = 0;
		int normal = 0;
	};

	RawFaceIndices ParseFaceVertexToken(const std::string& token)
	{
		RawFaceIndices result;
		std::istringstream v(token);
		std::string idx;

		if ( std::getline(v, idx, '/') && !idx.empty() )
		{
			result.position = std::stoi(idx);
		}
		if ( std::getline(v, idx, '/') && !idx.empty() )
		{
			result.texcoord = std::stoi(idx);
		}
		if ( std::getline(v, idx, '/') && !idx.empty() )
		{
			result.normal = std::stoi(idx);
		}
		return result;
	}

	// 1始まりの絶対index、または負の相対index(-1 = 直前に追加された要素)を
	// 実際の配列添字(0始まり)に変換する。indexRaw==0(未指定)の場合は-1を返す。
	int ResolveIndex(int indexRaw, size_t arraySize)
	{
		if ( indexRaw > 0 )
		{
			return indexRaw - 1;
		}
		if ( indexRaw < 0 )
		{
			return static_cast< int >(arraySize) + indexRaw;
		}
		return -1; // 未指定
	}

	// 三角形の3頂点から面法線を計算する(vnが無い場合のフォールバック用)
	Vector3 CalculateFaceNormal(const Vector4& p0, const Vector4& p1, const Vector4& p2)
	{
		Vector3 e1 = { p1.x - p0.x, p1.y - p0.y, p1.z - p0.z };
		Vector3 e2 = { p2.x - p0.x, p2.y - p0.y, p2.z - p0.z };
		Vector3 n = {
			e1.y * e2.z - e1.z * e2.y,
			e1.z * e2.x - e1.x * e2.z,
			e1.x * e2.y - e1.y * e2.x
		};
		return Normalize(n);
	}

	// 面の1頂点分(設定の適用済み)
	struct FaceCorner
	{
		VertexData data{};
		bool normalMissing = false;
	};

	// "f"行の1トークンから頂点を組み立てる。
	// vt/vnが省略されていても、負の相対indexでも、範囲外でも、クラッシュしたり無効なメモリを読んだりしない
	FaceCorner BuildCorner(
		const std::string& token,
		const std::vector<Vector4>& positions,
		const std::vector<Vector2>& texcoords,
		const std::vector<Vector3>& normals,
		const ModelImportSettings& settings)
	{
		FaceCorner corner;
		const RawFaceIndices raw = ParseFaceVertexToken(token);

		const int posIndex = ResolveIndex(raw.position, positions.size());
		const int uvIndex = ResolveIndex(raw.texcoord, texcoords.size());
		const int nrmIndex = ResolveIndex(raw.normal, normals.size());

		// position: 必須。範囲外なら安全のため原点にしておく
		Vector4 position = { 0.0f, 0.0f, 0.0f, 1.0f };
		if ( posIndex >= 0 && posIndex < static_cast< int >(positions.size()) )
		{
			position = positions[posIndex];
		}
		if ( settings.flipX )
		{
			position.x *= -1.0f;
		}
		position.x = position.x * settings.scale + settings.offset.x;
		position.y = position.y * settings.scale + settings.offset.y;
		position.z = position.z * settings.scale + settings.offset.z;
		position.w = 1.0f;

		// texcoord: 省略されていたら(0,0)を使う
		Vector2 texcoord = { 0.0f, 0.0f };
		if ( uvIndex >= 0 && uvIndex < static_cast< int >(texcoords.size()) )
		{
			texcoord = texcoords[uvIndex];
			if ( settings.flipV )
			{
				texcoord.y = 1.0f - texcoord.y; // DirectXは左上が原点なので、V座標を反転させる
			}
		}

		// normal: 省略されていたら後で面法線から計算するので、ここでは印だけつけておく
		Vector3 normal = { 0.0f, 1.0f, 0.0f };
		if ( nrmIndex >= 0 && nrmIndex < static_cast< int >(normals.size()) )
		{
			normal = normals[nrmIndex];
			if ( settings.flipX )
			{
				normal.x *= -1.0f;
			}
		} else
		{
			corner.normalMissing = true;
		}

		corner.data = { position, texcoord, normal };
		return corner;
	}

	// "f"行1つ分を三角形に分けて out に足す(4頂点以上は扇形に分割)
	void AppendFace(
		std::istringstream& s,
		const std::vector<Vector4>& positions,
		const std::vector<Vector2>& texcoords,
		const std::vector<Vector3>& normals,
		const ModelImportSettings& settings,
		std::vector<VertexData>& out)
	{
		std::vector<FaceCorner> corners;
		std::string token;
		while ( s >> token )
		{
			corners.push_back(BuildCorner(token, positions, texcoords, normals, settings));
		}
		if ( corners.size() < 3 )
		{
			return; // 面にならない
		}

		for ( size_t i = 1; i + 1 < corners.size(); ++i )
		{
			FaceCorner tri[3] = { corners[0], corners[i], corners[i + 1] };

			// vnが1頂点でも無かった場合は、面法線を計算して欠けている頂点にだけ適用する
			// (フラットシェーディング相当。lighting用のnormalがおかしくならないようにするため)
			if ( settings.generateMissingNormals && ( tri[0].normalMissing || tri[1].normalMissing || tri[2].normalMissing ) )
			{
				const Vector3 faceNormal = CalculateFaceNormal(tri[0].data.position, tri[1].data.position, tri[2].data.position);
				for ( FaceCorner& c : tri )
				{
					if ( c.normalMissing )
					{
						c.data.normal = faceNormal;
					}
				}
			}

			// 頂点を逆順で登録することで、回り順を逆にする
			if ( settings.flipWinding )
			{
				out.push_back(tri[2].data);
				out.push_back(tri[1].data);
				out.push_back(tri[0].data);
			} else
			{
				out.push_back(tri[0].data);
				out.push_back(tri[1].data);
				out.push_back(tri[2].data);
			}
		}
	}

	// 全頂点のバウンディングボックス
	Bounds ComputeBounds(const std::vector<SubMeshData>& subMeshes)
	{
		Bounds bounds;
		for ( const SubMeshData& sub : subMeshes )
		{
			for ( const VertexData& v : sub.vertices )
			{
				if ( !bounds.valid )
				{
					bounds.min = { v.position.x, v.position.y, v.position.z };
					bounds.max = bounds.min;
					bounds.valid = true;
					continue;
				}
				bounds.min.x = std::min(bounds.min.x, v.position.x);
				bounds.min.y = std::min(bounds.min.y, v.position.y);
				bounds.min.z = std::min(bounds.min.z, v.position.z);
				bounds.max.x = std::max(bounds.max.x, v.position.x);
				bounds.max.y = std::max(bounds.max.y, v.position.y);
				bounds.max.z = std::max(bounds.max.z, v.position.z);
			}
		}
		return bounds;
	}
}

//===================================================================
// mtl
//===================================================================
std::unordered_map<std::string, MaterialData> LoadMaterialTemplateFile(
	const std::string& directoryPath, const std::string& filename, std::string* firstName)
{
	std::unordered_map<std::string, MaterialData> materials;
	std::string currentMaterialName;
	std::string line;

	if ( firstName )
	{
		firstName->clear();
	}

	std::ifstream file(directoryPath + "/" + filename);
	if ( !file.is_open() )
	{
		// Release(NDEBUG)ビルドでも見えるよう、必ずログに残す
		Logger::Log("LoadMaterialTemplateFile: failed to open " + directoryPath + "/" + filename + "\n");
		return materials; // 開けなかった場合は空のマップを返す
	}

	while ( std::getline(file, line) )
	{
		std::string identifier;
		std::istringstream s(line);
		s >> identifier;

		if ( identifier == "newmtl" )
		{
			s >> currentMaterialName;
			materials[currentMaterialName] = MaterialData{};
			if ( firstName && firstName->empty() )
			{
				*firstName = currentMaterialName;
			}
		} else if ( identifier == "map_Kd" )
		{ // ディフューズマップのファイル名
			std::string textureFileName;
			s >> textureFileName;
			// newmtlより前にmap_Kdが来ても落ちないよう、名前が空のマテリアルとして扱う
			materials[currentMaterialName].textureFilePath = directoryPath + "/" + textureFileName;
			if ( firstName && firstName->empty() )
			{
				*firstName = currentMaterialName;
			}
		}
	}
	return materials;
}

//===================================================================
// obj
//===================================================================
MultiMeshModelData LoadModel(const std::string& directoryPath, const std::string& filename, const ModelImportSettings& settings)
{
	MultiMeshModelData result;

	std::ifstream file(directoryPath + "/" + filename);
	if ( !file.is_open() )
	{
		// assertだけだとRelease(NDEBUG)ビルドでは無音で握りつぶされてしまうため、必ずログを残す
		Logger::Log("LoadModel: failed to open " + directoryPath + "/" + filename + "\n");
		return result;
	}

	std::vector<Vector4> positions; // 頂点座標
	std::vector<Vector2> texcoords; // テクスチャ座標
	std::vector<Vector3> normals;   // 法線ベクトル

	std::unordered_map<std::string, MaterialData> materials;
	std::string firstMaterialName;  // usemtlが無いときに使うマテリアル

	std::string pendingObjectName;   // 次のサブメッシュに付ける名前
	std::string pendingMaterialName; // 次のサブメッシュに使うマテリアル名
	bool needNewSubMesh = true;      // o / usemtl が来たら次のfで新しいサブメッシュを作る

	std::string line;
	while ( std::getline(file, line) )
	{
		std::string identifier;
		std::istringstream s(line);
		s >> identifier; // 行の先頭の文字列を取得

		if ( identifier == "v" )
		{ // 頂点座標
			Vector4 position;
			s >> position.x >> position.y >> position.z;
			position.w = 1.0f;
			positions.push_back(position);
		} else if ( identifier == "vt" )
		{ // テクスチャ座標
			Vector2 texcoord;
			s >> texcoord.x >> texcoord.y;
			texcoords.push_back(texcoord);
		} else if ( identifier == "vn" )
		{ // 法線ベクトル
			Vector3 normal;
			s >> normal.x >> normal.y >> normal.z;
			normals.push_back(normal);
		} else if ( identifier == "mtllib" )
		{ // マテリアルライブラリのファイル名
			std::string materialFileName;
			s >> materialFileName;
			materials = LoadMaterialTemplateFile(directoryPath, materialFileName, &firstMaterialName);
		} else if ( identifier == "o" )
		{ // オブジェクト名: ここから新しいサブメッシュ
			s >> pendingObjectName;
			needNewSubMesh = true;
		} else if ( identifier == "usemtl" )
		{ // マテリアルの使用宣言: ここから新しいサブメッシュ
			s >> pendingMaterialName;
			needNewSubMesh = true;
		} else if ( identifier == "f" )
		{ // 面情報
			if ( needNewSubMesh )
			{
				// 面が来て初めてサブメッシュを作る(oとusemtlが続けて来ても、空のサブメッシュができない)
				SubMeshData sub;
				sub.name = pendingObjectName;

				auto it = materials.find(pendingMaterialName);
				if ( it == materials.end() && !pendingMaterialName.empty() )
				{
					Logger::Log("LoadModel: material \"" + pendingMaterialName + "\" not found in " + filename + "\n");
				}
				if ( it == materials.end() )
				{
					it = materials.find(firstMaterialName); // 名前が無い・見つからないときは最初のマテリアル
				}
				if ( it != materials.end() )
				{
					sub.material = it->second;
				}
				if ( !settings.textureOverride.empty() )
				{
					sub.material.textureFilePath = settings.textureOverride;
				}

				result.subMeshes.push_back(std::move(sub));
				needNewSubMesh = false;
			}
			AppendFace(s, positions, texcoords, normals, settings, result.subMeshes.back().vertices);
		}
	}

	// 面を持たないままのサブメッシュは無い(fが来た時だけ作るため)が、念のため空を取り除く
	std::erase_if(result.subMeshes, [] (const SubMeshData& sub)
	{
		return sub.vertices.empty();
	});

	if ( result.subMeshes.empty() )
	{
		// ファイルは開けたが、中身が空(あるいは想定した形式でない)だった場合もここで気付けるようにする
		Logger::Log("LoadModel: " + directoryPath + "/" + filename + " opened but contains no faces.\n");
		return result;
	}

	result.bounds = ComputeBounds(result.subMeshes);

	// 中心を原点に移す(boundsも合わせて更新する)
	if ( settings.centerOnOrigin && result.bounds.valid )
	{
		const Vector3 center = result.bounds.Center();
		for ( SubMeshData& sub : result.subMeshes )
		{
			for ( VertexData& v : sub.vertices )
			{
				v.position.x -= center.x;
				v.position.y -= center.y;
				v.position.z -= center.z;
			}
		}
		result.bounds = ComputeBounds(result.subMeshes);
	}

	return result;
}