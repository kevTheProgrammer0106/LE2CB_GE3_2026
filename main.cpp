#include <Windows.h>

#include "WinApp.h"
#include "DirectXCommon.h"
#include "WindowRenderer.h"
#include "D3DResourceLeakChecker.h"

#include "WorldTransform.h"
#include "ResourceObject.h"
#include "Particle.h"
#include "SoundManager.h"
#include "InputManager.h"
#include "DebugCamera.h"
#include "Camera.h"
#include "ModelLoader.h"

#include "GameScene.h"

#include <array>
#include <cstring>
#include <unordered_map>
#include <vector>

#ifdef USE_IMGUI
#include "externals/imgui/imgui.h"
#endif

using Microsoft::WRL::ComPtr;

//===================================================================
// 型・定数
//===================================================================
#pragma region 型・定数
enum class DrawMode
{
	Model,
	Particles
};

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

static const char* kModelNodeItems[] = {
	"Sphere", "Plane", "Axis", "Bunny", "MultiMaterial", "MultiMesh", "Suzanne", "Teapot",
};

// ImGuiのコンボ用。ParticleBlendModeの並びと同期させること
static const char* kBlendModeItems = "None\0Normal\0Add\0Subtract\0Multiply\0Screen\0Exclusion\0";

static constexpr int kBlendModeCount = static_cast< int >( ParticleBlendMode::Count_ );
static constexpr uint32_t kSphereSubdivision = 16; // 球の分割数
static constexpr float kPiConst = 3.14159265358979323846f;
static constexpr uint32_t kNumInstance = 10;       // インスタンシング描画の数
static constexpr float kDeltaTime = 1.0f / 60.0f;

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

// 1つの描画対象(モデル種別 + トランスフォーム + マテリアル + WVP)
struct RenderObject
{
	ModelNode modelNode = ModelNode::Sphere;
	TransformData transform{ { 1.0f, 1.0f, 1.0f, 0.0f }, { 0.0f, 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f, 0.0f } };
	TransformData uvTransform{ { 1.0f, 1.0f, 1.0f, 0.0f }, { 0.0f, 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f, 0.0f } };

	ResourceObject materialResource;
	Material* materialData = nullptr;

	ResourceObject wvpResource;
	TransformationMatrix* wvpData = nullptr;
};

// スプライト(画面左上のクワッド)
struct SpriteObject
{
	ResourceObject vertexResource;
	ResourceObject indexResource;
	ResourceObject materialResource;
	ResourceObject transformResource;
	D3D12_VERTEX_BUFFER_VIEW vbv{};
	D3D12_INDEX_BUFFER_VIEW ibv{};
	Material* materialData = nullptr;
	TransformationMatrix* transformData = nullptr;
	TransformData transform{ { 1.0f, 1.0f, 1.0f, 0.0f }, { 0.0f, 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f, 0.0f } };
	TransformData uvTransform{ { 1.0f, 1.0f, 1.0f, 0.0f }, { 0.0f, 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f, 0.0f } };
	D3D12_GPU_DESCRIPTOR_HANDLE textureSrv{};
	bool enabled = false;
};

// インスタンシング描画(StructuredBufferで行列を渡す)
struct InstancingObject
{
	ResourceObject vertexResource;
	D3D12_VERTEX_BUFFER_VIEW vbv{};
	UINT vertexCount = 0;
	ResourceObject instanceBuffer;
	TransformationMatrix* data = nullptr;
	D3D12_GPU_DESCRIPTOR_HANDLE srvGpu{};
	std::array<TransformData, kNumInstance> transforms{};
	bool enabled = true;
};

// ルートシグネチャ + PSO一式
struct PipelineSet
{
	ComPtr<ID3D12RootSignature> rootSignature;
	ComPtr<ID3D12PipelineState> sprite;
	ComPtr<ID3D12PipelineState> model[kBlendModeCount];
	ComPtr<ID3D12PipelineState> particle[kBlendModeCount];
	ID3D12PipelineState* particleRaw[kBlendModeCount] = {}; // ParticleEmitter::Draw に渡す用
};

// シーン全体の状態
struct Scene
{
	// カメラ
	Camera camera;
	DebugCamera debugCamera;
	bool useDebugCamera = false;
	Matrix4x4 viewMatrix = MakeIdentityMatrix();
	Matrix4x4 projectionMatrix = MakeIdentityMatrix();
	Matrix4x4 viewProjection = MakeIdentityMatrix();

	// ライト
	ResourceObject directionalLightResource;
	DirectionalLight* directionalLight = nullptr;

	// モデル
	std::unordered_map<ModelNode, ModelResource> modelResources;
	RenderObject object1;
	RenderObject object2;
	ParticleBlendMode modelBlendMode = ParticleBlendMode::None;

	// その他の描画物
	TextureResource spriteTexture;
	TextureResource particleTexture;
	SpriteObject sprite;
	InstancingObject instancing;
	ParticleEmitter emitter;

	DrawMode drawMode = DrawMode::Model;
};
#pragma endregion

//===================================================================
// クラッシュダンプ
//===================================================================
static LONG WINAPI ExportDump(EXCEPTION_POINTERS* exception)
{
	// 時刻を取得して、時刻を名前に入れたファイルを作成。Dumpsディレクトリ以下に出力
	SYSTEMTIME time;
	GetLocalTime(&time);
	wchar_t filePath[MAX_PATH] = { 0 };
	CreateDirectory(L"./Dumps", nullptr);
	StringCchPrintfW(filePath, MAX_PATH, L"./Dumps/CrashDump_%04d%02d%02d_%02d%02d%02d.dmp",
		time.wYear, time.wMonth, time.wDay, time.wHour, time.wMinute, time.wSecond);
	HANDLE dumpFileHandle = CreateFile(filePath, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_WRITE | FILE_SHARE_READ, 0, CREATE_ALWAYS, 0, 0);
	// processId(このexeのId)とクラッシュ(例外)の発生したthreadを取得
	DWORD processId = GetCurrentProcessId();
	DWORD threadId = GetCurrentThreadId();
	// 設定情報を入力
	MINIDUMP_EXCEPTION_INFORMATION minidumpInformation{ 0 };
	minidumpInformation.ThreadId = threadId;
	minidumpInformation.ExceptionPointers = exception;
	minidumpInformation.ClientPointers = TRUE;
	// Dumpを出力。MiniDumpNormalは最低限の情報を出力するフラグ
	MiniDumpWriteDump(GetCurrentProcess(), processId, dumpFileHandle, MiniDumpNormal, &minidumpInformation, nullptr, nullptr);
	// 他に関連づけられているSEH例外ハンドラがあれば実行。通常はプロセスを終了する
	return EXCEPTION_EXECUTE_HANDLER;
}



