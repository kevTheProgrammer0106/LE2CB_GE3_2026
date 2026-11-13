#include "Engine/Graphics/AssetCache.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstring>
#include <format>

#include "Engine/Graphics/DirectXCommon.h"

//===================================================================
// ロードの流れ
//===================================================================
void AssetCache::Initialize(DirectXCommon& dx)
{
	BeginLoad();

	// 白(1x1)
	AddGeneratedTexture(dx, kWhiteTexture, 1, { 255, 255, 255, 255 });

	// 市松模様(64x64、8pxごとに白と灰色)
	{
		const uint32_t size = 64;
		std::vector<uint8_t> pixels(size_t(size) * size * 4);
		for ( uint32_t y = 0; y < size; ++y )
		{
			for ( uint32_t x = 0; x < size; ++x )
			{
				const uint8_t value = ( ( x / 8 + y / 8 ) % 2 == 0 ) ? 255 : 150;
				uint8_t* p = &pixels[( size_t(y) * size + x ) * 4];
				p[0] = p[1] = p[2] = value;
				p[3] = 255;
			}
		}
		AddGeneratedTexture(dx, kCheckerTexture, size, pixels);
	}

	// ぼかした円(64x64、中心が白で外へ向かって透明になる)
	{
		const uint32_t size = 64;
		std::vector<uint8_t> pixels(size_t(size) * size * 4);
		for ( uint32_t y = 0; y < size; ++y )
		{
			for ( uint32_t x = 0; x < size; ++x )
			{
				const float dx_ = ( static_cast< float >( x ) + 0.5f ) / size * 2.0f - 1.0f;
				const float dy_ = ( static_cast< float >( y ) + 0.5f ) / size * 2.0f - 1.0f;
				const float r = std::sqrt(dx_ * dx_ + dy_ * dy_);
				const float a = std::clamp(1.0f - r, 0.0f, 1.0f);
				uint8_t* p = &pixels[( size_t(y) * size + x ) * 4];
				p[0] = p[1] = p[2] = 255;
				p[3] = static_cast< uint8_t >( a * a * 255.0f ); // 縁をなだらかに消す
			}
		}
		AddGeneratedTexture(dx, kParticleTexture, size, pixels);
	}

	EndLoad(dx);
}

void AssetCache::BeginLoad()
{
	loading_ = true;
}

void AssetCache::EndLoad(DirectXCommon& dx)
{
	if ( !loading_ )
	{
		return;
	}
	dx.FlushUploadCommands(); // GPUへの転送が終わるまで待つ
	keepAlive_.clear();       // 中間リソースを解放してよい
	loading_ = false;
}

