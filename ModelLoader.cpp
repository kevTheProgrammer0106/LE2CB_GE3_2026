#include "ModelLoader.h"
#include <fstream>
#include <sstream>
#include <cassert>

// 前方宣言(main.cppにあるLog関数を使う)
void Log(const std::string& message);

namespace
{
	// objの"f"行の1トークン("position/texcoord/normal")を分解して取得する。
	// - texcoordやnormalが省略されている("v//vn"や"v"だけ)場合は0(未指定)を返す
	// - 負のindex(相対indexで「最後に追加された要素から数える」形式)にも対応する
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

	// "f"行の3頂点分を、position/texcoord/normal配列から安全に組み立てる。
	// vt/vnが省略されている場合や、負の相対indexが使われている場合でも
	// クラッシュしたり無効なメモリを読んだりしないようにする。
	void BuildTriangleFromFaceLine(
		std::istringstream& s,
		const std::vector<Vector4>& positions,
		const std::vector<Vector2>& texcoords,
		const std::vector<Vector3>& normals,
		VertexData outTriangle[3])
	{
		bool normalMissing[3] = { false, false, false };

		for ( int32_t faceVertex = 0; faceVertex < 3; ++faceVertex )
		{
			std::string vertexDefinition;
			s >> vertexDefinition; // 頂点情報を読み込む

			RawFaceIndices raw = ParseFaceVertexToken(vertexDefinition);

			int posIndex = ResolveIndex(raw.position, positions.size());
			int uvIndex = ResolveIndex(raw.texcoord, texcoords.size());
			int nrmIndex = ResolveIndex(raw.normal, normals.size());

			// position: 必須。範囲外なら安全のため原点にしておく
			Vector4 position = { 0.0f, 0.0f, 0.0f, 1.0f };
			if ( posIndex >= 0 && posIndex < static_cast< int >(positions.size()) )
			{
				position = positions[posIndex];
			}
			position.x *= -1.0f;

			// texcoord: 省略されていたら(0,0)を使う
			Vector2 texcoord = { 0.0f, 0.0f };
			if ( uvIndex >= 0 && uvIndex < static_cast< int >(texcoords.size()) )
			{
				texcoord = texcoords[uvIndex];
				texcoord.y = 1.0f - texcoord.y; // DirectXは左上が原点なので、V座標を反転させる
			}

			// normal: 省略されていたら後で面法線から計算するのでここでは印だけつけておく
			Vector3 normal = { 0.0f, 1.0f, 0.0f };
			if ( nrmIndex >= 0 && nrmIndex < static_cast< int >(normals.size()) )
			{
				normal = normals[nrmIndex];
				normal.x *= -1.0f;
			} else
			{
				normalMissing[faceVertex] = true;
			}

			outTriangle[faceVertex] = { position, texcoord, normal };
		}

		// vnが1頂点でも無かった場合は、面法線を計算して欠けている頂点にだけ適用する
		// (フラットシェーディング相当。lighting用のnormalがおかしくならないようにするため)
		if ( normalMissing[0] || normalMissing[1] || normalMissing[2] )
		{
			Vector3 faceNormal = CalculateFaceNormal(outTriangle[0].position, outTriangle[1].position, outTriangle[2].position);
			for ( int32_t i = 0; i < 3; ++i )
			{
				if ( normalMissing[i] )
				{
					outTriangle[i].normal = faceNormal;
				}
			}
		}
	}
}

