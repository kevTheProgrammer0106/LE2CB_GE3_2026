#include "AssetCache.h"

#include <cassert>
#include <cstring>
#include <format>

#include "DirectXCommon.h"

namespace
{
	constexpr uint32_t kSphereSubdivision = 16; // 球の分割数
	constexpr float kPiConst = 3.14159265358979323846f;
}

void AssetCache::Initialize(DirectXCommon& dx)
{
	// アップロード用の中間リソースは、GPUへの転送が終わるまで生かしておく必要がある
	std::vector<ResourceObject> keepAlive;

	spriteTexture_ = CreateTextureWithSrv(dx, "resources/uvChecker.png", keepAlive);
	particleTexture_ = CreateTextureWithSrv(dx, "resources/particle.jpg", keepAlive);
	whiteTexture_ = CreateTextureWithSrv(dx, "resources/white1x1.png", keepAlive);

	LoadModels(dx, keepAlive);

	dx.FlushUploadCommands();
} // ここで中間リソースが解放される

//===================================================================
// テクスチャ
//===================================================================
TextureResource AssetCache::CreateTextureWithSrv(DirectXCommon& dx, const std::string& path, std::vector<ResourceObject>& keepAlive)
{
	DirectX::ScratchImage mip = DirectXCommon::LoadTexture(path);
	const DirectX::TexMetadata& meta = mip.GetMetadata();

	TextureResource texture;
	texture.resource = dx.CreateTextureResource(meta);
	keepAlive.push_back(dx.UploadTextureData(texture.resource.Get(), mip));

	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
	srvDesc.Format = meta.format;
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	srvDesc.Texture2D.MipLevels = UINT(meta.mipLevels);

	const uint32_t srvIndex = dx.AllocateSrvIndex();
	dx.GetDevice()->CreateShaderResourceView(texture.resource.Get(), &srvDesc, dx.GetSRVCPUDescriptorHandle(srvIndex));
	texture.srvGpu = dx.GetSRVGPUDescriptorHandle(srvIndex);
	return texture;
}

//===================================================================
// モデル
//===================================================================
std::vector<VertexData> AssetCache::GenerateSphereVertices(uint32_t subdivision)
{
	std::vector<VertexData> vertices;
	vertices.resize(size_t(subdivision) * subdivision * 6);

	const float kLonEvery = ( 2.0f * kPiConst ) / float(subdivision); // 経度分割1つ分の角度
	const float kLatEvery = kPiConst / float(subdivision);            // 緯度分割1つ分の角度
	const float step = 1.0f / float(subdivision);

	for ( uint32_t latIndex = 0; latIndex < subdivision; ++latIndex )
	{
		float lat = -kPiConst / 2.0f + kLatEvery * latIndex; // 現在の緯度
		for ( uint32_t lonIndex = 0; lonIndex < subdivision; ++lonIndex )
		{
			float u = float(lonIndex) / float(subdivision);
			float v = 1.0f - float(latIndex) / float(subdivision);
			float lon = lonIndex * kLonEvery; // 現在の経度
			uint32_t startIndex = ( latIndex * subdivision + lonIndex ) * 6;

			VertexData a{}, b{}, c{}, d{};

			a.position = { cosf(lat) * cosf(lon), sinf(lat), cosf(lat) * sinf(lon), 1.0f };
			a.texcoord = { u, v };
			a.normal = { a.position.x, a.position.y, a.position.z };

			b.position = { cosf(lat + kLatEvery) * cosf(lon), sinf(lat + kLatEvery), cosf(lat + kLatEvery) * sinf(lon), 1.0f };
			b.texcoord = { u, v - step };
			b.normal = { b.position.x, b.position.y, b.position.z };

			c.position = { cosf(lat) * cosf(lon + kLonEvery), sinf(lat), cosf(lat) * sinf(lon + kLonEvery), 1.0f };
			c.texcoord = { u + step, v };
			c.normal = { c.position.x, c.position.y, c.position.z };

			d.position = { cosf(lat + kLatEvery) * cosf(lon + kLonEvery), sinf(lat + kLatEvery), cosf(lat + kLatEvery) * sinf(lon + kLonEvery), 1.0f };
			d.texcoord = { u + step, v - step };
			d.normal = { d.position.x, d.position.y, d.position.z };

			// 三角形1枚目 ABC
			vertices[startIndex + 0] = a;
			vertices[startIndex + 1] = b;
			vertices[startIndex + 2] = c;
			// 三角形2枚目 BDC
			vertices[startIndex + 3] = b;
			vertices[startIndex + 4] = d;
			vertices[startIndex + 5] = c;
		}
	}
	return vertices;
}

