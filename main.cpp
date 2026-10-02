#include <Windows.h>
#include <cstdint>

#include <string>
#include <format>

#include <d3d12.h>
#include <dxgi1_6.h>
#include <cassert>

#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxgi.lib")

//Debug用のあれやこれやを使えるようにする
#include <dbghelp.h>
#pragma comment(lib, "Dbghelp.lib")

#include<strsafe.h>

#include <dxgidebug.h>
#pragma comment(lib, "dxguid.lib")

#include <dxcapi.h>
#pragma comment(lib, "dxcompiler.lib")

#include "WorldTransform.h"
#include "ResourceObject.h"
#include"Particle.h"
#include "SoundManager.h"
#include "InputManager.h"
#include "DebugCamera.h"
#include "Camera.h"
#include "ModelLoader.h"


#include <unordered_map>
#include <vector>
#include <cstring>

#ifdef USE_IMGUI
#include "externals/imgui/imgui.h"
#include "externals/imgui/imgui_impl_dx12.h"
#include "externals/imgui/imgui_impl_win32.h"
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
#endif 

#include<wrl.h>
using Microsoft::WRL::ComPtr;

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
	"Sphere",
	"Plane",
	"Axis",
	"Bunny",
	"MultiMaterial",
	"MultiMesh",
	"Suzanne",
	"Teapot",
};

// 球の分割数
static const uint32_t kSphereSubdivision = 16;
static const float kPiConst = 3.14159265358979323846f;

//===================================================================
// 追加: モデルリソースキャッシュ（1体分のサブメッシュ = 頂点バッファ + テクスチャ）
//===================================================================
struct SubMeshResource
{
	ResourceObject vertexResource;
	D3D12_VERTEX_BUFFER_VIEW vbv{};
	UINT vertexCount = 0;
	ResourceObject textureResource;
	D3D12_GPU_DESCRIPTOR_HANDLE textureSrvGpu{};
};

struct ModelResource
{
	std::vector<SubMeshResource> subMeshes;
};

static LONG WINAPI ExportDump(EXCEPTION_POINTERS* exception)
{
	//時刻を取得して、時刻を名前に入れたファイルを作成。Dumpsディレクトリ以下に出力
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
	//他に関連づけられているSEH例外ハンドラがあれば実行。通常はプロセスを終了する
	return EXCEPTION_EXECUTE_HANDLER;
}

// string -> wstring
std::wstring ConvertString(const std::string& str)
{
	if ( str.empty() )
	{
		return std::wstring();
	}

	auto sizeNeeded = MultiByteToWideChar(CP_UTF8, 0, reinterpret_cast< const char* >( &str[0] ), static_cast< int >( str.size() ), NULL, 0);
	if ( sizeNeeded == 0 )
	{
		return std::wstring();
	}
	std::wstring result(sizeNeeded, 0);
	MultiByteToWideChar(CP_UTF8, 0, reinterpret_cast< const char* >( &str[0] ), static_cast< int >( str.size() ), &result[0], sizeNeeded);
	return result;
}
// wstring -> string
std::string ConvertString(const std::wstring& str)
{
	if ( str.empty() )
	{
		return std::string();
	}

	auto sizeNeeded = WideCharToMultiByte(CP_UTF8, 0, str.data(), static_cast< int >( str.size() ), NULL, 0, NULL, NULL);
	if ( sizeNeeded == 0 )
	{
		return std::string();
	}
	std::string result(sizeNeeded, 0);
	WideCharToMultiByte(CP_UTF8, 0, str.data(), static_cast< int >( str.size() ), result.data(), sizeNeeded, NULL, NULL);
	return result;
}
void Log(const std::string& message)
{
	OutputDebugStringA(message.c_str());
}

DirectX::ScratchImage LoadTexture(const std::string& filePath)
{
	DirectX::ScratchImage image;
	std::wstring filePathW = ConvertString(filePath);

	HRESULT hr = DirectX::LoadFromWICFile(
		filePathW.c_str(),
		DirectX::WIC_FLAGS_FORCE_SRGB,
		nullptr,
		image);
	assert(SUCCEEDED(hr));

	DirectX::ScratchImage mipImages;
	hr = DirectX::GenerateMipMaps(
		image.GetImages(),
		image.GetImageCount(),
		image.GetMetadata(),
		DirectX::TEX_FILTER_SRGB,
		0,
		mipImages);

	if ( FAILED(hr) )
	{
		// 1x1 textures (and some other formats) don't need mipmaps.
		return image;
	}

	return mipImages;
}

Microsoft::WRL::ComPtr <ID3D12DescriptorHeap> CreateDescriptorHeap(ID3D12Device* device, D3D12_DESCRIPTOR_HEAP_TYPE type, UINT numDescriptors, bool shaderVisible)
{
	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> descriptorHeap = nullptr;
	D3D12_DESCRIPTOR_HEAP_DESC descriptorHeapDesc{};
	descriptorHeapDesc.Type = type;
	descriptorHeapDesc.NumDescriptors = numDescriptors;
	descriptorHeapDesc.Flags = shaderVisible ? D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE : D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
	HRESULT hr = device->CreateDescriptorHeap(&descriptorHeapDesc, IID_PPV_ARGS(&descriptorHeap));
	assert(SUCCEEDED(hr));
	return descriptorHeap;
}


ID3D12Resource* CreateBufferResource(ID3D12Device* device, size_t sizeInBytes)
{
	//頂点リソース用のヒープ用の設定
	D3D12_HEAP_PROPERTIES uploadHeapProperties{};
	uploadHeapProperties.Type = D3D12_HEAP_TYPE_UPLOAD;
	//頂点リソースの設定
	D3D12_RESOURCE_DESC vertexResourceDesc{};
	//バッファリソース、テクスチャの場合はまた別の設定をする
	vertexResourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
	vertexResourceDesc.Width = sizeInBytes; // リソースサイズ、今回はVector4を３頂点分
	// バッファの場合はこれは１にする決まり
	vertexResourceDesc.Height = 1;
	vertexResourceDesc.DepthOrArraySize = 1;
	vertexResourceDesc.MipLevels = 1;
	vertexResourceDesc.SampleDesc.Count = 1;
	// バファの場合はこれにする決まり
	vertexResourceDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
	// 実際に頂点リソースを作る
	ID3D12Resource* vertexResource = nullptr;
	HRESULT hr = device->CreateCommittedResource(&uploadHeapProperties, D3D12_HEAP_FLAG_NONE, &vertexResourceDesc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&vertexResource));
	assert(SUCCEEDED(hr));

	return vertexResource;
}

ID3D12Resource* CreateTextureResource(ID3D12Device* device, const DirectX::TexMetadata& metaData)
{
	//metaDataを基にResourcwの設定
	D3D12_RESOURCE_DESC resourceDesc{};
	resourceDesc.Width = UINT(metaData.width); // Textureの幅
	resourceDesc.Height = UINT(metaData.height); // Textureの高さ
	resourceDesc.MipLevels = UINT16(metaData.mipLevels); // mipMapの関数
	resourceDesc.DepthOrArraySize = UINT16(metaData.arraySize); // 奥行き or 配列Textureの配列数
	resourceDesc.Format = metaData.format; // TextureのFormat
	resourceDesc.SampleDesc.Count = 1; //　サンプリングカウント。1固定
	resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION(metaData.dimension); // Textureの次元数。普段使っているのは２次元

	//利用するHeapの設定。非常に特殊な運用。02_04exで一般敵なケース版がある
	D3D12_HEAP_PROPERTIES heapProperties{};
	heapProperties.Type = D3D12_HEAP_TYPE_DEFAULT; // 細かい設定を行う


	//Resourceの生成
	ID3D12Resource* resource = nullptr;
	HRESULT hr = device->CreateCommittedResource(
		&heapProperties, // Heapの設定
		D3D12_HEAP_FLAG_NONE, // Heapの特殊な設定。特になし
		&resourceDesc, // Resourceの設定
		D3D12_RESOURCE_STATE_COPY_DEST,// 初回のResourceState。Textureは基本読むだけ
		nullptr, // Clear最適値。使わないのでnullptr
		IID_PPV_ARGS(&resource)); //　作成するResourceポインタへのポインタ
	assert(SUCCEEDED(hr));
	return resource;
}

D3D12_CPU_DESCRIPTOR_HANDLE GetCPUDescriptorHandle(ID3D12DescriptorHeap* descriptorHeap, uint32_t descriptorSize, uint32_t index)
{
	D3D12_CPU_DESCRIPTOR_HANDLE handleCPU = descriptorHeap->GetCPUDescriptorHandleForHeapStart();
	handleCPU.ptr += ( descriptorSize * index );
	return handleCPU;
}
D3D12_GPU_DESCRIPTOR_HANDLE GetGPUDescriptorHandle(ID3D12DescriptorHeap* descriptorHeap, uint32_t descriptorSize, uint32_t index)
{
	D3D12_GPU_DESCRIPTOR_HANDLE handleGPU = descriptorHeap->GetGPUDescriptorHandleForHeapStart();
	handleGPU.ptr += ( descriptorSize * index );
	return handleGPU;
}


[[nodiscard]]
ID3D12Resource* UploadTextureData(ID3D12Resource* texture, const DirectX::ScratchImage& mipImages, ID3D12Device* device, ID3D12GraphicsCommandList* commandList)
{
	std::vector<D3D12_SUBRESOURCE_DATA> subresources;
	DirectX::PrepareUpload(device, mipImages.GetImages(), mipImages.GetImageCount(), mipImages.GetMetadata(), subresources);
	uint64_t intermediateSize = GetRequiredIntermediateSize(texture, 0, UINT(subresources.size()));
	ID3D12Resource* intermediateResource = CreateBufferResource(device, intermediateSize);
	UpdateSubresources(commandList, texture, intermediateResource, 0, 0, UINT(subresources.size()), subresources.data());
	// Textureへの転送後は利用できるよう、D3D12_RESOURCE_STATE_COPY_DESTからD3D12_RESOURCE_STATE_GENERIC_READへResourceStateを変更する
	D3D12_RESOURCE_BARRIER barrier{};
	barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
	barrier.Transition.pResource = texture;
	barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
	barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
	barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_GENERIC_READ;
	commandList->ResourceBarrier(1, &barrier);
	return intermediateResource;
}

