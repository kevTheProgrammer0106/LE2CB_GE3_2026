#pragma once

#include <d3d12.h>

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "ResourceObject.h"
#include "WorldTransform.h"

class DirectXCommon;

//===================================================================
// モデルの種類(ImGuiのコンボと並びを同期させること)
//===================================================================
enum class ModelNode : int32_t
{
	Sphere = 0,
	Plane,
	Axis,
	Bunny,
	MultiMaterial,
	MultiMesh,
	Suzanne,
	Teapot,
};

inline constexpr const char* kModelNodeItems[] = {
	"Sphere", "Plane", "Axis", "Bunny", "MultiMaterial", "MultiMesh", "Suzanne", "Teapot",
};

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
};

/// <summary>
/// 全ウィンドウで共有する読み取り専用のアセット(モデル・テクスチャ)。
/// 頂点バッファとテクスチャは描画中に書き換えないので、複数のウィンドウから同時に参照してよい
/// </summary>
class AssetCache
{
public:
	/// 全アセットを読み込み、GPUへのアップロード完了まで待つ
	void Initialize(DirectXCommon& dx);

	const ModelResource& GetModel(ModelNode node) const
	{
		return models_.at(node);
	}
	const TextureResource& GetSpriteTexture() const
	{
		return spriteTexture_;
	}
	const TextureResource& GetParticleTexture() const
	{
		return particleTexture_;
	}

	/// 画像を読み込み、GPUへアップロードコマンドを積み、SRVを作って返す。
	/// 中間リソースは dx.FlushUploadCommands() が終わるまで keepAlive に保持しておくこと
	static TextureResource CreateTextureWithSrv(DirectXCommon& dx, const std::string& path, std::vector<ResourceObject>& keepAlive);

private:
	void LoadModels(DirectXCommon& dx, std::vector<ResourceObject>& keepAlive);

	// サブメッシュ1つ分を構築してmodelに追加する
	static void AddSubMesh(
		DirectXCommon& dx,
		ModelResource& model,
		const std::vector<VertexData>& verts,
		const std::string& textureFilePath,
		const char* modelName,
		std::vector<ResourceObject>& keepAlive);

	// 球の頂点生成(インデックス無しの三角形リスト)
	static std::vector<VertexData> GenerateSphereVertices(uint32_t subdivision);

	std::unordered_map<ModelNode, ModelResource> models_;
	TextureResource spriteTexture_;
	TextureResource particleTexture_;
};