//===================================================================
// テクスチャ
//===================================================================
// 画像を読み込み、GPUへアップロードし、SRVを作って返す。
// 中間リソースはアップロード完了(FlushCommands)まで keepAlive に保持しておく
static TextureResource CreateTextureWithSrv(DirectXCommon& dx, const std::string& path, std::vector<ResourceObject>& keepAlive)
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
#pragma region モデル
// 球の頂点生成(インデックス無しの三角形リスト)
static std::vector<VertexData> GenerateSphereVertices(uint32_t subdivision)
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

// サブメッシュ1つ分を構築してmodelに追加する
static void AddSubMesh(
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

// 全モデルを読み込んでキャッシュに詰める。
static void LoadAllModelResources(
	DirectXCommon& dx,
	std::unordered_map<ModelNode, ModelResource>& modelResources,
	std::vector<ResourceObject>& keepAlive)
{
	// Sphere(プロシージャル生成、テクスチャはuvCheckerを流用)
	{
		ModelResource model;
		AddSubMesh(dx, model, GenerateSphereVertices(kSphereSubdivision), "", "Sphere", keepAlive);
		modelResources[ModelNode::Sphere] = std::move(model);
	}

	// 単一メッシュのobj。textureOverrideがあればmtlのテクスチャは無視してそれを貼る。
	//  - Suzanne: 常にwhite1x1を貼る。
	//  - Teapot : teapot.objはvt(テクスチャ座標)を持たず全頂点のUVが(0,0)になる。
	//             絵柄付きテクスチャだと単色の塊に見えてしまうので、white1x1にしてHalf-Lambertの陰影で形を見せる。
	struct SingleModelDesc
	{
		ModelNode node;
		const char* name;
		const char* objFile;
		const char* textureOverride; // nullptrならmtlのテクスチャを使う
	};
	const SingleModelDesc singleModels[] = {
		{ ModelNode::Plane, "Plane", "plane.obj", nullptr },
		{ ModelNode::Axis, "Axis", "axis.obj", nullptr },
		{ ModelNode::Bunny, "Bunny", "bunny.obj", nullptr },
		{ ModelNode::Suzanne, "Suzanne", "suzanne.obj", "resources/white1x1.png" },
		{ ModelNode::Teapot, "Teapot", "teapot.obj", "resources/white1x1.png" },
	};
	for ( const auto& desc : singleModels )
	{
		ModelData md = LoadObjFile("resources", desc.objFile);
		const std::string texture = desc.textureOverride ? desc.textureOverride : md.material.textureFilePath;
		ModelResource model;
		AddSubMesh(dx, model, md.vertices, texture, desc.name, keepAlive);
		modelResources[desc.node] = std::move(model);
	}

	// 複数サブメッシュのobj
	struct MultiModelDesc
	{
		ModelNode node;
		const char* name;
		const char* objFile;
	};
	const MultiModelDesc multiModels[] = {
		{ ModelNode::MultiMaterial, "MultiMaterial", "multiMaterial.obj" },
		{ ModelNode::MultiMesh, "MultiMesh", "multiMesh.obj" },
	};
	for ( const auto& desc : multiModels )
	{
		MultiMeshModelData md = LoadObjFileMulti("resources", desc.objFile);
		ModelResource model;
		for ( auto& sm : md.subMeshes )
		{
			AddSubMesh(dx, model, sm.vertices, sm.material.textureFilePath, desc.name, keepAlive);
		}
		modelResources[desc.node] = std::move(model);
	}
}
#pragma endregion

//===================================================================
// RenderObject
//===================================================================
#pragma region RenderObject
static void InitRenderObject(DirectXCommon& dx, RenderObject& obj)
{
	obj.materialResource = dx.CreateBufferResource(sizeof(Material));
	obj.materialResource.Get()->Map(0, nullptr, reinterpret_cast< void** >( &obj.materialData ));
	*obj.materialData = {};
	obj.materialData->color = { 1.0f, 1.0f, 1.0f, 1.0f };
	obj.materialData->lightingMode = LightingMode::HalfLambert;
	obj.materialData->uvTransform = MakeIdentityMatrix();

	obj.wvpResource = dx.CreateBufferResource(sizeof(TransformationMatrix));
	obj.wvpResource.Get()->Map(0, nullptr, reinterpret_cast< void** >( &obj.wvpData ));
	obj.wvpData->WVP = MakeIdentityMatrix();
	obj.wvpData->World = MakeIdentityMatrix();
}

// カメラで作った共通のviewProjectionを使ってオブジェクトのWVPを更新する
static void UpdateRenderObject(RenderObject& obj, const Matrix4x4& viewProjection)
{
	Matrix4x4 world = MakeAffineMatrix(obj.transform.scale, obj.transform.rotate, obj.transform.translate);
	obj.wvpData->WVP = Multiply(world, viewProjection);
	obj.wvpData->World = world;

	Matrix4x4 uv = MakeScaleMatrix(obj.uvTransform.scale);
	uv = Multiply(uv, MakeRotateZMatrix(obj.uvTransform.rotate.z));
	uv = Multiply(uv, MakeTranslateMatrix(obj.uvTransform.translate));
	obj.materialData->uvTransform = uv;
}

// RootSignature/PSO/DescriptorHeap/Viewportは呼び出し側で設定済みの前提
static void DrawRenderObject(
	ID3D12GraphicsCommandList* commandList,
	const RenderObject& obj,
	const std::unordered_map<ModelNode, ModelResource>& modelResources,
	ID3D12Resource* directionalLightResource)
{
	commandList->SetGraphicsRootConstantBufferView(0, obj.materialResource.Get()->GetGPUVirtualAddress());
	commandList->SetGraphicsRootConstantBufferView(1, obj.wvpResource.Get()->GetGPUVirtualAddress());
	commandList->SetGraphicsRootConstantBufferView(3, directionalLightResource->GetGPUVirtualAddress());

	const ModelResource& model = modelResources.at(obj.modelNode);
	for ( const auto& sub : model.subMeshes )
	{
		commandList->IASetVertexBuffers(0, 1, &sub.vbv);
		commandList->SetGraphicsRootDescriptorTable(2, sub.texture.srvGpu);
		commandList->DrawInstanced(sub.vertexCount, 1, 0, 0);
	}
}
#pragma endregion

//===================================================================
// スプライト / インスタンシング
//===================================================================
#pragma region スプライト・インスタンシング
static void InitSprite(DirectXCommon& dx, SpriteObject& sprite, D3D12_GPU_DESCRIPTOR_HANDLE textureSrv)
{
	constexpr UINT kVertexCount = 4;
	constexpr UINT kIndexCount = 6;

	sprite.textureSrv = textureSrv;

	// マテリアル(スプライトは基本的にLightingの影響を受けないが、元の挙動に合わせてHalfLambertのまま)
	sprite.materialResource = dx.CreateBufferResource(sizeof(Material));
	sprite.materialResource.Get()->Map(0, nullptr, reinterpret_cast< void** >( &sprite.materialData ));
	*sprite.materialData = {};
	sprite.materialData->color = { 1.0f, 1.0f, 1.0f, 1.0f };
	sprite.materialData->lightingMode = LightingMode::HalfLambert;
	sprite.materialData->uvTransform = MakeIdentityMatrix();

	// 頂点バッファ(4頂点・重複なし。インデックスバッファで三角形2枚を組む)
	sprite.vertexResource = dx.CreateBufferResource(sizeof(VertexData) * kVertexCount);
	sprite.vbv.BufferLocation = sprite.vertexResource.Get()->GetGPUVirtualAddress();
	sprite.vbv.SizeInBytes = sizeof(VertexData) * kVertexCount;
	sprite.vbv.StrideInBytes = sizeof(VertexData);

	VertexData* vertices = nullptr;
	sprite.vertexResource.Get()->Map(0, nullptr, reinterpret_cast< void** >( &vertices ));
	vertices[0] = { { 0.0f, 360.0f, 0.0f, 1.0f }, { 0.0f, 1.0f }, { 0.0f, 0.0f, -1.0f } }; // 左下
	vertices[1] = { { 0.0f, 0.0f, 0.0f, 1.0f }, { 0.0f, 0.0f }, { 0.0f, 0.0f, -1.0f } }; // 左上
	vertices[2] = { { 640.0f, 0.0f, 0.0f, 1.0f }, { 1.0f, 0.0f }, { 0.0f, 0.0f, -1.0f } }; // 右上
	vertices[3] = { { 640.0f, 360.0f, 0.0f, 1.0f }, { 1.0f, 1.0f }, { 0.0f, 0.0f, -1.0f } }; // 右下

	// インデックスバッファ
	// 三角形1枚目: 左下(0)→左上(1)→右下(3) / 三角形2枚目: 左上(1)→右上(2)→右下(3)
	sprite.indexResource = dx.CreateBufferResource(sizeof(uint32_t) * kIndexCount);
	sprite.ibv.BufferLocation = sprite.indexResource.Get()->GetGPUVirtualAddress();
	sprite.ibv.SizeInBytes = sizeof(uint32_t) * kIndexCount;
	sprite.ibv.Format = DXGI_FORMAT_R32_UINT;

	uint32_t* indices = nullptr;
	sprite.indexResource.Get()->Map(0, nullptr, reinterpret_cast< void** >( &indices ));
	indices[0] = 0; indices[1] = 1; indices[2] = 3;
	indices[3] = 1; indices[4] = 2; indices[5] = 3;

	// TransformationMatrix
	sprite.transformResource = dx.CreateBufferResource(sizeof(TransformationMatrix));
	sprite.transformResource.Get()->Map(0, nullptr, reinterpret_cast< void** >( &sprite.transformData ));
	sprite.transformData->WVP = MakeIdentityMatrix();
	sprite.transformData->World = MakeIdentityMatrix();
}

static void UpdateSprite(SpriteObject& sprite, float clientWidth, float clientHeight)
{
	// スプライトはカメラの影響を受けないので、ビュー行列は単位行列、射影は正射影
	Matrix4x4 world = MakeAffineMatrix(sprite.transform.scale, sprite.transform.rotate, sprite.transform.translate);
	Matrix4x4 view = MakeIdentityMatrix();
	Matrix4x4 projection = MakeOrthoGraphicMatrix(0.0f, 0.0f, clientWidth, clientHeight, 0.0f, 100.0f);
	sprite.transformData->WVP = Multiply(world, Multiply(view, projection));
	sprite.transformData->World = world;

	Matrix4x4 uv = MakeScaleMatrix(sprite.uvTransform.scale);
	uv = Multiply(uv, MakeRotateZMatrix(sprite.uvTransform.rotate.z));
	uv = Multiply(uv, MakeTranslateMatrix(sprite.uvTransform.translate));
	sprite.materialData->uvTransform = uv;
}

static void InitInstancing(DirectXCommon& dx, InstancingObject& inst)
{
	// 板ポリ(三角形2枚)
	const VertexData vertices[] = {
		{ { 1.0f, 1.0f, 0.0f, 1.0f }, { 0.0f, 0.0f }, { 0.0f, 0.0f, 1.0f } },
		{ { -1.0f, 1.0f, 0.0f, 1.0f }, { 1.0f, 0.0f }, { 0.0f, 0.0f, 1.0f } },
		{ { 1.0f, -1.0f, 0.0f, 1.0f }, { 0.0f, 1.0f }, { 0.0f, 0.0f, 1.0f } },
		{ { 1.0f, -1.0f, 0.0f, 1.0f }, { 0.0f, 1.0f }, { 0.0f, 0.0f, 1.0f } },
		{ { -1.0f, 1.0f, 0.0f, 1.0f }, { 1.0f, 0.0f }, { 0.0f, 0.0f, 1.0f } },
		{ { -1.0f, -1.0f, 0.0f, 1.0f }, { 1.0f, 1.0f }, { 0.0f, 0.0f, 1.0f } },
	};
	inst.vertexCount = static_cast< UINT >( std::size(vertices) );

	inst.vertexResource = dx.CreateBufferResource(sizeof(vertices));
	VertexData* mapped = nullptr;
	HRESULT hr = inst.vertexResource.Get()->Map(0, nullptr, reinterpret_cast< void** >( &mapped ));
	assert(SUCCEEDED(hr) && mapped != nullptr);
	std::memcpy(mapped, vertices, sizeof(vertices));

	inst.vbv.BufferLocation = inst.vertexResource.Get()->GetGPUVirtualAddress();
	inst.vbv.SizeInBytes = static_cast< UINT >( sizeof(vertices) );
	inst.vbv.StrideInBytes = sizeof(VertexData);

	// インスタンスごとの行列バッファ
	inst.instanceBuffer = dx.CreateBufferResource(sizeof(TransformationMatrix) * kNumInstance);
	inst.instanceBuffer.Get()->Map(0, nullptr, reinterpret_cast< void** >( &inst.data ));

	for ( uint32_t i = 0; i < kNumInstance; ++i )
	{
		inst.data[i].WVP = MakeIdentityMatrix();
		inst.data[i].World = MakeIdentityMatrix();

		inst.transforms[i].scale = { 1.0f, 1.0f, 1.0f };
		inst.transforms[i].rotate = { 0.0f, 0.0f, 0.0f };
		inst.transforms[i].translate = { i * 0.1f, i * 0.1f, i * 0.1f };
	}

	// StructuredBuffer用のSRV
	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
	srvDesc.Format = DXGI_FORMAT_UNKNOWN;
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
	srvDesc.Buffer.FirstElement = 0;
	srvDesc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;
	srvDesc.Buffer.NumElements = kNumInstance;
	srvDesc.Buffer.StructureByteStride = sizeof(TransformationMatrix);

	const uint32_t srvIndex = dx.AllocateSrvIndex();
	dx.GetDevice()->CreateShaderResourceView(inst.instanceBuffer.Get(), &srvDesc, dx.GetSRVCPUDescriptorHandle(srvIndex));
	inst.srvGpu = dx.GetSRVGPUDescriptorHandle(srvIndex);
}

static void UpdateInstancing(InstancingObject& inst, const Matrix4x4& viewProjection)
{
	for ( uint32_t i = 0; i < kNumInstance; ++i )
	{
		Matrix4x4 world = MakeAffineMatrix(inst.transforms[i].scale, inst.transforms[i].rotate, inst.transforms[i].translate);
		inst.data[i].WVP = Multiply(world, viewProjection);
		inst.data[i].World = world;
	}
}
#pragma endregion

//===================================================================
// パイプライン(RootSignature / PSO)
//===================================================================
#pragma region パイプライン
static ComPtr<ID3D12RootSignature> CreateRootSignature(ID3D12Device* device)
{
	D3D12_ROOT_SIGNATURE_DESC desc{};
	desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

	// テクスチャ用 (PS t0)
	D3D12_DESCRIPTOR_RANGE textureRange[1] = {};
	textureRange[0].BaseShaderRegister = 0;
	textureRange[0].NumDescriptors = 1;
	textureRange[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
	textureRange[0].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

	// インスタンシング用 (VS t0)
	D3D12_DESCRIPTOR_RANGE instancingRange[1] = {};
	instancingRange[0].BaseShaderRegister = 0;
	instancingRange[0].NumDescriptors = 1;
	instancingRange[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
	instancingRange[0].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

	// [0] Material(PS b0) [1] TransformationMatrix(VS b0) [2] Texture(PS table)
	// [3] DirectionalLight(PS b1) [4] Instancing(VS table)
	D3D12_ROOT_PARAMETER rootParameters[5] = {};
	rootParameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
	rootParameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
	rootParameters[0].Descriptor.ShaderRegister = 0;

	rootParameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
	rootParameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
	rootParameters[1].Descriptor.ShaderRegister = 0;

	rootParameters[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
	rootParameters[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
	rootParameters[2].DescriptorTable.pDescriptorRanges = textureRange;
	rootParameters[2].DescriptorTable.NumDescriptorRanges = _countof(textureRange);

	rootParameters[3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
	rootParameters[3].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
	rootParameters[3].Descriptor.ShaderRegister = 1;

	rootParameters[4].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
	rootParameters[4].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
	rootParameters[4].DescriptorTable.pDescriptorRanges = instancingRange;
	rootParameters[4].DescriptorTable.NumDescriptorRanges = _countof(instancingRange);

	desc.pParameters = rootParameters;
	desc.NumParameters = _countof(rootParameters);

	D3D12_STATIC_SAMPLER_DESC staticSamplers[1] = {};
	staticSamplers[0].Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR; // バイリニアフィルタ
	staticSamplers[0].AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
	staticSamplers[0].AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
	staticSamplers[0].AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
	staticSamplers[0].ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
	staticSamplers[0].MaxLOD = D3D12_FLOAT32_MAX; // ありったけのMipmapを使う
	staticSamplers[0].ShaderRegister = 0;
	staticSamplers[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
	desc.pStaticSamplers = staticSamplers;
	desc.NumStaticSamplers = _countof(staticSamplers);

	// シリアライズしてバイナリにする
	ComPtr<ID3DBlob> signatureBlob;
	ComPtr<ID3DBlob> errorBlob;
	HRESULT hr = D3D12SerializeRootSignature(&desc, D3D_ROOT_SIGNATURE_VERSION_1, &signatureBlob, &errorBlob);
	if ( FAILED(hr) )
	{
		Logger::Log(reinterpret_cast< char* >( errorBlob->GetBufferPointer() ));
		assert(false);
	}

	ComPtr<ID3D12RootSignature> rootSignature;
	hr = device->CreateRootSignature(0, signatureBlob->GetBufferPointer(), signatureBlob->GetBufferSize(), IID_PPV_ARGS(&rootSignature));
	assert(SUCCEEDED(hr));
	return rootSignature;
}

static PipelineSet CreatePipelines(DirectXCommon& dx, DXGI_FORMAT rtvFormat)
{
	ID3D12Device* device = dx.GetDevice();
	PipelineSet pipelines;
	pipelines.rootSignature = CreateRootSignature(device);

	// InputLayout
	D3D12_INPUT_ELEMENT_DESC inputElementDescs[3] = {};
	inputElementDescs[0].SemanticName = "POSITION";
	inputElementDescs[0].SemanticIndex = 0;
	inputElementDescs[0].Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
	inputElementDescs[0].AlignedByteOffset = D3D12_APPEND_ALIGNED_ELEMENT;

	inputElementDescs[1].SemanticName = "TEXCOORD";
	inputElementDescs[1].SemanticIndex = 0;
	inputElementDescs[1].Format = DXGI_FORMAT_R32G32_FLOAT;
	inputElementDescs[1].AlignedByteOffset = D3D12_APPEND_ALIGNED_ELEMENT;

	inputElementDescs[2].SemanticName = "NORMAL";
	inputElementDescs[2].SemanticIndex = 0;
	inputElementDescs[2].Format = DXGI_FORMAT_R32G32B32_FLOAT;
	inputElementDescs[2].AlignedByteOffset = D3D12_APPEND_ALIGNED_ELEMENT;

	D3D12_INPUT_LAYOUT_DESC inputLayoutDesc{};
	inputLayoutDesc.pInputElementDescs = inputElementDescs;
	inputLayoutDesc.NumElements = _countof(inputElementDescs);

	// RasterizerState: 裏面も表示する
	D3D12_RASTERIZER_DESC rasterizerDesc{};
	rasterizerDesc.CullMode = D3D12_CULL_MODE_NONE;
	rasterizerDesc.FillMode = D3D12_FILL_MODE_SOLID;

	// DepthStencilState: 近ければ描画する
	D3D12_DEPTH_STENCIL_DESC depthStencilDesc{};
	depthStencilDesc.DepthEnable = true;
	depthStencilDesc.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
	depthStencilDesc.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;

	// パーティクルは深度書き込みをしない
	D3D12_DEPTH_STENCIL_DESC particleDepthDesc = depthStencilDesc;
	particleDepthDesc.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;

	// シェーダー
	ComPtr<IDxcBlob> vs = dx.CompileShader(L"Object3D.VS.hlsl", L"vs_6_0");
	ComPtr<IDxcBlob> ps = dx.CompileShader(L"Object3D.PS.hlsl", L"ps_6_0");
	ComPtr<IDxcBlob> particleVs = dx.CompileShader(L"Particle.VS.hlsl", L"vs_6_0");
	ComPtr<IDxcBlob> particlePs = dx.CompileShader(L"Particle.PS.hlsl", L"ps_6_0");
	assert(vs && ps && particleVs && particlePs);

	// ベースのPSO設定(モデル/スプライト用)
	D3D12_GRAPHICS_PIPELINE_STATE_DESC baseDesc{};
	baseDesc.pRootSignature = pipelines.rootSignature.Get();
	baseDesc.InputLayout = inputLayoutDesc;
	baseDesc.VS = { vs->GetBufferPointer(), vs->GetBufferSize() };
	baseDesc.PS = { ps->GetBufferPointer(), ps->GetBufferSize() };
	baseDesc.RasterizerState = rasterizerDesc;
	baseDesc.NumRenderTargets = 1;
	baseDesc.RTVFormats[0] = rtvFormat;
	baseDesc.DepthStencilState = depthStencilDesc;
	baseDesc.DSVFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;
	baseDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	baseDesc.SampleDesc.Count = 1;
	baseDesc.SampleMask = D3D12_DEFAULT_SAMPLE_MASK;

	// スプライト: 通常のアルファブレンド
	{
		D3D12_GRAPHICS_PIPELINE_STATE_DESC desc = baseDesc;
		desc.BlendState = CreateParticleBlendDesc(ParticleBlendMode::Normal);
		HRESULT hr = device->CreateGraphicsPipelineState(&desc, IID_PPV_ARGS(&pipelines.sprite));
		assert(SUCCEEDED(hr));
	}

	// ブレンドモードごとのモデル用 / パーティクル用PSO
	for ( int i = 0; i < kBlendModeCount; ++i )
	{
		const D3D12_BLEND_DESC blend = CreateParticleBlendDesc(static_cast< ParticleBlendMode >(i));

		D3D12_GRAPHICS_PIPELINE_STATE_DESC modelDesc = baseDesc;
		modelDesc.BlendState = blend;
		HRESULT hr = device->CreateGraphicsPipelineState(&modelDesc, IID_PPV_ARGS(&pipelines.model[i]));
		assert(SUCCEEDED(hr));

		D3D12_GRAPHICS_PIPELINE_STATE_DESC particleDesc = baseDesc;
		particleDesc.VS = { particleVs->GetBufferPointer(), particleVs->GetBufferSize() };
		particleDesc.PS = { particlePs->GetBufferPointer(), particlePs->GetBufferSize() };
		particleDesc.DepthStencilState = particleDepthDesc;
		particleDesc.BlendState = blend;
		hr = device->CreateGraphicsPipelineState(&particleDesc, IID_PPV_ARGS(&pipelines.particle[i]));
		assert(SUCCEEDED(hr));

		pipelines.particleRaw[i] = pipelines.particle[i].Get();
	}
	return pipelines;
}
#pragma endregion

//===================================================================
// シーンの初期化 / 更新 / 描画
//===================================================================
#pragma region シーン
// 戻り値の中間リソースは dx.FlushCommands() が終わるまで保持すること
static std::vector<ResourceObject> InitScene(DirectXCommon& dx, Scene& scene)
{
	std::vector<ResourceObject> keepAlive;

	// カメラ
	scene.camera.Initialize();
	scene.debugCamera.Initialize();

	// ライト
	scene.directionalLightResource = dx.CreateBufferResource(sizeof(DirectionalLight));
	scene.directionalLightResource.Get()->Map(0, nullptr, reinterpret_cast< void** >( &scene.directionalLight ));
	scene.directionalLight->color = { 1.0f, 1.0f, 1.0f, 1.0f };
	scene.directionalLight->direction = { 0.0f, -1.0f, 0.0f };
	scene.directionalLight->intensity = 1.0f;

	// テクスチャ
	scene.spriteTexture = CreateTextureWithSrv(dx, "resources/uvChecker.png", keepAlive);
	scene.particleTexture = CreateTextureWithSrv(dx, "resources/particle.jpg", keepAlive);

	// モデル
	LoadAllModelResources(dx, scene.modelResources, keepAlive);

	scene.object1.modelNode = ModelNode::Sphere;
	InitRenderObject(dx, scene.object1);

	scene.object2.modelNode = ModelNode::Plane;
	scene.object2.transform.translate = { 3.0f, 0.0f, 0.0f, 0.0f };
	InitRenderObject(dx, scene.object2);

	// スプライト / インスタンシング
	InitSprite(dx, scene.sprite, scene.spriteTexture.srvGpu);
	InitInstancing(dx, scene.instancing);

	// パーティクルエミッタ
	scene.emitter.Initialize(dx.GetDevice());
	scene.emitter.config.position = { 0.0f, 0.0f, 35.0f, 1.0f };

	return keepAlive;
}

static void UpdateScene(Scene& scene, InputManager& input, float clientWidth, float clientHeight)
{
	// --- カメラ更新 ---
	// Update()はview/projectionを更新するために呼ぶだけで、worldMatrix引数は使わない
	// (各オブジェクトのWVPは自前で組み立てるため)
	if ( input.TriggerKey(DIK_F1) )
	{
		scene.useDebugCamera = !scene.useDebugCamera;
	}

	if ( scene.useDebugCamera )
	{
		scene.debugCamera.Update(MakeIdentityMatrix(), input);
		scene.viewMatrix = scene.debugCamera.GetViewMatrix();
		scene.projectionMatrix = scene.debugCamera.GetProjectionMatrix();
	} else
	{
		scene.camera.Update(MakeIdentityMatrix());
		scene.viewMatrix = scene.camera.GetViewMatrix();
		scene.projectionMatrix = scene.camera.GetProjectionMatrix();
	}
	scene.viewProjection = Multiply(scene.viewMatrix, scene.projectionMatrix);

	// --- オブジェクト更新 ---
	if ( scene.drawMode == DrawMode::Model )
	{
		UpdateRenderObject(scene.object1, scene.viewProjection);
		UpdateRenderObject(scene.object2, scene.viewProjection);
	} else
	{
		scene.emitter.Update(kDeltaTime);
	}

	UpdateSprite(scene.sprite, clientWidth, clientHeight);
	UpdateInstancing(scene.instancing, scene.viewProjection);
}

static void DrawScene(ID3D12GraphicsCommandList* commandList, Scene& scene, const PipelineSet& pipelines)
{
	ID3D12Resource* light = scene.directionalLightResource.Get();

	commandList->SetGraphicsRootSignature(pipelines.rootSignature.Get());
	commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

	// --- モデル or パーティクル ---
	if ( scene.drawMode == DrawMode::Model )
	{
		commandList->SetPipelineState(pipelines.model[static_cast< int >( scene.modelBlendMode )].Get());
		DrawRenderObject(commandList, scene.object1, scene.modelResources, light);
		DrawRenderObject(commandList, scene.object2, scene.modelResources, light);
	} else
	{
		scene.emitter.Draw(commandList, scene.viewMatrix, scene.projectionMatrix, scene.particleTexture.srvGpu, pipelines.particleRaw);
	}

	// --- スプライト ---
	// ルート引数は有効/無効に関わらず設定しておく(後続のインスタンシング描画が参照するため)
	commandList->SetPipelineState(pipelines.sprite.Get());
	commandList->IASetVertexBuffers(0, 1, &scene.sprite.vbv);
	commandList->IASetIndexBuffer(&scene.sprite.ibv);
	commandList->SetGraphicsRootConstantBufferView(0, scene.sprite.materialResource.Get()->GetGPUVirtualAddress());
	commandList->SetGraphicsRootConstantBufferView(1, scene.sprite.transformResource.Get()->GetGPUVirtualAddress());
	commandList->SetGraphicsRootDescriptorTable(2, scene.sprite.textureSrv);
	commandList->SetGraphicsRootConstantBufferView(3, light->GetGPUVirtualAddress()); // パーティクルモードでも未設定にならないように
	if ( scene.sprite.enabled )
	{
		commandList->DrawIndexedInstanced(6, 1, 0, 0, 0);
	}

	// --- インスタンシング描画 ---
	if ( scene.instancing.enabled )
	{
		commandList->SetPipelineState(pipelines.particle[static_cast< int >( ParticleBlendMode::Normal )].Get());
		commandList->SetGraphicsRootConstantBufferView(0, scene.sprite.materialResource.Get()->GetGPUVirtualAddress());
		commandList->SetGraphicsRootDescriptorTable(2, scene.particleTexture.srvGpu);
		commandList->SetGraphicsRootConstantBufferView(3, light->GetGPUVirtualAddress());
		commandList->SetGraphicsRootDescriptorTable(4, scene.instancing.srvGpu);
		commandList->IASetVertexBuffers(0, 1, &scene.instancing.vbv);
		commandList->DrawInstanced(scene.instancing.vertexCount, kNumInstance, 0, 0);
	}
}
#pragma endregion

//===================================================================
// ImGui
//===================================================================
#ifdef USE_IMGUI
#pragma region ImGui
// 1オブジェクト分のImGuiパネルを描画する
static void DrawRenderObjectImGui(const char* label, RenderObject& obj)
{
	ImGui::PushID(label);
	ImGui::Text("%s", label);

	int currentModel = static_cast< int >( obj.modelNode );
	if ( ImGui::Combo("Model", &currentModel, kModelNodeItems, IM_ARRAYSIZE(kModelNodeItems)) )
	{
		obj.modelNode = static_cast< ModelNode >( currentModel );
	}

	ImGui::DragFloat3("Scale", &obj.transform.scale.x, 0.1f);
	ImGui::DragFloat3("Rotate", &obj.transform.rotate.x, 0.1f);
	ImGui::DragFloat3("Translate", &obj.transform.translate.x, 0.1f);

	ImGui::DragFloat2("UV Translate", &obj.uvTransform.translate.x, 0.1f, -10.0f, 10.0f);
	ImGui::DragFloat2("UV Scale", &obj.uvTransform.scale.x, 0.1f, -10.0f, 10.0f);
	ImGui::SliderAngle("UV Rotate", &obj.uvTransform.rotate.z);

	ImGui::ColorEdit4("Color", &obj.materialData->color.x);

	int currentMode = static_cast< int >( obj.materialData->lightingMode );
	const char* items[] = { "None", "Half Lambert", "Lambert" };
	if ( ImGui::Combo("Lighting Mode", &currentMode, items, IM_ARRAYSIZE(items)) )
	{
		obj.materialData->lightingMode = static_cast< LightingMode >( currentMode );
	}

	ImGui::PopID();
}

// Camera / DebugCamera 共通のTransform編集UI
template <typename CameraType>
static void DrawCameraTransformImGui(CameraType& cam)
{
	Vector4 scale = cam.GetScale();
	if ( ImGui::DragFloat3("Camera Scale", &scale.x, 0.1f) )
	{
		cam.SetScale(scale);
	}
	Vector4 rotation = cam.GetRotation();
	if ( ImGui::DragFloat3("Camera Rotate", &rotation.x, 0.1f) )
	{
		cam.SetRotation(rotation);
	}
	Vector4 translation = cam.GetTranslation();
	if ( ImGui::DragFloat3("Camera Translate", &translation.x, 0.1f) )
	{
		cam.SetTranslation(translation);
	}
}

static void DrawCameraImGui(Scene& scene)
{
	if ( ImGui::Button("Use Camera") )
	{
		scene.useDebugCamera = false;
	}
	if ( ImGui::Button("Use Debug Camera") )
	{
		scene.useDebugCamera = true;
	}
	ImGui::Text("F1: toggle camera / WASD: move / Hold right click: look / Q: lock cursor / F: pivot mode");

	if ( !scene.useDebugCamera )
	{
		ImGui::Text("Camera Settings");
		DrawCameraTransformImGui(scene.camera);
	} else
	{
		ImGui::Text("Debug Camera Settings");
		DrawCameraTransformImGui(scene.debugCamera);
	}
}

static void DrawModelModeImGui(Scene& scene)
{
	ImGui::Text("Model");
	int blendIndex = static_cast< int >( scene.modelBlendMode );
	if ( ImGui::Combo("Blend Mode", &blendIndex, kBlendModeItems) )
	{
		scene.modelBlendMode = static_cast< ParticleBlendMode >( blendIndex );
	}

	DrawRenderObjectImGui("Object 1", scene.object1);
	ImGui::Separator();
	DrawRenderObjectImGui("Object 2", scene.object2);
	ImGui::Separator();

	ImGui::Text("Directional Light Settings");
	ImGui::ColorEdit4("Light Color", &scene.directionalLight->color.x);
	ImGui::DragFloat3("Light Direction", &scene.directionalLight->direction.x, 0.1f);
	ImGui::DragFloat("Light Intensity", &scene.directionalLight->intensity, 0.1f);
}

static void DrawParticleModeImGui(Scene& scene)
{
	ParticleEmitterConfig& config = scene.emitter.config;

	ImGui::Text("Particle Emitter");
	int blendMode = static_cast< int >( config.blendMode );
	if ( ImGui::Combo("Blend Mode", &blendMode, kBlendModeItems) )
	{
		config.blendMode = static_cast< ParticleBlendMode >( blendMode );
	}

	ImGui::DragFloat3("Emitter Position", &config.position.x, 0.1f);
	ImGui::ColorEdit4("Color Start", &config.colorStart.x);
	ImGui::ColorEdit4("Color End", &config.colorEnd.x);
	ImGui::DragFloat("Emit Rate", &config.emitRate, 0.5f, 0.1f, 256.0f);
	ImGui::DragFloat("Lifetime Min", &config.lifetimeMin, 0.1f, 0.1f, 60.0f);
	ImGui::DragFloat("Lifetime Max", &config.lifetimeMax, 0.1f, 0.1f, 60.0f);
	ImGui::DragFloat("Speed Min", &config.speedMin, 0.1f, 0.0f, 20.0f);
	ImGui::DragFloat("Speed Max", &config.speedMax, 0.1f, 0.0f, 20.0f);
	ImGui::DragFloat("Size Start", &config.sizeStart, 0.01f, 0.0f, 10.0f);
	ImGui::DragFloat("Size End", &config.sizeEnd, 0.01f, 0.0f, 10.0f);
	ImGui::Checkbox("Looping", &config.looping);
	if ( ImGui::Button("Burst (64)") )
	{
		scene.emitter.Emit(64);
	}
}

static void DrawSpriteImGui(SpriteObject& sprite)
{
	if ( ImGui::Button("Enable Sprite") )
	{
		sprite.enabled = true;
	}
	if ( ImGui::Button("Disable Sprite") )
	{
		sprite.enabled = false;
	}
	if ( sprite.enabled )
	{
		ImGui::Text("Sprite Settings");
		ImGui::DragFloat3("Sprite Scale", &sprite.transform.scale.x, 0.1f);
		ImGui::DragFloat3("Sprite Rotate", &sprite.transform.rotate.x, 0.1f);
		ImGui::DragFloat3("Sprite Translate", &sprite.transform.translate.x, 0.1f);
		ImGui::DragFloat2("UV Translate", &sprite.uvTransform.translate.x, 0.1f, -10.0f, 10.0f);
		ImGui::DragFloat2("UV Scale", &sprite.uvTransform.scale.x, 0.1f, -10.0f, 10.0f);
		ImGui::SliderAngle("UV Rotate", &sprite.uvTransform.rotate.z);
	}
}

// デバッグウィンドウ全体
static void DrawSceneUi(Scene& scene)
{
	ImGui::Begin("Debug");

	DrawCameraImGui(scene);
	ImGui::Separator();

	ImGui::Text("Draw Mode");
	if ( ImGui::RadioButton("Model", scene.drawMode == DrawMode::Model) )
	{
		scene.drawMode = DrawMode::Model;
	}
	ImGui::SameLine();
	if ( ImGui::RadioButton("Particles", scene.drawMode == DrawMode::Particles) )
	{
		scene.drawMode = DrawMode::Particles;
	}

	if ( scene.drawMode == DrawMode::Model )
	{
		DrawModelModeImGui(scene);
		ImGui::Separator();
		DrawSpriteImGui(scene.sprite);
	} else
	{
		ImGui::Separator();
		DrawParticleModeImGui(scene);
	}

	ImGui::Separator();
	ImGui::Checkbox("Instancing Demo", &scene.instancing.enabled);

	ImGui::End();
}
#pragma endregion
#endif

//===================================================================
// アプリ本体(シーンの寿命をDirectXCommonより短くするため、WinMainから切り出している)
//===================================================================
static void Run(WinApp& winApp, DirectXCommon& dx, WindowRenderer& renderer)
{
	InputManager input;
	input.Initialize(&winApp);

	// --- サウンド(現在は無効化) ---
	// (元のコメントはそのまま残してOK)

	// --- GPUリソースの作成 ---
	PipelineSet pipelines = CreatePipelines(dx, renderer.GetRtvFormat());
	Scene scene;
	{
		// アップロード用の中間リソースは、GPUへの転送が終わるまで生かしておく必要がある
		std::vector<ResourceObject> keepAlive = InitScene(dx, scene);
		dx.FlushUploadCommands();
	} // ここで中間リソースが解放される

	// --- ゲームループ ---
	while ( !WinApp::ProcessMessage() )
	{
		input.Update();

		// 最小化中・非表示中は更新も描画もしない。リサイズもここで適用される
		if ( !renderer.BeginFrame() )
		{
			Sleep(16);
			continue;
		}

		// UI → 更新 → 描画 の順
		renderer.BeginImGuiFrame();
#ifdef USE_IMGUI
		// ImGuiがマウス・キーボードを使っている間は、カメラ操作などのゲーム入力を無効にする
		const ImGuiIO& io = ImGui::GetIO();
		input.SetUiCapture(io.WantCaptureMouse, io.WantCaptureKeyboard);
		DrawSceneUi(scene);
#endif
		UpdateScene(scene, input,
			static_cast< float >( renderer.GetWidth() ), static_cast< float >( renderer.GetHeight() ));

		renderer.PreDraw();
		DrawScene(renderer.GetCommandList(), scene, pipelines);
		renderer.RenderImGui();
		renderer.PostDraw();
	}

	// シーンのリソースを解放する前に、GPUの処理が終わっていることを保証する
	renderer.WaitForGpu();
	scene.emitter.Shutdown();
}

//===================================================================
// エントリーポイント
//===================================================================
int WINAPI WinMain(_In_ HINSTANCE hInstance, _In_opt_ HINSTANCE, _In_ LPSTR, _In_ int)
{
	//システムタイマーの分解能を上げる
	timeBeginPeriod(1);

	HRESULT hr = CoInitializeEx(0, COINIT_MULTITHREADED); //マルチスレッドでCOMを利用するための初期化
	assert(SUCCEEDED(hr));
	( void ) hr;
	// 誰も補足しなかった場合に(Unhandled)、補足する関数を登録。main関数はじまってすぐに登録すると良い
	SetUnhandledExceptionFilter(ExportDump);

	// 最後に破棄されるよう、最初に宣言しておく
	D3DResourceLeakChecker leakCheck;

	{
		// 宣言の逆順(renderer → dx → winApp)で破棄されるので、依存関係の順に並べておく
		WinApp winApp;
		winApp.Initialize(hInstance, L"GameEngine", 1280, 720, WindowCloseBehavior::QuitApplication);

		DirectXCommon dxCommon;
		dxCommon.Initialize();

		WindowRenderer renderer;
		renderer.Initialize(&winApp, &dxCommon);

		WinApp editorWin;
		editorWin.Initialize(hInstance, L"Particle Editor", 960, 540, WindowCloseBehavior::HideWindow);
		WindowRenderer editorRenderer;
		editorRenderer.Initialize(&editorWin, &dxCommon);
		editorRenderer.SetVSync(false);   // vsync待ちが2回重なってFPSが半減するのを防ぐ
		InputManager editorInput;
		editorInput.Initialize(&editorWin);

		Run(winApp, dxCommon, renderer);
	}

	CoUninitialize();
	timeEndPeriod(1);
	return 0;
}

//	// 記録時間(FPS固定用)
//std::chrono::steady_clock::time_point referenceFPS_;
//// FPS
//// FPS固定用の初期化
//void InitializeVSync(); // Frames per Second固定用の初期化
//void UpdateVSync(); // Frames per Second固定用の更新
//void DirectXCommon::InitializeVSync()
//{
//	//現在の時間を記録する
//	referenceFPS_ = std::chrono::steady_clock::now();
//};
//void DirectXCommon::UpdateVSync()
//{
//	// 1/60秒ぴったりの時間
//	const std::chrono::microseconds kMinTime(uint64_t(1000000.0f / 60.0f));
//	// 1/60秒よりわずかに短い時間
//	const std::chrono::microseconds kMinCheckTime(uint64_t(1000000.0f / 65.0f));
//
//	//現在の時間を取得する
//	std::chrono::steady_clock::time_point currentTime = std::chrono::steady_clock::now();
//	// 前回の時間からの経過時間を計算する
//	std::chrono::microseconds elapsedTime = std::chrono::duration_cast< std::chrono::microseconds >( currentTime - referenceFPS_ );
//
//	// 1/60秒 (よりわずかに短い時間) 経ってない場合
//	if ( elapsedTime < kMinCheckTime )
//	{
//		// 1/60秒経過するまで微小なスリープするスリープを振り返す
//		while ( std::chrono::steady_clock::now() - referenceFPS_ < kMinTime )
//		{
//			// 1マイクロ秒スリープする
//			std::this_thread::sleep_for(std::chrono::microseconds(1));
//		}
//	}
//	//現在の時間を記録する
//	referenceFPS_ = std::chrono::steady_clock::now();
//
//	//// FPS固定用の更新
//	//// 1秒間に60回描画するための時間(ナノ秒)
//	//constexpr uint64_t kOneSecondInNanoseconds = 1000000000;
//	//constexpr uint64_t kTargetFPS = 60;
//	//constexpr uint64_t kFrameDuration = kOneSecondInNanoseconds / kTargetFPS;
//	//// 現在の時間を取得
//	//LARGE_INTEGER frequency;
//	//QueryPerformanceFrequency(&frequency);
//	//LARGE_INTEGER currentTime;
//	//QueryPerformanceCounter(&currentTime);
//	//// 次のフレームの開始時間まで待つ
//	//if ( currentTime.QuadPart < nextFrameTime )
//	//{
//	//	uint64_t waitTime = nextFrameTime - currentTime.QuadPart;
//	//	uint64_t waitMilliseconds = ( waitTime * 1000 ) / frequency.QuadPart;
//	//	Sleep(static_cast< DWORD >(waitMilliseconds));
//	//}
//	//// 次のフレームの開始時間を計算
//	//nextFrameTime += ( kFrameDuration * frequency.QuadPart ) / kOneSecondInNanoseconds;
//};