ID3D12Resource* CreateDepthStencilTextureResource(ID3D12Device* device, int32_t width, int32_t height)
{
	//生成するResourceの設定
	D3D12_RESOURCE_DESC resourceDesc{};
	resourceDesc.Width = width; // Textureの幅。
	resourceDesc.Height = height; // Textureの高さ。
	resourceDesc.MipLevels = 1; // mipMapの数。DepthStencilは基本的に1
	resourceDesc.DepthOrArraySize = 1; // 奥行き or 配列Textureの配列数。DepthStencilは基本的に配列なし
	resourceDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT; // DepthStencilのFormat。今回は深度だけのフォーマットにする
	resourceDesc.SampleDesc.Count = 1; // サンプリングカウント。1固定
	resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D; // Textureの次元数。DepthStencilは基本的に2D
	resourceDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL; // DepthStencilとして使う通知

	// 利用するHeapの設定
	D3D12_HEAP_PROPERTIES heapProperties{};
	heapProperties.Type = D3D12_HEAP_TYPE_DEFAULT; // VRAM上に作る

	// 深度のクリア設定
	D3D12_CLEAR_VALUE depthClearValue{};
	depthClearValue.DepthStencil.Depth = 1.0f; // 最大(最大値)でクリア
	depthClearValue.Format = resourceDesc.Format; // DepthStencilのFormatと同じにする

	// リソースの生成
	ID3D12Resource* resource = nullptr;
	HRESULT hr = device->CreateCommittedResource(
		&heapProperties, // Heapの設定
		D3D12_HEAP_FLAG_NONE, // Heapの特殊な設定。特になし
		&resourceDesc, // Resourceの設定
		D3D12_RESOURCE_STATE_DEPTH_WRITE, // 深度値を書き込む状態にしておく
		&depthClearValue, // Clear最適値。
		IID_PPV_ARGS(&resource)); // 作成するResourceポインタへのポインタ
	assert(SUCCEEDED(hr));
	return resource;
}

Microsoft::WRL::ComPtr<IDxcBlob> CompileShader(
// Compilerするshaderファイルへのパス
const std::wstring& filePath,
// CompilerにしようするProfile
const wchar_t* profile,
//初期化で生成したものを三つ
IDxcUtils* dxcUtils, IDxcCompiler3* dxcCompiler, IDxcIncludeHandler* includeHandler)
{
	//これからシェーダーをコンパイルする時をログに出す
	Log(ConvertString(std::format(L"Begin CompileShader, path:{}, profile:{}\n", filePath, profile)));
	// hlslファイルを読む
	IDxcBlobEncoding* shaderSource = nullptr;
	HRESULT hr = dxcUtils->LoadFile(filePath.c_str(), nullptr, &shaderSource);
	//読めなかったら止める
	assert(SUCCEEDED(hr));
	//読み込んだファイルの内容を設定する
	DxcBuffer shaderSourceBuffer;
	shaderSourceBuffer.Ptr = shaderSource->GetBufferPointer();
	shaderSourceBuffer.Size = shaderSource->GetBufferSize();
	shaderSourceBuffer.Encoding = DXC_CP_UTF8; //UTF8の文字コードであることを通知

	LPCWSTR arguments[] = {
		filePath.c_str(), // コンパイル対象のhlslファイル名
		L"-E", L"main", // エントリーポイントの指定。基本的にmain以外にはしない 
		L"-T", profile, // ShaderProfileの設定
		L"-Zi", L"-Qembed_debug", // デバッグ用の情報を埋め込む
		L"-Od", // 最適化を外しておく
		L"-Zpr",  // メモリレイアウトは行優先
	};
	// 実際にShaderをコンパイルする
	IDxcResult* shaderResult = nullptr;
	hr = dxcCompiler->Compile(
		&shaderSourceBuffer, //読み込んだファイル
		arguments, // コンパイルオプション
		_countof(arguments), // コンパイルオプションの数
		includeHandler, // includeが含まれた諸々
		IID_PPV_ARGS(&shaderResult) //コンパイル結果
	);
	//コンパイルエラーではなくdxcが起動できないなど致命的な状況
	assert(SUCCEEDED(hr));

	//警告・エラーが出てたらログに出して止める
	IDxcBlobUtf8* shaderError = nullptr;
	shaderResult->GetOutput(DXC_OUT_ERRORS, IID_PPV_ARGS(&shaderError), nullptr);
	if ( shaderError != nullptr && shaderError->GetStringLength() != 0 )
	{
		Log(shaderError->GetStringPointer());
		//警告・エラーダメ絶対
		assert(false);
	}

	// コンパイル結果から実行用のバイナリ部分を取得
	Microsoft::WRL::ComPtr<IDxcBlob> shaderBlob = nullptr;
	hr = shaderResult->GetOutput(DXC_OUT_OBJECT, IID_PPV_ARGS(&shaderBlob), nullptr);
	assert(SUCCEEDED(hr));
	//成功したログを出す
	Log(ConvertString(std::format(L"Compile Succeeded, path:{}, profile:{}\n", filePath, profile)));
	//実行のバイナリを返却
	return shaderBlob;
}

struct D3DResourceLeakChecker
{
	~D3DResourceLeakChecker()
	{
		//リソースリークチェック
		Microsoft::WRL::ComPtr<IDXGIDebug1> debug;
		if ( SUCCEEDED(DXGIGetDebugInterface1(0, IID_PPV_ARGS(&debug))) )
		{
			debug->ReportLiveObjects(DXGI_DEBUG_ALL, DXGI_DEBUG_RLO_ALL);
			debug->ReportLiveObjects(DXGI_DEBUG_APP, DXGI_DEBUG_RLO_ALL);
			debug->ReportLiveObjects(DXGI_DEBUG_D3D12, DXGI_DEBUG_RLO_ALL);
		}
	}
};