//===================================================================
// テクスチャ
//===================================================================
TextureResource AssetCache::CreateTextureWithSrv(DirectXCommon& dx, const DirectX::ScratchImage& image)
{
	assert(loading_ && "Call BeginLoad() before loading assets.");
	const DirectX::TexMetadata& meta = image.GetMetadata();

	TextureResource texture;
	texture.resource = dx.CreateTextureResource(meta);
	keepAlive_.push_back(dx.UploadTextureData(texture.resource.Get(), image));

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

void AssetCache::AddGeneratedTexture(DirectXCommon& dx, const std::string& name, uint32_t size, const std::vector<uint8_t>& rgba)
{
	assert(rgba.size() == size_t(size) * size * 4);

	DirectX::ScratchImage image;
	const HRESULT hr = image.Initialize2D(DXGI_FORMAT_R8G8B8A8_UNORM_SRGB, size, size, 1, 1);
	assert(SUCCEEDED(hr));
	( void ) hr;
	std::memcpy(image.GetImages()[0].pixels, rgba.data(), rgba.size());

	textures_[name] = CreateTextureWithSrv(dx, image);
}

const TextureResource& AssetCache::AcquireTexture(DirectXCommon& dx, const std::string& path)
{
	auto it = texturesByPath_.find(path);
	if ( it == texturesByPath_.end() )
	{
		it = texturesByPath_.emplace(path, CreateTextureWithSrv(dx, DirectXCommon::LoadTexture(path))).first;
	}
	return it->second;
}

const TextureResource& AssetCache::LoadTexture(DirectXCommon& dx, const std::string& name, const std::string& path)
{
	textures_[name] = AcquireTexture(dx, path);
	return textures_[name];
}

bool AssetCache::HasTexture(const std::string& name) const
{
	return textures_.find(name) != textures_.end();
}

const TextureResource& AssetCache::GetTexture(const std::string& name) const
{
	const auto it = textures_.find(name);
	if ( it != textures_.end() )
	{
		return it->second;
	}
	Logger::Log(std::format("[AssetCache] WARNING: texture \"{}\" is not registered. Using the white texture instead.\n", name));
	return textures_.at(kWhiteTexture);
}

//===================================================================
// モデル
//===================================================================
void AssetCache::AddSubMesh(DirectXCommon& dx, ModelResource& model, const std::vector<VertexData>& vertices,
	const std::string& texturePath, const std::string& modelName)
{
	// 頂点が1つも無い(=objファイルが見つからなかった/読めなかった)場合は警告を出してスキップする。
	// assertで即停止しないのは、1回の実行で欠けているモデルをまとめて確認できるようにするため。
	if ( vertices.empty() )
	{
		Logger::Log(std::format("[AssetCache] WARNING: \"{}\" produced an empty vertex list (texture path was \"{}\"). "
			"Check that the corresponding .obj file exists and that the Working Directory is set correctly.\n",
			modelName, texturePath));
		return;
	}

	SubMeshResource sub;

	// 頂点バッファ
	VertexData* mapped = nullptr;
	dx.CreateMappedBuffer(sub.vertexResource, sizeof(VertexData) * vertices.size(), &mapped);
	std::memcpy(mapped, vertices.data(), sizeof(VertexData) * vertices.size());
	sub.vbv.BufferLocation = sub.vertexResource.Get()->GetGPUVirtualAddress();
	sub.vbv.SizeInBytes = UINT(sizeof(VertexData) * vertices.size());
	sub.vbv.StrideInBytes = sizeof(VertexData);
	sub.vertexCount = UINT(vertices.size());

	// テクスチャ(materialが無ければ市松模様で代用)
	sub.texture = texturePath.empty() ? GetCheckerTexture() : AcquireTexture(dx, texturePath);

	model.subMeshes.push_back(std::move(sub));
}

const ModelResource& AssetCache::AddModel(DirectXCommon& dx, const std::string& name, const std::vector<VertexData>& vertices,
	const Bounds& bounds, const std::string& texturePath)
{
	assert(loading_ && "Call BeginLoad() before loading assets.");

	ModelResource model;
	model.bounds = bounds;
	AddSubMesh(dx, model, vertices, texturePath, name);

	if ( models_.find(name) == models_.end() )
	{
		modelNames_.push_back(name);
	}
	models_[name] = std::move(model);
	return models_[name];
}

const ModelResource& AssetCache::LoadModel(DirectXCommon& dx, const std::string& name, const std::string& directory,
	const std::string& objFile, const ModelImportSettings& settings)
{
	assert(loading_ && "Call BeginLoad() before loading assets.");

	const MultiMeshModelData data = ::LoadModel(directory, objFile, settings);

	ModelResource model;
	model.bounds = data.bounds;
	for ( const SubMeshData& sub : data.subMeshes )
	{
		AddSubMesh(dx, model, sub.vertices, sub.material.textureFilePath, name);
	}

	if ( models_.find(name) == models_.end() )
	{
		modelNames_.push_back(name);
	}
	models_[name] = std::move(model);
	return models_[name];
}

bool AssetCache::HasModel(const std::string& name) const
{
	return models_.find(name) != models_.end();
}

const ModelResource& AssetCache::GetModel(const std::string& name) const
{
	const auto it = models_.find(name);
	assert(it != models_.end() && "Model is not registered. Call AssetCache::LoadModel first.");
	return it->second;
}