MaterialData LoadMaterialTemplateFile(const std::string& directoryPath, const std::string& filename)
{
	MaterialData materialData; // 構築するMaterialData
	std::string line; // ファイルから読み込んだ1行分の文字列を格納する変数
	std::ifstream file(directoryPath + "/" + filename); // ファイルを開く
	if ( !file.is_open() )
	{
		// assert(file.is_open())だけだとRelease(NDEBUG)ビルドでは無音で握りつぶされてしまうため、
		// ここで必ずログに残すようにする
		Log("LoadMaterialTemplateFile: failed to open " + directoryPath + "/" + filename + "\n");
		assert(false);
		return materialData; // 開けなかった場合は空のMaterialDataを返す
	}
	while ( std::getline(file, line) )
	{
		std::string identifier;
		std::istringstream s(line);
		s >> identifier; // 1行目の先頭の文字列を取得

		if ( identifier == "map_Kd" )
		{ // ディフューズマップのファイル名
			std::string textureFileName;
			s >> textureFileName;
			//連結してファイルパスにする
			materialData.textureFilePath = directoryPath + "/" + textureFileName;
		}
	} // Whileの終了
	return materialData; // 構築したMaterialDataを返す
}
std::unordered_map<std::string, MaterialData> LoadMaterialTemplateFileMulti(const std::string& directoryPath, const std::string& filename)
{
	std::unordered_map<std::string, MaterialData> materials;
	std::string currentMaterialName;
	std::string line;
	std::ifstream file(directoryPath + "/" + filename);
	if ( !file.is_open() )
	{
		Log("LoadMaterialTemplateFileMulti: failed to open " + directoryPath + "/" + filename + "\n");
		assert(false);
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
		} else if ( identifier == "map_Kd" )
		{
			std::string textureFileName;
			s >> textureFileName;
			materials[currentMaterialName].textureFilePath = directoryPath + "/" + textureFileName;
		}
	}
	return materials;
}

