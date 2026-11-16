#pragma once

#include <d3d12.h>

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "VirtualisEngine/Graphics/ModelLoader.h"
#include "VirtualisEngine/Graphics/ResourceObject.h"
#include "VirtualisEngine/Math/WorldTransform.h"

class DirectXCommon;

// テクスチャ(リソース + SRVのGPUハンドル)
struct TextureResource
{
	ResourceObject resource;
	D3D12_GPU_DESCRIPTOR_HANDLE srvGpu{};
};

// サブメッシュ1つ分(頂点バッファ + テクスチャ)
struct SubMeshResource
{
	ResourceObject vertexResource;
	D3D12_VERTEX_BUFFER_VIEW vbv{};
	UINT vertexCount = 0;
	TextureResource texture;
};

// モデル1体分
struct ModelResource
{
	std::vector<SubMeshResource> subMeshes;
	// 読み込み設定(scale・offsetなど)を適用した後の範囲。当たり判定やカメラの注視に使える
	Bounds bounds;
};

/// <summary>
/// 全ウィンドウで共有する読み取り専用のアセット(モデル・テクスチャ)。
/// 頂点バッファとテクスチャは描画中に書き換えないので、複数のウィンドウから同時に参照してよい。
///
/// ライブラリ側は「どのファイルを読むか」を知らない。ゲーム側が名前を付けて登録する:
///   assets.BeginLoad();
///   assets.LoadTexture(dx, "player", "resources/player.png");
///   assets.LoadModel(dx, "Teapot", "resources", "teapot.obj");
///   assets.EndLoad(dx);               // まとめてGPUへ転送(ここで完了を待つ)
///   ... assets.GetTexture("player") / assets.GetModel("Teapot")
///
/// Initialize() が作る組み込みテクスチャ(white / checker / particle)は画像ファイルなしで使える。
/// 同じ名前で LoadTexture すれば差し替えられる(例: "particle" に自前の画像を使う)
/// </summary>
class AssetCache
{
public:
	// 組み込みテクスチャの名前
	static constexpr const char* kWhiteTexture = "white";       // 1x1の白。色(Material::color)だけで見せたい図形用
	static constexpr const char* kCheckerTexture = "checker";   // 白と灰色の市松模様。UVの確認用
	static constexpr const char* kParticleTexture = "particle"; // 中心が明るいぼかした円。パーティクルの既定

	/// 組み込みテクスチャを作る。ほかのアセットを読む前に1回呼ぶ(Applicationが呼ぶ)
	void Initialize(DirectXCommon& dx);

	/// 読み込みの開始 / 終了。間に挟んだ Load* の転送がまとめて行われる
	void BeginLoad();
	void EndLoad(DirectXCommon& dx);

	/// 画像を name で登録する。同じパスの画像は1回だけ読み込んで共有する。
	/// 同じ name で呼ぶと差し替える(ただし、前の画像を使っている描画が終わってから呼ぶこと)
	const TextureResource& LoadTexture(DirectXCommon& dx, const std::string& name, const std::string& path);
	/// objファイルを name で登録する。directory は resources などのフォルダ
	const ModelResource& LoadModel(DirectXCommon& dx, const std::string& name, const std::string& directory,
		const std::string& objFile, const ModelImportSettings& settings = {});
	/// 自分で作った頂点(三角形リスト)をモデルとして登録する。texturePath が空なら市松模様(checker)を貼る
	const ModelResource& AddModel(DirectXCommon& dx, const std::string& name, const std::vector<VertexData>& vertices,
		const Bounds& bounds, const std::string& texturePath = "");

	bool HasTexture(const std::string& name) const;
	bool HasModel(const std::string& name) const;
	/// 登録が無い名前は警告を出して白テクスチャを返す(落とさない)
	const TextureResource& GetTexture(const std::string& name) const;
	/// 登録が無い名前は assert で止める(モデルは代わりが無いので)
	const ModelResource& GetModel(const std::string& name) const;
	/// 登録した順のモデル名(ImGuiのコンボ用)
	const std::vector<std::string>& GetModelNames() const
	{
		return modelNames_;
	}

	const TextureResource& GetWhiteTexture() const
	{
		return GetTexture(kWhiteTexture);
	}
	const TextureResource& GetCheckerTexture() const
	{
		return GetTexture(kCheckerTexture);
	}
	const TextureResource& GetParticleTexture() const
	{
		return GetTexture(kParticleTexture);
	}

private:
	/// path の画像を(読み込み済みなら再利用して)返す
	const TextureResource& AcquireTexture(DirectXCommon& dx, const std::string& path);
	/// 画像データ(ScratchImage)からGPUテクスチャとSRVを作る
	TextureResource CreateTextureWithSrv(DirectXCommon& dx, const DirectX::ScratchImage& image);
	/// 組み込みテクスチャ用: 8bit RGBA の画像を作って name で登録する
	void AddGeneratedTexture(DirectXCommon& dx, const std::string& name, uint32_t size,
		const std::vector<uint8_t>& rgba);
	/// サブメッシュ1つを作って model に足す
	void AddSubMesh(DirectXCommon& dx, ModelResource& model, const std::vector<VertexData>& vertices,
		const std::string& texturePath, const std::string& modelName);

	std::unordered_map<std::string, TextureResource> texturesByPath_; // 読み込み済みの画像(同じファイルを二重に読まない)
	std::unordered_map<std::string, TextureResource> textures_;       // 名前 → テクスチャ
	std::unordered_map<std::string, ModelResource> models_;
	std::vector<std::string> modelNames_;

	// アップロード用の中間リソース。EndLoad で転送が終わるまで生かしておく
	std::vector<ResourceObject> keepAlive_;
	bool loading_ = false;
};