//===================================================================
// 追加: 球の頂点生成（分割数16、インデックス無しの三角形リスト）
//===================================================================
std::vector<VertexData> GenerateSphereVertices(uint32_t subdivision)
{
	std::vector<VertexData> vertices;
	vertices.resize(size_t(subdivision) * subdivision * 6);

	const float kLonEvery = ( 2.0f * kPiConst ) / float(subdivision); //経度分割１つ分の角度
	const float kLatEvery = kPiConst / float(subdivision);          //緯度分割１つ分の角度
	const float step = 1.0f / float(subdivision);

	for ( uint32_t latIndex = 0; latIndex < subdivision; ++latIndex )
	{
		float lat = -kPiConst / 2.0f + kLatEvery * latIndex; //現在の緯度
		for ( uint32_t lonIndex = 0; lonIndex < subdivision; ++lonIndex )
		{
			float u = float(lonIndex) / float(subdivision);
			float v = 1.0f - float(latIndex) / float(subdivision);
			float lon = lonIndex * kLonEvery; //現在の経度
			uint32_t startIndex = ( latIndex * subdivision + lonIndex ) * 6;

			VertexData a{}, b{}, c{}, d{};

			//基準点a
			a.position = { cosf(lat) * cosf(lon), sinf(lat), cosf(lat) * sinf(lon), 1.0f };
			a.texcoord = { u, v };
			a.normal = { a.position.x, a.position.y, a.position.z };
			//基準点b
			b.position = { cosf(lat + kLatEvery) * cosf(lon), sinf(lat + kLatEvery), cosf(lat + kLatEvery) * sinf(lon), 1.0f };
			b.texcoord = { u, v - step };
			b.normal = { b.position.x, b.position.y, b.position.z };
			//基準点c
			c.position = { cosf(lat) * cosf(lon + kLonEvery), sinf(lat), cosf(lat) * sinf(lon + kLonEvery), 1.0f };
			c.texcoord = { u + step, v };
			c.normal = { c.position.x, c.position.y, c.position.z };
			//基準点d
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
void AddSubMesh(
	ModelResource& model,
	const std::vector<VertexData>& verts,
	const std::string& textureFilePath,
	const char* modelName,
	ID3D12Device* device,
	ID3D12GraphicsCommandList* commandList,
	ID3D12DescriptorHeap* srvHeap,
	uint32_t descriptorSizeSRV,
	uint32_t& nextSrvIndex,
	std::vector<ResourceObject>& keepAliveIntermediates)
{
	// 頂点が1つも無い(=objファイルが見つからなかった/読めなかった)場合はここで気付けるようにする。
	// このままCreateBufferResourceに0バイトを渡すと、リソース生成が失敗してvertexResourceがnullptrのまま
	// 返ってきて、直後のMap呼び出しで"mapped"がnullになる(あるいはクラッシュする)原因になる。
	//
	// ここではassertで即停止せず、警告を出してこのサブメッシュだけスキップする。
	// 1つ止まるたびにデバッグし直すのではなく、1回の実行でどのモデルが全部欠けているかまとめて確認できるようにするため。
	if ( verts.empty() )
	{
		Log(std::format("[AddSubMesh] WARNING: \"{}\" produced an empty vertex list (texture path was \"{}\"). "
			"Check that the corresponding .obj file exists under resources/ and that the Working Directory is set correctly.\n",
			modelName, textureFilePath));
		return;
	}

	SubMeshResource sub;

	// 頂点バッファ
	sub.vertexResource = CreateBufferResource(device, sizeof(VertexData) * verts.size());
	assert(sub.vertexResource.Get() != nullptr && "CreateBufferResource failed for vertex buffer");

	VertexData* mapped = nullptr;
	HRESULT hrMap = sub.vertexResource.Get()->Map(0, nullptr, reinterpret_cast< void** >( &mapped ));
	assert(SUCCEEDED(hrMap) && mapped != nullptr && "Map() failed on vertex buffer");

	std::memcpy(mapped, verts.data(), sizeof(VertexData) * verts.size());
	sub.vbv.BufferLocation = sub.vertexResource.Get()->GetGPUVirtualAddress();
	sub.vbv.SizeInBytes = UINT(sizeof(VertexData) * verts.size());
	sub.vbv.StrideInBytes = sizeof(VertexData);
	sub.vertexCount = UINT(verts.size());

	// テクスチャ（materialが無ければuvCheckerで代用）
	std::string texPath = textureFilePath.empty() ? "resources/uvChecker.png" : textureFilePath;
	DirectX::ScratchImage mip = LoadTexture(texPath);
	const DirectX::TexMetadata& meta = mip.GetMetadata();
	sub.textureResource = CreateTextureResource(device, meta);
	ResourceObject intermediate = UploadTextureData(sub.textureResource.Get(), mip, device, commandList);
	keepAliveIntermediates.push_back(std::move(intermediate));

	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
	srvDesc.Format = meta.format;
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	srvDesc.Texture2D.MipLevels = UINT(meta.mipLevels);

	D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle = GetCPUDescriptorHandle(srvHeap, descriptorSizeSRV, nextSrvIndex);
	D3D12_GPU_DESCRIPTOR_HANDLE gpuHandle = GetGPUDescriptorHandle(srvHeap, descriptorSizeSRV, nextSrvIndex);
	device->CreateShaderResourceView(sub.textureResource.Get(), &srvDesc, cpuHandle);
	sub.textureSrvGpu = gpuHandle;
	++nextSrvIndex;

	model.subMeshes.push_back(std::move(sub));
}

// 9種類のモデルを全て読み込んでキャッシュに詰める。
// ※ ここではファイル名を仮定しています。実際のresources以下のファイル名に合わせて変更してください。
std::vector<ResourceObject> LoadAllModelResources(
	std::unordered_map<ModelNode, ModelResource>& modelResources,
	ID3D12Device* device,
	ID3D12GraphicsCommandList* commandList,
	ID3D12DescriptorHeap* srvHeap,
	uint32_t descriptorSizeSRV,
	uint32_t& nextSrvIndex)
{
	std::vector<ResourceObject> keepAliveIntermediates;

	// Sphere（プロシージャル生成、テクスチャはuvCheckerを流用）
	{
		ModelResource model;
		AddSubMesh(model, GenerateSphereVertices(kSphereSubdivision), "", "Sphere", device, commandList, srvHeap, descriptorSizeSRV, nextSrvIndex, keepAliveIntermediates);
		modelResources[ModelNode::Sphere] = std::move(model);
	}
	// Plane
	{
		ModelData md = LoadObjFile("resources", "plane.obj");
		ModelResource model;
		AddSubMesh(model, md.vertices, md.material.textureFilePath, "Plane", device, commandList, srvHeap, descriptorSizeSRV, nextSrvIndex, keepAliveIntermediates);
		modelResources[ModelNode::Plane] = std::move(model);
	}
	// Axis
	{
		ModelData md = LoadObjFile("resources", "axis.obj");
		ModelResource model;
		AddSubMesh(model, md.vertices, md.material.textureFilePath, "Axis", device, commandList, srvHeap, descriptorSizeSRV, nextSrvIndex, keepAliveIntermediates);
		modelResources[ModelNode::Axis] = std::move(model);
	}
	// Bunny
	{
		ModelData md = LoadObjFile("resources", "bunny.obj");
		ModelResource model;
		AddSubMesh(model, md.vertices, md.material.textureFilePath, "Bunny", device, commandList, srvHeap, descriptorSizeSRV, nextSrvIndex, keepAliveIntermediates);
		modelResources[ModelNode::Bunny] = std::move(model);
	}
	// MultiMaterial（サブメッシュ複数）
	{
		MultiMeshModelData md = LoadObjFileMulti("resources", "multiMaterial.obj");
		ModelResource model;
		for ( auto& sm : md.subMeshes )
		{
			AddSubMesh(model, sm.vertices, sm.material.textureFilePath, "MultiMaterial", device, commandList, srvHeap, descriptorSizeSRV, nextSrvIndex, keepAliveIntermediates);
		}
		modelResources[ModelNode::MultiMaterial] = std::move(model);
	}
	// MultiMesh（サブメッシュ複数）
	{
		MultiMeshModelData md = LoadObjFileMulti("resources", "multiMesh.obj");
		ModelResource model;
		for ( auto& sm : md.subMeshes )
		{
			AddSubMesh(model, sm.vertices, sm.material.textureFilePath, "MultiMesh", device, commandList, srvHeap, descriptorSizeSRV, nextSrvIndex, keepAliveIntermediates);
		}
		modelResources[ModelNode::MultiMesh] = std::move(model);
	}
	// Suzanne（マテリアルのテクスチャは無視して、常にwhite1x1を貼る）
	{
		ModelData md = LoadObjFile("resources", "suzanne.obj");
		ModelResource model;
		AddSubMesh(model, md.vertices, "resources/white1x1.png", "Suzanne", device, commandList, srvHeap, descriptorSizeSRV, nextSrvIndex, keepAliveIntermediates);
		modelResources[ModelNode::Suzanne] = std::move(model);
	}
	// Teapot（teapot.objはvt(テクスチャ座標)を1つも持っておらず、全頂点のUVが(0,0)にフォールバックしてしまう。
	// そのままuvChecker等の絵柄付きテクスチャを貼ると、全体が同じ1点をサンプリングした単色の塊に見えてしまう
	// -- これが「テクスチャが1枚しか生成されていないように見える」症状の実体。
	// UV自体が無い以上、模様入りテクスチャでは直しきれないので、Suzanneと同じくwhite1x1にして
	// Half-Lambertの陰影だけで形が分かるようにする）
	{
		ModelData md = LoadObjFile("resources", "teapot.obj");
		ModelResource model;
		AddSubMesh(model, md.vertices, "resources/white1x1.png", "Teapot", device, commandList, srvHeap, descriptorSizeSRV, nextSrvIndex, keepAliveIntermediates);
		modelResources[ModelNode::Teapot] = std::move(model);
	}

	return keepAliveIntermediates;
}

//===================================================================
// 追加: 1つの描画対象（モデル種別 + トランスフォーム + マテリアル + WVP）
//===================================================================
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

//ウィンドウプロシージャ
LRESULT CALLBACK WindowProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{

#ifdef USE_IMGUI
	if ( ImGui_ImplWin32_WndProcHandler(hwnd, msg, wparam, lparam) )
	{
		return true;
	}
#endif
	//メッセージに応じてゲーム固有の処理を行う
	switch ( msg )
	{
		// ウィンドウが破棄されたときの処理
		case WM_DESTROY:
			// OSに対して、アプリケーションの終了を伝える
			PostQuitMessage(0);
			break;
	}

	//基準にメッセージ処理を行う
	return DefWindowProc(hwnd, msg, wparam, lparam);
}

void InitRenderObject(RenderObject& obj, ID3D12Device* device)
{
	obj.materialResource = CreateBufferResource(device, sizeof(Material));
	obj.materialResource.Get()->Map(0, nullptr, reinterpret_cast< void** >( &obj.materialData ));
	*obj.materialData = {};
	obj.materialData->color = { 1.0f, 1.0f, 1.0f, 1.0f };
	obj.materialData->lightingMode = LightingMode::HalfLambert;
	obj.materialData->uvTransform = MakeIdentityMatrix();

	obj.wvpResource = CreateBufferResource(device, sizeof(TransformationMatrix));
	obj.wvpResource.Get()->Map(0, nullptr, reinterpret_cast< void** >( &obj.wvpData ));
	obj.wvpData->WVP = MakeIdentityMatrix();
	obj.wvpData->World = MakeIdentityMatrix();
}

// カメラで作った共通のviewProjectionを使ってオブジェクトのWVPを更新する
void UpdateRenderObject(RenderObject& obj, const Matrix4x4& viewProjection)
{
	Matrix4x4 world = MakeAffineMatrix(obj.transform.scale, obj.transform.rotate, obj.transform.translate);
	obj.wvpData->WVP = Multiply(world, viewProjection);
	obj.wvpData->World = world;

	Matrix4x4 uv = MakeScaleMatrix(obj.uvTransform.scale);
	uv = Multiply(uv, MakeRotateZMatrix(obj.uvTransform.rotate.z));
	uv = Multiply(uv, MakeTranslateMatrix(obj.uvTransform.translate));
	obj.materialData->uvTransform = uv;
}

// オブジェクトを描画する（RootSignature/PSO/DescriptorHeap/Viewportは呼び出し側で設定済みの前提）
void DrawRenderObject(ID3D12GraphicsCommandList* commandList, const RenderObject& obj, const std::unordered_map<ModelNode, ModelResource>& modelResources, ID3D12Resource* directionalLightResource)
{
	commandList->SetGraphicsRootConstantBufferView(0, obj.materialResource.Get()->GetGPUVirtualAddress());
	commandList->SetGraphicsRootConstantBufferView(1, obj.wvpResource.Get()->GetGPUVirtualAddress());
	commandList->SetGraphicsRootConstantBufferView(3, directionalLightResource->GetGPUVirtualAddress());

	const ModelResource& model = modelResources.at(obj.modelNode);
	for ( const auto& sub : model.subMeshes )
	{
		commandList->IASetVertexBuffers(0, 1, &sub.vbv);
		commandList->SetGraphicsRootDescriptorTable(2, sub.textureSrvGpu);
		commandList->DrawInstanced(sub.vertexCount, 1, 0, 0);
	}
}

#ifdef USE_IMGUI
// 1オブジェクト分のImGuiパネルを描画する
void DrawRenderObjectImGui(const char* label, RenderObject& obj)
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


void EnableCamera(bool& useDebugCamera, Camera& camera, DebugCamera& debugCamera)
{
	if ( ImGui::Button("Use Camera") )
	{
		useDebugCamera = false;
	}
	if ( ImGui::Button("Use Debug Camera") )
	{
		useDebugCamera = true;
	}
	ImGui::Text("WASD to move. Hold Right click to move mouse view");
	if ( !useDebugCamera )
	{
		ImGui::Text("Camera Settings");
		Vector4 currentCameraScale = camera.GetScale();
		if ( ImGui::DragFloat3("Camera Scale", &currentCameraScale.x, 0.1f) )
		{
			camera.SetScale(currentCameraScale);
		}

		Vector4 currentCameraRotation = camera.GetRotation();
		if ( ImGui::DragFloat3("Camera Rotate", &currentCameraRotation.x, 0.1f) )
		{
			camera.SetRotation(currentCameraRotation);
		}

		Vector4 currentCameraTranslation = camera.GetTranslation();
		if ( ImGui::DragFloat3("Camera Translate", &currentCameraTranslation.x, 0.1f) )
		{
			camera.SetTranslation(currentCameraTranslation);
		}
	} else
	{
		ImGui::Text("Debug Camera Settings");
		Vector4 currentCameraScale = debugCamera.GetScale();
		if ( ImGui::DragFloat3("Camera Scale", &currentCameraScale.x, 0.1f) )
		{
			debugCamera.SetScale(currentCameraScale);
		}

		Vector4 currentCameraRotation = debugCamera.GetRotation();
		if ( ImGui::DragFloat3("Camera Rotate", &currentCameraRotation.x, 0.1f) )
		{
			debugCamera.SetRotation(currentCameraRotation);
		}

		Vector4 currentCameraTranslation = debugCamera.GetTranslation();
		if ( ImGui::DragFloat3("Camera Translate", &currentCameraTranslation.x, 0.1f) )
		{
			debugCamera.SetTranslation(currentCameraTranslation);
		}
	}
}
#endif
// Windowアプリでのエントリーポイント(main関数)
int WINAPI WinMain(_In_ HINSTANCE, _In_opt_ HINSTANCE, _In_ LPSTR, _In_ int)
{
	CoInitializeEx(0, COINIT_MULTITHREADED); //マルチスレッドでCOMを利用するための初期化
	//誰も補足しなかった場合に(Unhandled)、補足する関数を登録
// main関数はじまってすぐに登録すると良い
	SetUnhandledExceptionFilter(ExportDump);

	D3DResourceLeakChecker leakCheck;

	WNDCLASS wc{};
	//ウィンドウプロシージャ
	wc.lpfnWndProc = WindowProc;
	// ウィンドウクラス名
	wc.lpszClassName = L"GE3Window";
	//インスタンスハンドル
	wc.hInstance = GetModuleHandle(nullptr);
	//カーソル
	wc.hCursor = LoadCursor(nullptr, IDC_ARROW);

	RegisterClass(&wc);

	//クライント領域のサイズ
	const int32_t kClientWidth = 1280;
	const int32_t kClientHeight = 720;

	//ウィンドウサイズを表す構造体にクライアント領域を入れる
	RECT wrc{ 0, 0, kClientWidth, kClientHeight };

	//クライント領域を元に実際のサイズにwrcを変更してもらう
	AdjustWindowRect(&wrc, WS_OVERLAPPEDWINDOW, FALSE);

	HWND hwnd = CreateWindow(
		wc.lpszClassName, //クラス名
		L"LE2C_29_メンドーザ_ケビン_ラルフ_パレルモ", //タイトルバーの文字
		WS_OVERLAPPEDWINDOW, //ウィンドウスタイル
		CW_USEDEFAULT, //初期X座標
		CW_USEDEFAULT, //初期Y座標
		wrc.right - wrc.left, //ウィンドウ幅
		wrc.bottom - wrc.top, //ウィンドウ高
		nullptr, //親ウィンドウハンドル
		nullptr, //メニューハンドル
		wc.hInstance, //インスタンスハンドル
		nullptr //追加パラメータ
	);
	assert(hwnd != nullptr);

#ifdef _DEBUG

	Microsoft::WRL::ComPtr<ID3D12Debug1> debugController = nullptr;
	if ( SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debugController))) )
	{
		//デバッグレイヤーを有効化する
		debugController->EnableDebugLayer();
		// さらにGPU側でもチェックを行うようにする
		debugController->SetEnableGPUBasedValidation(TRUE);
	}

#endif


	//ウィンドウの表示
	ShowWindow(hwnd, SW_SHOW);

	MSG msg{};

	//DXGIファクトリーの生成
	Microsoft::WRL::ComPtr<IDXGIFactory7> dxgiFactory = nullptr;
	// HRESULTはWindows系のエラーコードであり、
	//関数が成功したかどうかをSUCCEEDEDマクロで判定できる
	HRESULT hr = CreateDXGIFactory(IID_PPV_ARGS(&dxgiFactory));
	//初期化の根本的な部分でエラーが出た場合はプログラムが間違っているか、どうかにもできない場合が多いのでassertにしておく
	assert(SUCCEEDED(hr));

	// 使用するアダプタ用の変数。最初にはnullptrを入れておく
	Microsoft::WRL::ComPtr<IDXGIAdapter4> useAdapter = nullptr;
	// 良い順にアダプタを頼む
	for ( UINT i = 0; dxgiFactory->EnumAdapterByGpuPreference(i, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE, IID_PPV_ARGS(&useAdapter)) != DXGI_ERROR_NOT_FOUND; ++i )
	{
		//アダプターの情報を取得する
		DXGI_ADAPTER_DESC3 adapterDesc{};
		hr = useAdapter->GetDesc3(&adapterDesc);
		assert(SUCCEEDED(hr)); //取得できないのは一大事
		//ソフトウェアアダプタでなければ採用!
		if ( !( adapterDesc.Flags & DXGI_ADAPTER_FLAG3_SOFTWARE ) )
		{
			//採用したアダプタの情報をログに出力。wstringの方なので注意

			Log(ConvertString(std::format(L"Use Adapter :{}\n", adapterDesc.Description)));
			break;
		}
		useAdapter = nullptr; //ソフトウェアアダプタの場合は見なかったことにする
	}
	// 適切なアダプタが見つからなかったので起動できない
	assert(useAdapter != nullptr);

	Microsoft::WRL::ComPtr <ID3D12Device> device = nullptr;
	// 機能レベルとログ出力用の変数の文字列
	D3D_FEATURE_LEVEL featureLevels[] = {
		D3D_FEATURE_LEVEL_12_2,
		D3D_FEATURE_LEVEL_12_1,
		D3D_FEATURE_LEVEL_12_0
	};
	const char* featureLevelStrings[] = { "12.2", "12.1", "12.0" };
	//高い順に生成できるか試していく
	for ( size_t i = 0; i < std::size(featureLevels); ++i )
	{
		//採用したアダプターでデバイスを生成
		hr = D3D12CreateDevice(useAdapter.Get(), featureLevels[i], IID_PPV_ARGS(&device));
		// 指定した機能レベルでデバイスが生成できたかを確認
		if ( SUCCEEDED(hr) )
		{
			//生成できたのでログ出力を行ってループを抜ける
			Log(std::format("Feature Level : {}\n", featureLevelStrings[i]));
			break;
		}
	}
	// デバイスの生成がうまくいかなかったので起動できない
	assert(device != nullptr);

	// コマンドキューを生成する
	Microsoft::WRL::ComPtr<ID3D12CommandQueue> commandQueue = nullptr;
	D3D12_COMMAND_QUEUE_DESC commandQueueDesc{};
	hr = device->CreateCommandQueue(&commandQueueDesc, IID_PPV_ARGS(&commandQueue));
	// コマンドキューの生成がうまくいかなかったので起動できない
	assert(SUCCEEDED(hr));

	//コマンドアロケータを生成する
	Microsoft::WRL::ComPtr<ID3D12CommandAllocator> commandAllocator = nullptr;
	hr = device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&commandAllocator));
	// コマンドアロケータの生成がうまくいかなかったので起動できない
	assert(SUCCEEDED(hr));

	// コマンドリストを生成する
	Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> commandList = nullptr;
	hr = device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, commandAllocator.Get(), nullptr, IID_PPV_ARGS(&commandList));
	//コマンドリストの生成がうまくいなかなかったので起動できない
	assert(SUCCEEDED(hr));

	//スワップチェーンを生成する
	Microsoft::WRL::ComPtr<IDXGISwapChain4> swapChain = nullptr;
	DXGI_SWAP_CHAIN_DESC1 swapChainDesc{};
	swapChainDesc.Width = kClientWidth; //画面の幅。ウィンドウのクライアント領域を同じものにしておく
	swapChainDesc.Height = kClientHeight; //画面の高さ。ウィンドウのクライアント領域を同じものにしておく
	swapChainDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM; //色の形式
	swapChainDesc.SampleDesc.Count = 1; //マルチサンプリングしない
	swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT; // 描画のターゲットとして利用する
	swapChainDesc.BufferCount = 2; //ダブルバッファ
	swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD; //モニタにうつしたら、中身を破棄
	//コマンドキュー、ウィンドウハンドル、設定を渡して生成する
	hr = dxgiFactory->CreateSwapChainForHwnd(commandQueue.Get(), hwnd, &swapChainDesc, nullptr, nullptr, reinterpret_cast< IDXGISwapChain1** >( swapChain.GetAddressOf() ));
	assert(SUCCEEDED(hr));

	//ディスクリプタヒープの生成
	// RTV用のヒープでディスクリプタの数は2。RTVはShader内で触るものではないので、ShaderVisibleはfalse
	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> rtvDescriptorHeap = CreateDescriptorHeap(device.Get(), D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 2, false);
	// SRV用のヒープでディスクリプタの数は128。SRVはShader内で触るものなので、ShaderVisibleはtrue
	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> srvDescriptorHeap = CreateDescriptorHeap(device.Get(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 128, true);

	// DepthStencilTextureをウィンドウのサイズで作成
	ResourceObject depthStencilResource = CreateDepthStencilTextureResource(device.Get(), kClientWidth, kClientHeight);

	// DSV用のヒープでディスクリプタの数は1。DSVはShader内で触るものではないので、ShaderVisibleはfalse
	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> dsvDescriptorHeap = CreateDescriptorHeap(device.Get(), D3D12_DESCRIPTOR_HEAP_TYPE_DSV, 1, false);
	//DSVの設定
	D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc{};
	dsvDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT; // Format。基本的にはResourceに合わせる
	dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D; // 2Dテクスチャとして使う
	// DSVHeapの先頭にDSVを作る
	device->CreateDepthStencilView(depthStencilResource.Get(), &dsvDesc, dsvDescriptorHeap->GetCPUDescriptorHandleForHeapStart());

	//// ディスクリプタヒープが作れなかったので起動できない
	assert(SUCCEEDED(hr));

	//SwapChainからResourceを引っ張ってくる
	Microsoft::WRL::ComPtr<ID3D12Resource> swapChainResources[2] = { nullptr };
	hr = swapChain->GetBuffer(0, IID_PPV_ARGS(&swapChainResources[0]));
	//うまく取得できなければ起動できない
	assert(SUCCEEDED(hr));
	hr = swapChain->GetBuffer(1, IID_PPV_ARGS(&swapChainResources[1]));
	assert(SUCCEEDED(hr));

	//RTVの設定
	D3D12_RENDER_TARGET_VIEW_DESC rtvDesc{};
	rtvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB; //出力結果をSGRBに変換して書き込み
	rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D; //2Dテクスチャとして書き込む
	//ディスクリㇷプタの先頭を取得する
	D3D12_CPU_DESCRIPTOR_HANDLE rtvStartHandle = rtvDescriptorHeap->GetCPUDescriptorHandleForHeapStart();
	// RTVを２つ作るのでディスクリプタを２つ用意
	D3D12_CPU_DESCRIPTOR_HANDLE rtvHandles[2];
	// まず１つ目を作る。１つ目は最初のところに作る。作る場所をこちらで指定してあげる必要がある
	rtvHandles[0] = rtvStartHandle;
	device->CreateRenderTargetView(swapChainResources[0].Get(), &rtvDesc, rtvHandles[0]);
	// 2つ目のディスクリプタハンドルを得る(自力で）
	rtvHandles[1].ptr = rtvHandles[0].ptr + device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
	// 2つ目を作る
	device->CreateRenderTargetView(swapChainResources[1].Get(), &rtvDesc, rtvHandles[1]);

	//初期値0でFenceを作る
	Microsoft::WRL::ComPtr<ID3D12Fence> fence = nullptr;
	uint64_t fenceValue = 0;
	hr = device->CreateFence(fenceValue, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence));
	assert(SUCCEEDED(hr));

	//FenceのSignalを持つためのイベントを作成する
	HANDLE fenceEvent = CreateEvent(NULL, FALSE, FALSE, NULL);
	assert(fenceEvent != nullptr);

	// dxcCompilerを初期化
	Microsoft::WRL::ComPtr <IDxcUtils> dxcUtils = nullptr;
	Microsoft::WRL::ComPtr <IDxcCompiler3> dxcCompiler = nullptr;
	hr = DxcCreateInstance(CLSID_DxcUtils, IID_PPV_ARGS(&dxcUtils));
	assert(SUCCEEDED(hr));
	hr = DxcCreateInstance(CLSID_DxcCompiler, IID_PPV_ARGS(&dxcCompiler));
	assert(SUCCEEDED(hr));

	// 現時点でincludeはしないでが、includeに対応するための設定を行っておく
	Microsoft::WRL::ComPtr<IDxcIncludeHandler> includeHandler = nullptr;
	hr = dxcUtils->CreateDefaultIncludeHandler(&includeHandler);
	assert(SUCCEEDED(hr));

	//RootSignature作成
	D3D12_ROOT_SIGNATURE_DESC descriptionRootSignature{};
	descriptionRootSignature.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

	D3D12_DESCRIPTOR_RANGE descriptorRange[1] = {};
	descriptorRange[0].BaseShaderRegister = 0; // レジスタ番号0から
	descriptorRange[0].NumDescriptors = 1; // 1個
	descriptorRange[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV; // SRV
	descriptorRange[0].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND; // 自動で割り当て

	//RootParameterを作成
	D3D12_ROOT_PARAMETER rootParameters[4] = {};
	rootParameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV; // CBVを使う
	rootParameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL; // PixelShader使う
	rootParameters[0].Descriptor.ShaderRegister = 0; // レジスタ番号0とバインド

	rootParameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV; // CBVを使う
	rootParameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX; // VertexShader使う
	rootParameters[1].Descriptor.ShaderRegister = 0; // レジスタ番号0とバインド

	rootParameters[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE; // DescriptorTableを使う
	rootParameters[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL; // PixelShaderで使う
	rootParameters[2].DescriptorTable.pDescriptorRanges = descriptorRange; // 範囲の配列へのポインタ
	rootParameters[2].DescriptorTable.NumDescriptorRanges = _countof(descriptorRange); // 範囲の数

	rootParameters[3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV; // CBVを使う
	rootParameters[3].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL; // PixelShaderで使う
	rootParameters[3].Descriptor.ShaderRegister = 1; // レジスタを番号1を使う

	descriptionRootSignature.pParameters = rootParameters; // ルートパラメータ配列のポインタ
	descriptionRootSignature.NumParameters = _countof(rootParameters); // 配列の長さ


	D3D12_STATIC_SAMPLER_DESC staticSamplers[1] = {};
	staticSamplers[0].Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR; // バイリニアフィルタ
	staticSamplers[0].AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP; // U方向は繰り返し
	staticSamplers[0].AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP; // V方向は繰り返し
	staticSamplers[0].AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP; // W方向は繰り返し
	staticSamplers[0].ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER; // 比較しない
	staticSamplers[0].MaxLOD = D3D12_FLOAT32_MAX; // ありったけのMipmapを使う
	staticSamplers[0].ShaderRegister = 0; // レジスタ番号0とバインド
	staticSamplers[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL; // PixelShaderで使う
	descriptionRootSignature.pStaticSamplers = staticSamplers;
	descriptionRootSignature.NumStaticSamplers = _countof(staticSamplers);

	// シリアライズしてバイナリにする
	Microsoft::WRL::ComPtr<ID3DBlob> signatureBlob = nullptr;
	Microsoft::WRL::ComPtr<ID3DBlob> errorBlob = nullptr;
	hr = D3D12SerializeRootSignature(&descriptionRootSignature, D3D_ROOT_SIGNATURE_VERSION_1, &signatureBlob, &errorBlob);
	if ( FAILED(hr) )
	{
		Log(reinterpret_cast< char* >( errorBlob->GetBufferPointer() ));
		assert(false);
	}
	//バイナリを元に生成
	Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature = nullptr;
	hr = device->CreateRootSignature(0, signatureBlob->GetBufferPointer(), signatureBlob->GetBufferSize(), IID_PPV_ARGS(&rootSignature));
	assert(SUCCEEDED(hr));

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

	//BlendStateの設定
	D3D12_BLEND_DESC blendDesc{};
	//すべての色要素を書き込む
	blendDesc.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
	blendDesc.RenderTarget[0].BlendEnable = TRUE;

	//RGB
	blendDesc.RenderTarget[0].SrcBlend = D3D12_BLEND_SRC_ALPHA; // ソースのアルファ値
	blendDesc.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD; // 加算
	blendDesc.RenderTarget[0].DestBlend = D3D12_BLEND_INV_SRC_ALPHA; // 1.0f - ソースのアルファ

	// 
	blendDesc.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE; // ソースのアルファ値
	blendDesc.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
	blendDesc.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_ZERO; // 0.0f



	// RasterizerStateの設定
	D3D12_RASTERIZER_DESC rasterizerDesc{};
	//裏面(時計回り)を標示しない
	rasterizerDesc.CullMode = D3D12_CULL_MODE_NONE;
	// 三角形の中を塗りつぶす	
	rasterizerDesc.FillMode = D3D12_FILL_MODE_SOLID;

	//DepthStencilStateの設定
	D3D12_DEPTH_STENCIL_DESC depthStencilDesc{};
	// Depthの機能を有効かする
	depthStencilDesc.DepthEnable = true;
	//書き込みします
	depthStencilDesc.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
	// 比較関数はLessEqual。つまり、近ければ描画させる
	depthStencilDesc.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;

	// Shaderをコンパイルする
	Microsoft::WRL::ComPtr<IDxcBlob> vertexShaderBlob = CompileShader(L"Object3D.VS.hlsl", L"vs_6_0", dxcUtils.Get(), dxcCompiler.Get(), includeHandler.Get());
	assert(vertexShaderBlob != nullptr);
	Microsoft::WRL::ComPtr<IDxcBlob> pixelShaderBlob = CompileShader(L"Object3D.PS.hlsl", L"ps_6_0", dxcUtils.Get(), dxcCompiler.Get(), includeHandler.Get());
	assert(pixelShaderBlob != nullptr);

	// PSO
	D3D12_GRAPHICS_PIPELINE_STATE_DESC graphicsPipelineStateDesc{};
	graphicsPipelineStateDesc.pRootSignature = rootSignature.Get(); //RootSignature
	graphicsPipelineStateDesc.InputLayout = inputLayoutDesc; // InputLayout
	graphicsPipelineStateDesc.VS = { vertexShaderBlob->GetBufferPointer(), vertexShaderBlob->GetBufferSize() }; //VertexShader
	graphicsPipelineStateDesc.PS = { pixelShaderBlob->GetBufferPointer(), pixelShaderBlob->GetBufferSize() }; //PixelShader
	graphicsPipelineStateDesc.BlendState = blendDesc; // BlendState
	graphicsPipelineStateDesc.RasterizerState = rasterizerDesc; // RasterizerState
	// 書き込むRTVの情報
	graphicsPipelineStateDesc.NumRenderTargets = 1;
	graphicsPipelineStateDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
	// DepthStencilの設定
	graphicsPipelineStateDesc.DepthStencilState = depthStencilDesc;
	graphicsPipelineStateDesc.DSVFormat = DXGI_FORMAT_D24_UNORM_S8_UINT; // DepthStencilのFormat

	// 利用するトポロジー(形状)のタイプ、三角形
	graphicsPipelineStateDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	// どのように両面に色を打ち込むかの設定 (気にしなく良い)
	graphicsPipelineStateDesc.SampleDesc.Count = 1;
	graphicsPipelineStateDesc.SampleMask = D3D12_DEFAULT_SAMPLE_MASK;
	//実際に生成
	Microsoft::WRL::ComPtr<ID3D12PipelineState> graphicsPipelineState = nullptr;
	hr = device->CreateGraphicsPipelineState(&graphicsPipelineStateDesc, IID_PPV_ARGS(&graphicsPipelineState));
	assert(SUCCEEDED(hr));

# pragma region パーティクル用のPSOを作る
	ID3D12PipelineState* modelPipelineStates[static_cast< int >( ParticleBlendMode::Count_ )] = {};
	ID3D12PipelineState* particlePipelineStates[static_cast< int >( ParticleBlendMode::Count_ )] = {};

	for ( int i = 0; i < static_cast< int >(ParticleBlendMode::Count_); ++i )
	{
		D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = graphicsPipelineStateDesc;
		psoDesc.BlendState =
			CreateParticleBlendDesc(static_cast< ParticleBlendMode >(i));

		HRESULT hr = device->CreateGraphicsPipelineState(
			&psoDesc,
			IID_PPV_ARGS(&modelPipelineStates[i]));
		assert(SUCCEEDED(hr));
	}
	D3D12_DEPTH_STENCIL_DESC particleDepthDesc{};
	particleDepthDesc.DepthEnable = TRUE;
	particleDepthDesc.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
	particleDepthDesc.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;

	for ( int i = 0; i < static_cast< int >(ParticleBlendMode::Count_); ++i )
	{
		D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = graphicsPipelineStateDesc;
		psoDesc.DepthStencilState = particleDepthDesc;
		psoDesc.BlendState =
			CreateParticleBlendDesc(static_cast< ParticleBlendMode >(i));

		HRESULT hr = device->CreateGraphicsPipelineState(
			&psoDesc,
			IID_PPV_ARGS(&particlePipelineStates[i]));
		assert(SUCCEEDED(hr));
	}
#pragma endregion

	int vertexSpriteSize = 4;
	int indexSize = 6;

#pragma region ライト用のリソース作成
	ResourceObject directionalLightResource = CreateBufferResource(device.Get(), sizeof(DirectionalLight));
	DirectionalLight* directionalLight = nullptr;
	directionalLightResource.Get()->Map(0, nullptr, reinterpret_cast< void** >( &directionalLight ));
	directionalLight->color = { 1.0f, 1.0f, 1.0f, 1.0f };
	directionalLight->direction = { 0.0f, -1.0f, 0.0f };
	directionalLight->intensity = 1.0f;
#pragma endregion

#pragma region スプライトリソースデータと頂点の作り
	//Sprite用の頂点リソースを作る(4頂点分)
	ResourceObject vertexResourceSpriteQuad = CreateBufferResource(device.Get(), sizeof(VertexData) * vertexSpriteSize);
	// Sprite用のマテリアルリソースを作る
	ResourceObject materialResourceSpriteQuad = CreateBufferResource(device.Get(), sizeof(Material));
	// マテリアルにデータを書き込む
	Material* materialDataSpriteQuad = nullptr;
	// 書き込むためのアドレスを取得
	materialResourceSpriteQuad.Get()->Map(0, nullptr, reinterpret_cast< void** >( &materialDataSpriteQuad ));
	//白色。スプライトは基本的にLightingの影響を受けない
	*materialDataSpriteQuad = { { 1.0f, 1.0f, 1.0f, 1.0f } };
	materialDataSpriteQuad->lightingMode = LightingMode::HalfLambert;
	materialDataSpriteQuad->uvTransform = MakeIdentityMatrix();

	// 頂点バッファビューを作成する
	D3D12_VERTEX_BUFFER_VIEW vertexBufferViewSpriteQuad{};
	//リソースの先頭のアドレスから使う
	vertexBufferViewSpriteQuad.BufferLocation = vertexResourceSpriteQuad.Get()->GetGPUVirtualAddress();
	//しようするリソースのサイズは頂点4つ分のサイズ
	vertexBufferViewSpriteQuad.SizeInBytes = sizeof(VertexData) * vertexSpriteSize;
	//1頂点あたりのサイズ
	vertexBufferViewSpriteQuad.StrideInBytes = sizeof(VertexData);

	VertexData* vertexDataSpriteQuad = nullptr;
	vertexResourceSpriteQuad.Get()->Map(0, nullptr, reinterpret_cast< void** >( &vertexDataSpriteQuad ));

	// 4つの頂点(重複なし)。インデックスバッファで三角形2枚分を組む
	vertexDataSpriteQuad[0].position = { 0.0f, 360.0f, 0.0f, 1.0f }; //左下
	vertexDataSpriteQuad[0].texcoord = { 0.0f, 1.0f };
	vertexDataSpriteQuad[0].normal = { 0.0f, 0.0f, -1.0f };

	vertexDataSpriteQuad[1].position = { 0.0f, 0.0f, 0.0f, 1.0f }; //左上
	vertexDataSpriteQuad[1].texcoord = { 0.0f, 0.0f };
	vertexDataSpriteQuad[1].normal = { 0.0f, 0.0f, -1.0f };

	vertexDataSpriteQuad[2].position = { 640.0f, 0.0f, 0.0f, 1.0f }; //右上
	vertexDataSpriteQuad[2].texcoord = { 1.0f, 0.0f };
	vertexDataSpriteQuad[2].normal = { 0.0f, 0.0f, -1.0f };

	vertexDataSpriteQuad[3].position = { 640.0f, 360.0f, 0.0f, 1.0f }; //右下
	vertexDataSpriteQuad[3].texcoord = { 1.0f, 1.0f };
	vertexDataSpriteQuad[3].normal = { 0.0f, 0.0f, -1.0f };
#pragma endregion

#pragma region インデックスバッファの作成	(スプライト用)
	ResourceObject indexResourceSpriteQuad = CreateBufferResource(device.Get(), sizeof(uint32_t) * indexSize);

	D3D12_INDEX_BUFFER_VIEW indexBufferViewSpriteQuad{};
	// リソースの先頭のアドレスから使う
	indexBufferViewSpriteQuad.BufferLocation = indexResourceSpriteQuad.Get()->GetGPUVirtualAddress();
	// 使用するリソースのサイズはインデックス6つ分のサイズ
	indexBufferViewSpriteQuad.SizeInBytes = sizeof(uint32_t) * indexSize;
	// インデックスはuint32_tとする
	indexBufferViewSpriteQuad.Format = DXGI_FORMAT_R32_UINT;

	// インデックスリソースデータを書き込む
	// 三角形1枚目: 左下(0)→左上(1)→右下(3)
	// 三角形2枚目: 左上(1)→右上(2)→右下(3)
	uint32_t* indexDataSpriteQuad = nullptr;
	indexResourceSpriteQuad.Get()->Map(0, nullptr, reinterpret_cast< void** >( &indexDataSpriteQuad ));
	indexDataSpriteQuad[0] = 0; indexDataSpriteQuad[1] = 1; indexDataSpriteQuad[2] = 3;
	indexDataSpriteQuad[3] = 1; indexDataSpriteQuad[4] = 2; indexDataSpriteQuad[5] = 3;
#pragma endregion

	//ビューポート
	D3D12_VIEWPORT viewport{};
	//クライアント領域のサイズと一緒にして画面全体に表示
	viewport.Width = kClientWidth;
	viewport.Height = kClientHeight;
	viewport.TopLeftX = 0;
	viewport.TopLeftY = 0;
	viewport.MinDepth = 0.0f;
	viewport.MaxDepth = 1.0f;

	//シザー矩形
	D3D12_RECT scissorRect{};
	// 基本的にビューポートと同じ矩形が構成されようにする
	scissorRect.left = 0;
	scissorRect.right = kClientWidth;
	scissorRect.top = 0;
	scissorRect.bottom = kClientHeight;

	// Sprite用のTransformationMatrix用のリソースを作る
	ResourceObject transformationMatrixResourceSprite = CreateBufferResource(device.Get(), sizeof(TransformationMatrix));
	TransformationMatrix* transformationMatrixDataSprite = nullptr;
	transformationMatrixResourceSprite.Get()->Map(0, nullptr, reinterpret_cast< void** >( &transformationMatrixDataSprite ));
	transformationMatrixDataSprite->WVP = MakeIdentityMatrix();
	transformationMatrixDataSprite->World = MakeIdentityMatrix();

	bool enableSprite = false;

	// デバッグを行うカメラ
	DebugCamera debugCamera;
	// ゲームのビューを行うカメラ
	Camera camera;
	debugCamera.Initialize();
	camera.Initialize();
	TransformData spriteTransform{ { 1.0f, 1.0f, 1.0f }, { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f } };
	TransformData uvTransformSpriteQuad
	{
		{ 1.0f, 1.0f, 1.0f },
		{ 0.0f, 0.0f, 0.0f },
		{ 0.0f, 0.0f, 0.0f }
	};
	bool useDebugCamera = false;

	// DescriptorSizeを取得しておく
	const uint32_t descriptorSizeSRV = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	const uint32_t descriptorSizeRTV = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
	const uint32_t descriptorSizeDSV = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);

	// index0はImGuiのフォント用に予約されている。index1はスプライト用のuvChecker
	uint32_t nextSrvIndex = 2;

	// スプライト用のuvCheckerテクスチャを読み込む
	DirectX::ScratchImage mipImagesSprite = LoadTexture("resources/uvChecker.png");
	const DirectX::TexMetadata& metaDataSprite = mipImagesSprite.GetMetadata();
	ResourceObject textureResourceSprite = CreateTextureResource(device.Get(), metaDataSprite);
	ResourceObject intermediateResourceSprite = UploadTextureData(textureResourceSprite.Get(), mipImagesSprite, device.Get(), commandList.Get());

	D3D12_SHADER_RESOURCE_VIEW_DESC srvDescSprite{};
	srvDescSprite.Format = metaDataSprite.format;
	srvDescSprite.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDescSprite.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	srvDescSprite.Texture2D.MipLevels = UINT(metaDataSprite.mipLevels);

	D3D12_CPU_DESCRIPTOR_HANDLE textureSrvHandleCpu = GetCPUDescriptorHandle(srvDescriptorHeap.Get(), descriptorSizeSRV, 1);
	D3D12_GPU_DESCRIPTOR_HANDLE textureSrvHandleGpu = GetGPUDescriptorHandle(srvDescriptorHeap.Get(), descriptorSizeSRV, 1);
	device->CreateShaderResourceView(textureResourceSprite.Get(), &srvDescSprite, textureSrvHandleCpu);

#pragma region パーティクルテクスチャ

// パーティクル用テクスチャを読み込む
	DirectX::ScratchImage mipImagesParticle =
		LoadTexture("resources/particle.jpg");

	const DirectX::TexMetadata& metaDataParticle =
		mipImagesParticle.GetMetadata();

	ResourceObject textureResourceParticle =
		CreateTextureResource(
			device.Get(),
			metaDataParticle);

	ResourceObject intermediateResourceParticle =
		UploadTextureData(
			textureResourceParticle.Get(),
			mipImagesParticle,
			device.Get(),
			commandList.Get());

	D3D12_SHADER_RESOURCE_VIEW_DESC srvDescParticle{};
	srvDescParticle.Format = metaDataParticle.format;
	srvDescParticle.Shader4ComponentMapping =
		D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDescParticle.ViewDimension =
		D3D12_SRV_DIMENSION_TEXTURE2D;
	srvDescParticle.Texture2D.MipLevels =
		UINT(metaDataParticle.mipLevels);

	// 次に使用するSRV番号をパーティクル用に確保する
	D3D12_CPU_DESCRIPTOR_HANDLE particleTextureSrvHandleCpu =
		GetCPUDescriptorHandle(
			srvDescriptorHeap.Get(),
			descriptorSizeSRV,
			nextSrvIndex);

	D3D12_GPU_DESCRIPTOR_HANDLE particleTextureSrvHandleGpu =
		GetGPUDescriptorHandle(
			srvDescriptorHeap.Get(),
			descriptorSizeSRV,
			nextSrvIndex);

	device->CreateShaderResourceView(
		textureResourceParticle.Get(),
		&srvDescParticle,
		particleTextureSrvHandleCpu);

	++nextSrvIndex;

#pragma endregion

	// 9種類のモデル(球, 平面, 軸, うさぎ, 複数マテリアル, 複数メッシュ, スザンヌ, ティーポット, ユタティーポット)を
	// まとめて読み込んでキャッシュしておく
	std::unordered_map<ModelNode, ModelResource> modelResources;
	std::vector<ResourceObject> keepAliveIntermediates = LoadAllModelResources(
		modelResources, device.Get(), commandList.Get(), srvDescriptorHeap.Get(), descriptorSizeSRV, nextSrvIndex);

	commandList->Close();
	ID3D12CommandList* uploadCmdLists[] = { commandList.Get() };
	commandQueue->ExecuteCommandLists(1, uploadCmdLists);
	fenceValue++;
	commandQueue->Signal(fence.Get(), fenceValue);
	fence->SetEventOnCompletion(fenceValue, fenceEvent);
	WaitForSingleObject(fenceEvent, INFINITE);
	commandAllocator->Reset();
	commandList->Reset(commandAllocator.Get(), nullptr);
	// アップロード用の中間リソースはもう不要
	keepAliveIntermediates.clear();
	intermediateResourceSprite.Reset();
	intermediateResourceParticle.Reset();

	RenderObject object1;
	object1.modelNode = ModelNode::Sphere;
	InitRenderObject(object1, device.Get());

	RenderObject object2;
	object2.modelNode = ModelNode::Plane;
	object2.transform.translate = { 3.0f, 0.0f, 0.0f, 0.0f };
	InitRenderObject(object2, device.Get());

	// パーティクルエミッタを初期化する
	ParticleEmitter emitter;
	emitter.Initialize(device.Get());
	emitter.config.position = { 0.0f, 0.0f, 35.0f, 1.0f };

	ParticleBlendMode modelBlendMode = ParticleBlendMode::None;
	DrawMode currentMode = DrawMode::Model;

#ifdef USE_IMGUI
	// ImGuiの初期化。詳細はさして重要ではないので解説を省略する。
	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGui::StyleColorsDark();
	ImGui_ImplWin32_Init(hwnd);
	ImGui_ImplDX12_Init(
		device.Get(), swapChainDesc.BufferCount,
		rtvDesc.Format,
		srvDescriptorHeap.Get(),
		srvDescriptorHeap.Get()->GetCPUDescriptorHandleForHeapStart(),
		srvDescriptorHeap.Get()->GetGPUDescriptorHandleForHeapStart()
	);
	ImGuiIO& io = ImGui::GetIO();
	io.Fonts->Build();

#endif 

	Log("Complete create D3D12Device!!!\n"); //初期化完了のログを出す

	Microsoft::WRL::ComPtr<IXAudio2> xAudio2;
	IXAudio2MasteringVoice* masteringVoice;
	//xAudioエンジンのインスタンスを生成
	hr = XAudio2Create(&xAudio2, 0, XAUDIO2_DEFAULT_PROCESSOR);
	assert(SUCCEEDED(hr));
	//マスターボイスを生成
	hr = xAudio2->CreateMasteringVoice(&masteringVoice);
	assert(SUCCEEDED(hr));

	//音声読み込み
	SoundData soundData1 = SoundLoadWave("resources/WaltzOfFlowers.wav");
	// 音声再生
	SoundPlayWave(xAudio2.Get(), soundData1);

	InputManager input;
	input.Initialize(hwnd, wc.hInstance);

#ifdef _DEBUG
	Microsoft::WRL::ComPtr<ID3D12InfoQueue> infoQueue = nullptr;
	if ( SUCCEEDED(device->QueryInterface(IID_PPV_ARGS(&infoQueue))) )
	{
		//ヤバいエラー時に止まる
		infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_CORRUPTION, true);
		//エラーの時に止まる
		infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_ERROR, true);
		//警告時に止まる
		infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_WARNING, true);

		// 抑制するメッセージのID
		D3D12_MESSAGE_ID denyIds[] = {
			// Windows11でのDXGIデバッグレイヤーとDX12デバッグレイヤーの相互作用バグによるエラーメッセージ
			//https://stackoverflow.com/questions/69885245/directx-12-application-is-crashing-in-windows-11
			D3D12_MESSAGE_ID_RESOURCE_BARRIER_MISMATCHING_COMMAND_LIST_TYPE
		};

		//抑制するレベル
		D3D12_MESSAGE_SEVERITY severities[] = { D3D12_MESSAGE_SEVERITY_INFO };
		D3D12_INFO_QUEUE_FILTER filter{};
		filter.DenyList.NumIDs = _countof(denyIds);
		filter.DenyList.pIDList = denyIds;
		filter.DenyList.NumSeverities = _countof(severities);
		filter.DenyList.pSeverityList = severities;
		//指定したメッセージの標示を抑制する
		infoQueue->PushStorageFilter(&filter);
		//ComPtrを使ってるから解放は無視
	}

#endif
	while ( msg.message != WM_QUIT )
	{

		// メッセージがあるか確認
		if ( PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE) )
		{
			TranslateMessage(&msg);
			DispatchMessage(&msg);
		} else
		{
			input.Update();

			if ( input.TriggerKey(DIK_0) )
			{
				OutputDebugStringA("Hit 0\n");
			}

			//これから書き込みバックァのインデックスを取得
			UINT backBufferIndex = swapChain->GetCurrentBackBufferIndex();

			//2026/05/13 
			D3D12_RESOURCE_BARRIER barrier{};
			//今回のバリアはTransition
			barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
			// Noneにしておく
			barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
			// バリアを張る対象のリソース。現在のバックパッファに対して行う
			barrier.Transition.pResource = swapChainResources[backBufferIndex].Get();
			//遷移前(現在)のResourceState
			barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
			// 遷移後のResourceState
			barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;

			// TransitionBarrierを張る
			commandList->ResourceBarrier(1, &barrier);

			//描画先のRTVとDSVを設定する
			D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle = dsvDescriptorHeap->GetCPUDescriptorHandleForHeapStart();
			// 描画先のRTVを設定する
			commandList->OMSetRenderTargets(1, &rtvHandles[backBufferIndex], FALSE, &dsvHandle);
			//指定した色で画面全体をクリアする
			float clearColor[] = { 0.1f, 0.25f, 0.5f, 1.0f }; //青っぽい色。RGBAの順
			commandList->ClearRenderTargetView(rtvHandles[backBufferIndex], clearColor, 0, nullptr);
			//指定した深度で画面全体をクリアする
			commandList->ClearDepthStencilView(dsvHandle, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);

			// カメラは共通で1つ。Update()はview/projectionを更新するために呼ぶだけで、
			// worldMatrix引数はここでは使わない(各オブジェクトのWVPは自前で組み立てるため)。
			// GetViewProjectionMatrix()はworldMatrixに依存せずview*projectionだけを返してくれる。
			// --- カメラ更新 --- //
			if ( input.TriggerKey(DIK_F1) )
			{
				useDebugCamera = !useDebugCamera;
			}

			Matrix4x4 viewMatrix = MakeIdentityMatrix();
			Matrix4x4 projectionMatrix = MakeIdentityMatrix();
			Matrix4x4 viewProjection = MakeIdentityMatrix();
			if ( useDebugCamera )
			{
				debugCamera.Update(MakeIdentityMatrix(), input);
				viewMatrix = debugCamera.GetViewMatrix();
				projectionMatrix = debugCamera.GetProjectionMatrix();
			} else
			{
				camera.Update(MakeIdentityMatrix());
				viewMatrix = camera.GetViewMatrix();
				projectionMatrix = camera.GetProjectionMatrix();
			}
			viewProjection = Multiply(viewMatrix, projectionMatrix);

			// --- オブジェクト更新 --- //
			if ( currentMode == DrawMode::Model )
			{
				UpdateRenderObject(object1, viewProjection);
				UpdateRenderObject(object2, viewProjection);
			} else
			{
				// パーティクルを更新する
				float deltaTime = 1.0f / 60.0f;
				emitter.Update(deltaTime);
			}

			// Sprite用のWorldViewProjectionMatrixを作る
			Matrix4x4 worldMatrixSprite = MakeAffineMatrix(spriteTransform.scale, spriteTransform.rotate, spriteTransform.translate);
			Matrix4x4 viewMatrixSprite = MakeIdentityMatrix(); // スプライトはカメラの影響を受けないので、ビュー行列は単位行列にする
			Matrix4x4 projectionMatrixSprite = MakeOrthoGraphicMatrix(0.0f, 0.0f, static_cast< float >( kClientWidth ), static_cast< float >( kClientHeight ), 0.0f, 100.0f);
			Matrix4x4 worldViewProjectionMatrixSprite = Multiply(worldMatrixSprite, Multiply(viewMatrixSprite, projectionMatrixSprite));
			transformationMatrixDataSprite->WVP = worldViewProjectionMatrixSprite;
			transformationMatrixDataSprite->World = worldMatrixSprite;

			Matrix4x4 uvTransformMatrixQuad = MakeScaleMatrix(uvTransformSpriteQuad.scale);
			uvTransformMatrixQuad = Multiply(uvTransformMatrixQuad, MakeRotateZMatrix(uvTransformSpriteQuad.rotate.z));
			uvTransformMatrixQuad = Multiply(uvTransformMatrixQuad, MakeTranslateMatrix(uvTransformSpriteQuad.translate));
			materialDataSpriteQuad->uvTransform = uvTransformMatrixQuad;

			// --- オブジェクト描画 --- //
			commandList->RSSetViewports(1, &viewport);
			commandList->RSSetScissorRects(1, &scissorRect);
			commandList->SetGraphicsRootSignature(rootSignature.Get());
			commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
			// 描画用のDescriptorHeapを設定する
			ID3D12DescriptorHeap* descriptorHeaps[] = { srvDescriptorHeap.Get() };
			commandList->SetDescriptorHeaps(1, descriptorHeaps);
			if ( currentMode == DrawMode::Model )
			{
				// ------------------------------------------------
				// モデル描画
				// ------------------------------------------------
				commandList->SetPipelineState(modelPipelineStates[ static_cast< int >(modelBlendMode)]);
				DrawRenderObject(commandList.Get(), object1, modelResources, directionalLightResource.Get());
				DrawRenderObject(commandList.Get(), object2, modelResources, directionalLightResource.Get());
			} else
			{
				emitter.Draw(commandList.Get(), viewMatrix, projectionMatrix, particleTextureSrvHandleGpu, particlePipelineStates);
			}

			commandList->SetPipelineState(graphicsPipelineState.Get());
			// --- スプライトの描画 --- //
			commandList->IASetVertexBuffers(0, 1, &vertexBufferViewSpriteQuad); // VBVを設定
			commandList->SetGraphicsRootConstantBufferView(0, materialResourceSpriteQuad.Get()->GetGPUVirtualAddress());
			commandList->SetGraphicsRootConstantBufferView(1, transformationMatrixResourceSprite.Get()->GetGPUVirtualAddress());
			// SpriteはuvCheckerテクスチャを使う
			commandList->SetGraphicsRootDescriptorTable(2, textureSrvHandleGpu);
			commandList->IASetIndexBuffer(&indexBufferViewSpriteQuad);
			if ( enableSprite )
			{
				commandList->DrawIndexedInstanced(6, 1, 0, 0, 0);
			}

#ifdef USE_IMGUI
			ImGui_ImplDX12_NewFrame();
			ImGui_ImplWin32_NewFrame();
			ImGui::NewFrame();
			//開発用UIの処理。実際に開発用のUIを出す場合はここをゲーム固有の処理に置き換える
			ImGui::ShowDemoWindow();
			ImGui::Begin("Debug");

			EnableCamera(useDebugCamera, camera, debugCamera);
			ImGui::Separator();

			ImGui::Text("Draw Mode");

			if ( ImGui::RadioButton("Model", currentMode == DrawMode::Model) )
			{
				currentMode = DrawMode::Model;
			}
			ImGui::SameLine();
			if ( ImGui::RadioButton("Particles", currentMode == DrawMode::Particles) )
			{
				currentMode = DrawMode::Particles;
			}

			if ( currentMode == DrawMode::Model )
			{
				ImGui::Text("Model");
				int modelBlendModeIndex = static_cast< int >( modelBlendMode );
				ImGui::Combo("Blend Mode", &modelBlendModeIndex, "None\0Normal\0Add\0Subtract\0Multiply\0Screen\0Exclusion\0");
				// モデルのブレンドモードを変更する
				{
				modelBlendMode = static_cast< ParticleBlendMode >( modelBlendModeIndex );
				}

				DrawRenderObjectImGui( "Object 1", object1);
				ImGui::Separator();
				DrawRenderObjectImGui( "Object 2", object2);
				ImGui::Separator();
				ImGui::Text( "Directional Light Settings"); 
				ImGui::ColorEdit4( "Light Color", &directionalLight->color.x);
				ImGui::DragFloat3( "Light Direction", &directionalLight->direction.x, 0.1f);
				ImGui::DragFloat( "Light Intensity", &directionalLight->intensity, 0.1f);

#pragma region Sprite
				ImGui::Separator();
				if ( ImGui::Button("Enable Sprite") )
				{
					enableSprite = true;
				}
				if ( ImGui::Button("Disable Sprite") )
				{
					enableSprite = false;
				}
				if ( enableSprite )
				{
					ImGui::Text("Sprite Settings");
					ImGui::DragFloat3("Sprite Scale", &spriteTransform.scale.x, 0.1f);
					ImGui::DragFloat3("Sprite Rotate", &spriteTransform.rotate.x, 0.1f);
					ImGui::DragFloat3("Sprite Translate", &spriteTransform.translate.x, 0.1f);
					ImGui::DragFloat2("UV Translate", &uvTransformSpriteQuad.translate.x, 0.1f, -10.0f, 10.0f);
					ImGui::DragFloat2("UV Scale", &uvTransformSpriteQuad.scale.x, 0.1f, -10.0f, 10.0f);
					ImGui::SliderAngle("UV Rotate", &uvTransformSpriteQuad.rotate.z);
				}
#pragma endregion
			}
			if ( currentMode == DrawMode::Particles )
			{
				ImGui::Separator();
				ImGui::Text("Particle Emitter");
				int blendMode = static_cast< int >( emitter.config.blendMode );
				if ( ImGui::Combo("Blend Mode", &blendMode, "None\0" "Normal\0" "Add\0" "Subtract\0" "Multiply\0" "Screen\0" "Exclusion\0") )
				{
					emitter.config.blendMode = static_cast< ParticleBlendMode >( blendMode );
				}

				ImGui::DragFloat3("Emitter Position", &emitter.config.position.x, 0.1f);
				ImGui::ColorEdit4("Color Start", &emitter.config.colorStart.x);
				ImGui::ColorEdit4("Color End", &emitter.config.colorEnd.x);
				ImGui::DragFloat("Emit Rate", &emitter.config.emitRate, 0.5f, 0.1f, 256.0f);
				ImGui::DragFloat("Lifetime Min", &emitter.config.lifetimeMin, 0.1f, 0.1f, 60.0f);
				ImGui::DragFloat( "Lifetime Max", &emitter.config.lifetimeMax, 0.1f, 0.1f, 60.0f);
				ImGui::DragFloat( "Speed Min", &emitter.config.speedMin, 0.1f, 0.0f, 20.0f);
				ImGui::DragFloat( "Speed Max", &emitter.config.speedMax, 0.1f, 0.0f, 20.0f);
				ImGui::DragFloat( "Size Start", &emitter.config.sizeStart, 0.01f, 0.0f, 10.0f);
				ImGui::DragFloat( "Size End", &emitter.config.sizeEnd, 0.01f, 0.0f, 10.0f);
				ImGui::Checkbox( "Looping", &emitter.config.looping);
				if ( ImGui::Button("Burst (64)") )
				{
					emitter.Emit(64);
				}
			}

			ImGui::End();
			//ImGuiの内部コマンドを生成する
			ImGui::Render();
			// 実際のcommandListのImGuiの描画コマンドを積む
			ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), commandList.Get());