ModelData LoadObjFile(const std::string& directoryPath, const std::string& filename)
{
	ModelData modelData; // 構築するModelData
	std::vector<Vector4> positions; // 頂点座標
	std::vector<Vector2> texcoords; // テクスチャ座標
	std::vector<Vector3> normals; // 法線ベクトル
	std::string line; // ファイルから読み込んだ1行分の文字列を格納する変数

	std::ifstream file(directoryPath + "/" + filename); // ファイルを開く
	if ( !file.is_open() )
	{
		// assert(file.is_open())だけだとRelease(NDEBUG)ビルドでは無音で握りつぶされてしまい、
		// modelData.verticesが空のまま返ってバッファ生成やMap()の失敗につながるため、必ずログを残す
		Log("LoadObjFile: failed to open " + directoryPath + "/" + filename + "\n");
		assert(false);
		return modelData; // 開けなかった場合は空のModelDataを返す
	}

	while ( std::getline(file, line) )
	{
		std::string identifier;
		std::istringstream s(line);
		s >> identifier; // 1行目の先頭の文字列を取得

		// indentifierの値によって処理を分岐

		if ( identifier == "v" )
		{ // 頂点座標
			Vector4 position;
			s >> position.x >> position.y >> position.z; // x,y,z座標を読み込む
			position.w = 1.0f; // w座標は1.0fにする
			positions.push_back(position); // positionsに追加
		} else if ( identifier == "vt" )
		{ // テクスチャ座標
			Vector2 texcoord;
			s >> texcoord.x >> texcoord.y; // u,v座標を読み込む
			texcoords.push_back(texcoord); // texcoordsに追加
		} else if ( identifier == "vn" )
		{ // 法線ベクトル
			Vector3 normal;
			s >> normal.x >> normal.y >> normal.z; // x,y,z座標を読み込む
			normals.push_back(normal); // normalsに追加
		} else if ( identifier == "f" )
		{ // 面情報
			VertexData triangle[3]; // 1つの面は3つの頂点で構成されるので、3つ分の頂点データを格納する配列
			// vt/vnが省略されている場合や負の相対indexが使われている場合でも
			// クラッシュ・不正な法線にならないよう、共通のヘルパーで安全に組み立てる
			BuildTriangleFromFaceLine(s, positions, texcoords, normals, triangle);
			//頂点を逆順で登録することで、回り順を逆にする
			modelData.vertices.push_back(triangle[2]);
			modelData.vertices.push_back(triangle[1]);
			modelData.vertices.push_back(triangle[0]);
		} else if ( identifier == "mtllib" )
		{ // マテリアルライブラリのファイル名
			std::string materialFileName;
			s >> materialFileName; // マテリアルライブラリのファイル名を取得
			//マテリアルライブラリのファイルを読み込む
			modelData.material = LoadMaterialTemplateFile(directoryPath, materialFileName);
		} else if ( identifier == "Kd" )
		{
			std::string kDColor;
			s >> kDColor; // Kdの値を取得
		} else if ( identifier == "o" )
		{ // オブジェクト名
			std::string objectName;
			s >> objectName; // オブジェクト名を取得
			// 今回はオブジェクト名は使用しないので、特に処理は行わない
		} else if ( identifier == "usemtl" )
		{ // マテリアルの使用宣言
			std::string materialName;
			s >> materialName; // マテリアル名を取得
			// 今回はマテリアル名は使用しないので、特に処理は行わない
		}
	} // Whileの終了

	if ( modelData.vertices.empty() )
	{
		// ファイルは開けたが、中身が空(あるいは想定した形式でない)だった場合もここで気付けるようにする
		Log("LoadObjFile: " + directoryPath + "/" + filename + " opened but contains no vertices.\n");
	}

	return modelData; // 構築したModelDataを返す
}
MultiMeshModelData LoadObjFileMulti(const std::string& directoryPath, const std::string& filename)
{
	MultiMeshModelData result;
	std::unordered_map<std::string, MaterialData> materials;
	std::vector<Vector4> positions;
	std::vector<Vector2> texcoords;
	std::vector<Vector3> normals;
	std::string line;
	std::ifstream file(directoryPath + "/" + filename);
	if ( !file.is_open() )
	{
		Log("LoadObjFileMulti: failed to open " + directoryPath + "/" + filename + "\n");
		assert(false);
		return result; // 開けなかった場合は空のMultiMeshModelDataを返す
	}

	SubMeshData* currentSubMesh = nullptr;
	auto StartNewSubMesh = [&] (const std::string& materialName)
	{
		result.subMeshes.push_back(SubMeshData{});
		currentSubMesh = &result.subMeshes.back();
		if ( !materialName.empty() && materials.contains(materialName) )
		{
			currentSubMesh->material = materials[materialName];
		}
	};

	while ( std::getline(file, line) )
	{
		std::string identifier;
		std::istringstream s(line);
		s >> identifier;

		if ( identifier == "v" )
		{
			Vector4 position; s >> position.x >> position.y >> position.z; position.w = 1.0f;
			positions.push_back(position);
		} else if ( identifier == "vt" )
		{
			Vector2 texcoord; s >> texcoord.x >> texcoord.y;
			texcoords.push_back(texcoord);
		} else if ( identifier == "vn" )
		{
			Vector3 normal; s >> normal.x >> normal.y >> normal.z;
			normals.push_back(normal);
		} else if ( identifier == "mtllib" )
		{
			std::string materialFileName; s >> materialFileName;
			materials = LoadMaterialTemplateFileMulti(directoryPath, materialFileName);
		} else if ( identifier == "o" )
		{
			std::string objectName; s >> objectName;
			StartNewSubMesh(""); // オブジェクトが変わったら新しいサブメッシュ
		} else if ( identifier == "usemtl" )
		{
			std::string materialName; s >> materialName;
			StartNewSubMesh(materialName); // マテリアルが変わっても新しいサブメッシュ
		} else if ( identifier == "f" )
		{
			if ( !currentSubMesh ) StartNewSubMesh(""); // 保険:oやusemtlより前にfが来た場合
			VertexData triangle[3];
			BuildTriangleFromFaceLine(s, positions, texcoords, normals, triangle);
			currentSubMesh->vertices.push_back(triangle[2]);
			currentSubMesh->vertices.push_back(triangle[1]);
			currentSubMesh->vertices.push_back(triangle[0]);
		}
	}

	if ( result.subMeshes.empty() )
	{
		Log("LoadObjFileMulti: " + directoryPath + "/" + filename + " opened but produced no submeshes.\n");
	}

	return result;
}