void AssetCache::AddSubMesh(
	DirectXCommon& dx,
	ModelResource& model,
	const std::vector<VertexData>& verts,
	const std::string& textureFilePath,
	const char* modelName,
	std::vector<ResourceObject>& keepAlive)
{
	// 頂点が1つも無い(=objファイルが見つからなかった/読めなかった)場合は警告を出してスキップする。
	// assertで即停止しないのは、1回の実行で欠けているモデルをまとめて確認できるようにするため。
	if ( verts.empty() )
	{
		Logger::Log(std::format("[AddSubMesh] WARNING: \"{}\" produced an empty vertex list (texture path was \"{}\"). "
			"Check that the corresponding .obj file exists under resources/ and that the Working Directory is set correctly.\n",
			modelName, textureFilePath));
		return;
	}

	SubMeshResource sub;

	// 頂点バッファ
	sub.vertexResource = dx.CreateBufferResource(sizeof(VertexData) * verts.size());
	assert(sub.vertexResource.Get() != nullptr && "CreateBufferResource failed for vertex buffer");

	VertexData* mapped = nullptr;
	HRESULT hrMap = sub.vertexResource.Get()->Map(0, nullptr, reinterpret_cast< void** >( &mapped ));
	assert(SUCCEEDED(hrMap) && mapped != nullptr && "Map() failed on vertex buffer");
	( void ) hrMap;

	std::memcpy(mapped, verts.data(), sizeof(VertexData) * verts.size());
	sub.vbv.BufferLocation = sub.vertexResource.Get()->GetGPUVirtualAddress();
	sub.vbv.SizeInBytes = UINT(sizeof(VertexData) * verts.size());
	sub.vbv.StrideInBytes = sizeof(VertexData);
	sub.vertexCount = UINT(verts.size());

	// テクスチャ(materialが無ければuvCheckerで代用)
	const std::string texPath = textureFilePath.empty() ? "resources/uvChecker.png" : textureFilePath;
	sub.texture = CreateTextureWithSrv(dx, texPath, keepAlive);

	model.subMeshes.push_back(std::move(sub));
}

void AssetCache::LoadModels(DirectXCommon& dx, std::vector<ResourceObject>& keepAlive)
{
	// Sphere(プロシージャル生成、テクスチャはuvCheckerを流用)
	{
		ModelResource model;
		AddSubMesh(dx, model, GenerateSphereVertices(kSphereSubdivision), "", "Sphere", keepAlive);
		model.bounds = { { -1.0f, -1.0f, -1.0f }, { 1.0f, 1.0f, 1.0f }, true }; // 半径1
		models_[ModelNode::Sphere] = std::move(model);
	}

	// objモデルの一覧。モデルを増やすときは、ここに1項目足す(+ ModelNode と kModelNodeItems に同じ順で足す)。
	// settings には読み込み時の値(倍率・オフセット・反転・テクスチャ上書き)を書ける
	struct ModelCatalogEntry
	{
		ModelNode node;
		const char* name;
		const char* objFile;
		ModelImportSettings settings;
	};

	// textureOverrideの理由:
	//  - Suzanne: 常にwhite1x1を貼る。
	//  - Teapot : teapot.objはvt(テクスチャ座標)を持たず全頂点のUVが(0,0)になる。
	//             絵柄付きテクスチャだと単色の塊に見えてしまうので、white1x1にしてHalf-Lambertの陰影で形を見せる。
	ModelImportSettings whiteTexture;
	whiteTexture.textureOverride = "resources/white1x1.png";

	const std::vector<ModelCatalogEntry> catalog = {
		{ ModelNode::Plane, "Plane", "plane.obj", {} },
		{ ModelNode::Axis, "Axis", "axis.obj", {} },
		{ ModelNode::Bunny, "Bunny", "bunny.obj", {} },
		{ ModelNode::Suzanne, "Suzanne", "suzanne.obj", whiteTexture },
		{ ModelNode::Teapot, "Teapot", "teapot.obj", whiteTexture },
		{ ModelNode::MultiMaterial, "MultiMaterial", "multiMaterial.obj", {} },
		{ ModelNode::MultiMesh, "MultiMesh", "multiMesh.obj", {} },
	};

	for ( const ModelCatalogEntry& entry : catalog )
	{
		const MultiMeshModelData data = LoadModel("resources", entry.objFile, entry.settings);

		ModelResource model;
		model.bounds = data.bounds;
		for ( const SubMeshData& sub : data.subMeshes )
		{
			AddSubMesh(dx, model, sub.vertices, sub.material.textureFilePath, entry.name, keepAlive);
		}
		models_[entry.node] = std::move(model);
	}
}