#endif

			//画面に描く処理はすべて終わり、画面に映すので、状態を遷移
			//今回はRenderTargetからPresentにする
			barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
			barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
			//TransitionBarrierを張る
			commandList->ResourceBarrier(1, &barrier);

			// コマンドリストの内容を確定させる。すべてのコマンドを積んでからCloseすること
			hr = commandList->Close();
			assert(SUCCEEDED(hr));

			//GPUにコマンドリストの実行を行わせる
			ID3D12CommandList* commandLists[] = { commandList.Get() };
			commandQueue->ExecuteCommandLists(1, commandLists);
			//GPUとOSに画面の交換を行うように通知する
			swapChain->Present(1, 0); //垂直同期ありで画面を交換する
			// Fenceの値を更新
			fenceValue++;
			// GPUがここまでたどり着いた時に、Fenceの値を指定した値を代入するようにSignalを送る
			commandQueue->Signal(fence.Get(), fenceValue);

			// Fenceの値が指定したSignal値にたどり着いてるか確認する
			if ( fence->GetCompletedValue() < fenceValue )
			{
				// 指定したSignalにたどり着いていないので、たどり着くまで待つようにイベントを設定する
				fence->SetEventOnCompletion(fenceValue, fenceEvent);
				// イベント待つ
				WaitForSingleObject(fenceEvent, INFINITE);
			}
			//次のフレーム用のコマンドリストを準備
			hr = commandAllocator->Reset();
			assert(SUCCEEDED(hr));
			hr = commandList->Reset(commandAllocator.Get(), nullptr);
			assert(SUCCEEDED(hr));
		}
	}
	commandQueue->Signal(fence.Get(), ++fenceValue);
	fence->SetEventOnCompletion(fenceValue, fenceEvent);
	WaitForSingleObject(fenceEvent, INFINITE);

	// パーティクル用PSOを解放する
	for ( int i = 0; i < static_cast< int >(ParticleBlendMode::Count_); ++i )
	{
		if ( modelPipelineStates[i] )
		{
			modelPipelineStates[i]->Release();
			modelPipelineStates[i] = nullptr;
		}

		if ( particlePipelineStates[i] )
		{
			particlePipelineStates[i]->Release();
			particlePipelineStates[i] = nullptr;
		}
	}

	emitter.Shutdown();

#ifdef USE_IMGUI
	ImGui_ImplDX12_Shutdown();
	ImGui_ImplWin32_Shutdown();
	ImGui::DestroyContext();
#endif
	// gpuが処理を終えるのを待ってからリソースを解放する
	CloseHandle(fenceEvent);

	keepAliveIntermediates.clear();
	intermediateResourceSprite.Reset();

	//XAudio2解放
	xAudio2.Reset();
	//音声委データ解放
	SoundUnload(&soundData1);

	CoUninitialize(); //COMの終了処理
	//ImGuiの終了処理。詳細はさひて重要ではないので解決は省略する。
	//こういもんである。初期化と逆順に行う
	CloseWindow(hwnd);
	return 0;
}