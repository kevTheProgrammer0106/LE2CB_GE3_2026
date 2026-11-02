#include "DirectXCommon.h"

#include <cassert>
#include "externals/DirectXTex/d3dx12.h" // UpdateSubresources / GetRequiredIntermediateSize

#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxgi.lib")
// Debug用のあれやこれやを使えるようにする
#pragma comment(lib, "Dbghelp.lib")
#pragma comment(lib, "dxguid.lib")
#pragma comment(lib, "dxcompiler.lib")

#ifdef USE_IMGUI
#include "externals/imgui/imgui.h"
#include "externals/imgui/imgui_impl_dx12.h"
#include "externals/imgui/imgui_impl_win32.h"
#endif

using namespace Microsoft::WRL;

//==================================================
// Logger / StringUtility
//==================================================
#pragma region Namespaces
namespace Logger
{
	void Log(const std::string& message)
	{
		OutputDebugStringA(message.c_str());
	}
}

namespace StringUtility
{
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
}
#pragma endregion

//==================================================
// ライフサイクル
//==================================================
DirectXCommon::~DirectXCommon()
{
	// GPUが使っている間にリソースを壊さないよう、まず待つ
	if ( fence )
	{
		WaitForGpu();
	}

#ifdef USE_IMGUI
	if ( imguiInitialized )
	{
		ImGui_ImplDX12_Shutdown();
		ImGui_ImplWin32_Shutdown();
		ImGui::DestroyContext();
		imguiInitialized = false;
	}
#endif

	if ( fenceEvent )
	{
		CloseHandle(fenceEvent);
		fenceEvent = nullptr;
	}
}

void DirectXCommon::Initialize(WinApp* winApp)
{
	// NULL検出
	assert(winApp);
	// メンバ変数に記録
	this->winApp = winApp;

	InitializeDevice();
	InitializeCommands();
	InitializeSwapChain();
	InitializeDescriptorHeap();
	InitializeRenderTargetView();
	InitializeDepthBuffer();
	InitializeDepthStencilView();
	InitializeFence();
	InitializeViewportRectangle();
	InitializeScissoring();
	InitializeDxcCompiler();
	InitializeImGui();

	Logger::Log("Complete create D3D12Device!!!\n");
}

//==================================================
// 初期化
//==================================================
#pragma region DirectXCommonの初期化
void DirectXCommon::InitializeDevice()
{
	HRESULT hr;

#ifdef _DEBUG
	// デバッグレイヤーはデバイス生成の前に有効化する必要がある
	ComPtr<ID3D12Debug1> debugController;
	if ( SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debugController))) )
	{
		debugController->EnableDebugLayer();
		// さらにGPU側でもチェックを行いたい場合は有効にする(重い)
		// debugController->SetEnableGPUBasedValidation(TRUE);
	}
#endif

	// HRESULTはWindows系のエラーコードであり、
	// 関数が成功したかどうかをSUCCEEDEDマクロで判定できる
	hr = CreateDXGIFactory(IID_PPV_ARGS(&dxgiFactory));
	// 初期化の根本的な部分でエラーが出た場合はプログラムが間違っているか、どうにもできない場合が多いのでassertにしておく
	assert(SUCCEEDED(hr));

	// 良い順にアダプタを頼む
	for ( UINT i = 0; dxgiFactory->EnumAdapterByGpuPreference(i, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE, IID_PPV_ARGS(&useAdapter)) != DXGI_ERROR_NOT_FOUND; ++i )
	{
		// アダプターの情報を取得する
		DXGI_ADAPTER_DESC3 adapterDesc{};
		hr = useAdapter->GetDesc3(&adapterDesc);
		assert(SUCCEEDED(hr)); // 取得できないのは一大事
		// ソフトウェアアダプタでなければ採用!
		if ( !( adapterDesc.Flags & DXGI_ADAPTER_FLAG3_SOFTWARE ) )
		{
			Logger::Log(StringUtility::ConvertString(std::format(L"Use Adapter :{}\n", adapterDesc.Description)));
			break;
		}
		useAdapter = nullptr; // ソフトウェアアダプタの場合は見なかったことにする
	}
	// 適切なアダプタが見つからなかったので起動できない
	assert(useAdapter != nullptr);

	// 機能レベルとログ出力用の文字列
	D3D_FEATURE_LEVEL featureLevels[] = {
		D3D_FEATURE_LEVEL_12_2,
		D3D_FEATURE_LEVEL_12_1,
		D3D_FEATURE_LEVEL_12_0
	};
	const char* featureLevelStrings[] = { "12.2", "12.1", "12.0" };
	// 高い順に生成できるか試していく
	for ( size_t i = 0; i < std::size(featureLevels); ++i )
	{
		hr = D3D12CreateDevice(useAdapter.Get(), featureLevels[i], IID_PPV_ARGS(&device));
		if ( SUCCEEDED(hr) )
		{
			Logger::Log(std::format("Feature Level : {}\n", featureLevelStrings[i]));
			break;
		}
	}
	// デバイスの生成がうまくいかなかったので起動できない
	assert(device != nullptr);

#ifdef _DEBUG
	ComPtr<ID3D12InfoQueue> infoQueue;
	if ( SUCCEEDED(device->QueryInterface(IID_PPV_ARGS(&infoQueue))) )
	{
		// ヤバいエラー時に止まる
		infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_CORRUPTION, true);
		// エラーの時に止まる
		infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_ERROR, true);
		// 警告時に止まる
		infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_WARNING, true);

		// 抑制するメッセージのID
		D3D12_MESSAGE_ID denyIds[] = {
			// Windows11でのDXGIデバッグレイヤーとDX12デバッグレイヤーの相互作用バグによるエラーメッセージ
			// https://stackoverflow.com/questions/69885245/directx-12-application-is-crashing-in-windows-11
			D3D12_MESSAGE_ID_RESOURCE_BARRIER_MISMATCHING_COMMAND_LIST_TYPE
		};
		// 抑制するレベル
		D3D12_MESSAGE_SEVERITY severities[] = { D3D12_MESSAGE_SEVERITY_INFO };
		D3D12_INFO_QUEUE_FILTER filter{};
		filter.DenyList.NumIDs = _countof(denyIds);
		filter.DenyList.pIDList = denyIds;
		filter.DenyList.NumSeverities = _countof(severities);
		filter.DenyList.pSeverityList = severities;
		infoQueue->PushStorageFilter(&filter);
	}
#endif
}

void DirectXCommon::InitializeCommands()
{
	HRESULT hr;

	D3D12_COMMAND_QUEUE_DESC commandQueueDesc{};
	hr = device->CreateCommandQueue(&commandQueueDesc, IID_PPV_ARGS(&commandQueue));
	assert(SUCCEEDED(hr));

	hr = device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&commandAllocator));
	assert(SUCCEEDED(hr));

	hr = device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, commandAllocator.Get(), nullptr, IID_PPV_ARGS(&commandList));
	assert(SUCCEEDED(hr));
}

void DirectXCommon::InitializeSwapChain()
{
	swapChainDesc.Width = WinApp::kClientWidth; // ウィンドウのクライアント領域と同じにしておく
	swapChainDesc.Height = WinApp::kClientHeight;
	swapChainDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM; // 色の形式
	swapChainDesc.SampleDesc.Count = 1; // マルチサンプリングしない
	swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT; // 描画のターゲットとして利用する
	swapChainDesc.BufferCount = kBackBufferCount; // ダブルバッファ
	swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD; // モニタにうつしたら、中身を破棄

	HRESULT hr = dxgiFactory->CreateSwapChainForHwnd(
		commandQueue.Get(), winApp->GetHwnd(), &swapChainDesc, nullptr, nullptr,
		reinterpret_cast< IDXGISwapChain1** >( swapChain.GetAddressOf() ));
	assert(SUCCEEDED(hr));
}

void DirectXCommon::InitializeDescriptorHeap()
{
	// RTVはShader内で触るものではないので、ShaderVisibleはfalse
	rtvDescriptorHeap = CreateDescriptorHeap(device.Get(), D3D12_DESCRIPTOR_HEAP_TYPE_RTV, kRtvHeapSize, false);
	// SRVはShader内で触るものなので、ShaderVisibleはtrue
	srvDescriptorHeap = CreateDescriptorHeap(device.Get(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, kSrvHeapSize, true);
	// DSVはShader内で触るものではないので、ShaderVisibleはfalse
	dsvDescriptorHeap = CreateDescriptorHeap(device.Get(), D3D12_DESCRIPTOR_HEAP_TYPE_DSV, kDsvHeapSize, false);

	// ディスクリプタのサイズは一度だけ取得してキャッシュする
	descriptorSizeSRV = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	descriptorSizeRTV = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
	descriptorSizeDSV = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);
}

void DirectXCommon::InitializeRenderTargetView()
{
	rtvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB; // 出力結果をsRGBに変換して書き込み
	rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D; // 2Dテクスチャとして書き込む

	for ( uint32_t i = 0; i < kBackBufferCount; ++i )
	{
		HRESULT hr = swapChain->GetBuffer(i, IID_PPV_ARGS(&swapChainResources[i]));
		assert(SUCCEEDED(hr));

		rtvHandles[i] = GetCPUDescriptorHandle(rtvDescriptorHeap.Get(), descriptorSizeRTV, i);
		device->CreateRenderTargetView(swapChainResources[i].Get(), &rtvDesc, rtvHandles[i]);
	}
}

void DirectXCommon::InitializeDepthBuffer()
{
	// ウィンドウのサイズで深度バッファを作成する
	depthStencilResource = CreateDepthStencilTextureResource(WinApp::kClientWidth, WinApp::kClientHeight);
	assert(depthStencilResource != nullptr);
}

void DirectXCommon::InitializeDepthStencilView()
{
	D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc{};
	dsvDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT; // 基本的にはResourceに合わせる
	dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D; // 2Dテクスチャとして使う

	// DSVヒープの先頭にビューを作成する
	device->CreateDepthStencilView(depthStencilResource.Get(), &dsvDesc, dsvDescriptorHeap->GetCPUDescriptorHandleForHeapStart());
}

void DirectXCommon::InitializeFence()
{
	fenceValue = 0;
	HRESULT hr = device->CreateFence(fenceValue, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence));
	assert(SUCCEEDED(hr));

	// FenceのSignalを待つためのイベントを作成する
	fenceEvent = CreateEvent(NULL, FALSE, FALSE, NULL);
	assert(fenceEvent != nullptr);
}

void DirectXCommon::InitializeViewportRectangle()
{
	// クライアント領域のサイズと一緒にして画面全体に表示
	viewport.Width = static_cast< float >( WinApp::kClientWidth );
	viewport.Height = static_cast< float >( WinApp::kClientHeight );
	viewport.TopLeftX = 0;
	viewport.TopLeftY = 0;
	viewport.MinDepth = 0.0f;
	viewport.MaxDepth = 1.0f;
}

void DirectXCommon::InitializeScissoring()
{
	// 基本的にビューポートと同じ矩形が構成されるようにする
	scissorRect.left = 0;
	scissorRect.right = WinApp::kClientWidth;
	scissorRect.top = 0;
	scissorRect.bottom = WinApp::kClientHeight;
}

void DirectXCommon::InitializeDxcCompiler()
{
	HRESULT hr = DxcCreateInstance(CLSID_DxcUtils, IID_PPV_ARGS(&dxcUtils));
	assert(SUCCEEDED(hr));
	hr = DxcCreateInstance(CLSID_DxcCompiler, IID_PPV_ARGS(&dxcCompiler));
	assert(SUCCEEDED(hr));

	// #include("Particle.hlsli"など)に対応するためのハンドラ
	hr = dxcUtils->CreateDefaultIncludeHandler(&includeHandler);
	assert(SUCCEEDED(hr));
}

void DirectXCommon::InitializeImGui()
{
#ifdef USE_IMGUI
	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGui::StyleColorsDark();
	ImGui_ImplWin32_Init(winApp->GetHwnd());
	ImGui_ImplDX12_Init(
		device.Get(), swapChainDesc.BufferCount,
		rtvDesc.Format,
		srvDescriptorHeap.Get(),
		srvDescriptorHeap->GetCPUDescriptorHandleForHeapStart(),
		srvDescriptorHeap->GetGPUDescriptorHandleForHeapStart());
	ImGui::GetIO().Fonts->Build();
	imguiInitialized = true;
#endif
}
#pragma endregion

//==================================================
// フレーム制御
//==================================================
#pragma region フレーム制御
void DirectXCommon::TransitionBackBuffer(D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after)
{
	D3D12_RESOURCE_BARRIER barrier{};
	barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
	barrier.Transition.pResource = swapChainResources[backBufferIndex].Get();
	barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
	barrier.Transition.StateBefore = before;
	barrier.Transition.StateAfter = after;
	commandList->ResourceBarrier(1, &barrier);
}

void DirectXCommon::PreDraw()
{
	// これから書き込むバックバッファのインデックスを取得
	backBufferIndex = swapChain->GetCurrentBackBufferIndex();

	// Present -> RenderTarget
	TransitionBackBuffer(D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);

	// 描画先のRTVとDSVを設定する
	D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle = dsvDescriptorHeap->GetCPUDescriptorHandleForHeapStart();
	commandList->OMSetRenderTargets(1, &rtvHandles[backBufferIndex], FALSE, &dsvHandle);

	// 指定した色・深度で画面全体をクリアする
	const float clearColor[] = { 0.1f, 0.25f, 0.5f, 1.0f }; // 青っぽい色。RGBAの順
	commandList->ClearRenderTargetView(rtvHandles[backBufferIndex], clearColor, 0, nullptr);
	commandList->ClearDepthStencilView(dsvHandle, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);

	// ビューポート / シザー / SRVヒープ
	commandList->RSSetViewports(1, &viewport);
	commandList->RSSetScissorRects(1, &scissorRect);
	ID3D12DescriptorHeap* descriptorHeaps[] = { srvDescriptorHeap.Get() };
	commandList->SetDescriptorHeaps(1, descriptorHeaps);
}

void DirectXCommon::PostDraw()
{
	// 描画が終わったので RenderTarget -> Present
	TransitionBackBuffer(D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);

	// コマンドリストの内容を確定させる。すべてのコマンドを積んでからCloseすること
	HRESULT hr = commandList->Close();
	assert(SUCCEEDED(hr));

	// GPUにコマンドリストの実行を行わせる
	ID3D12CommandList* commandLists[] = { commandList.Get() };
	commandQueue->ExecuteCommandLists(1, commandLists);

	// GPUとOSに画面の交換を行うように通知する(垂直同期あり)
	swapChain->Present(1, 0);

	// このフレームのGPU処理が終わるまで待つ
	WaitForGpu();

	// 次のフレーム用のコマンドリストを準備
	hr = commandAllocator->Reset();
	assert(SUCCEEDED(hr));
	hr = commandList->Reset(commandAllocator.Get(), nullptr);
	assert(SUCCEEDED(hr));
}

void DirectXCommon::WaitForGpu()
{
	// GPUがここまでたどり着いた時に、Fenceに指定した値を代入するようにSignalを送る
	++fenceValue;
	commandQueue->Signal(fence.Get(), fenceValue);

	// まだたどり着いていなければ、たどり着くまでイベントで待つ
	if ( fence->GetCompletedValue() < fenceValue )
	{
		fence->SetEventOnCompletion(fenceValue, fenceEvent);
		WaitForSingleObject(fenceEvent, INFINITE);
	}
}

void DirectXCommon::FlushCommands()
{
	HRESULT hr = commandList->Close();
	assert(SUCCEEDED(hr));

	ID3D12CommandList* commandLists[] = { commandList.Get() };
	commandQueue->ExecuteCommandLists(1, commandLists);
	WaitForGpu();

	hr = commandAllocator->Reset();
	assert(SUCCEEDED(hr));
	hr = commandList->Reset(commandAllocator.Get(), nullptr);
	assert(SUCCEEDED(hr));
}
#pragma endregion

//==================================================
// ImGui
//==================================================
void DirectXCommon::BeginImGuiFrame()
{
#ifdef USE_IMGUI
	ImGui_ImplDX12_NewFrame();
	ImGui_ImplWin32_NewFrame();
	ImGui::NewFrame();
#endif
}

void DirectXCommon::RenderImGui()
{
#ifdef USE_IMGUI
	// ImGuiの内部コマンドを生成し、実際のcommandListに描画コマンドを積む
	ImGui::Render();
	ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), commandList.Get());
#endif
}

//==================================================
// ディスクリプタ
//==================================================
#pragma region ディスクリプタ
ComPtr<ID3D12DescriptorHeap> DirectXCommon::CreateDescriptorHeap(ID3D12Device* device, D3D12_DESCRIPTOR_HEAP_TYPE type, UINT numDescriptors, bool shaderVisible)
{
	ComPtr<ID3D12DescriptorHeap> descriptorHeap;
	D3D12_DESCRIPTOR_HEAP_DESC descriptorHeapDesc{};
	descriptorHeapDesc.Type = type;
	descriptorHeapDesc.NumDescriptors = numDescriptors;
	descriptorHeapDesc.Flags = shaderVisible ? D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE : D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
	HRESULT hr = device->CreateDescriptorHeap(&descriptorHeapDesc, IID_PPV_ARGS(&descriptorHeap));
	assert(SUCCEEDED(hr));
	return descriptorHeap;
}

D3D12_CPU_DESCRIPTOR_HANDLE DirectXCommon::GetCPUDescriptorHandle(ID3D12DescriptorHeap* descriptorHeap, uint32_t descriptorSize, uint32_t index)
{
	D3D12_CPU_DESCRIPTOR_HANDLE handleCPU = descriptorHeap->GetCPUDescriptorHandleForHeapStart();
	handleCPU.ptr += ( static_cast< SIZE_T >( descriptorSize ) * index );
	return handleCPU;
}

D3D12_GPU_DESCRIPTOR_HANDLE DirectXCommon::GetGPUDescriptorHandle(ID3D12DescriptorHeap* descriptorHeap, uint32_t descriptorSize, uint32_t index)
{
	D3D12_GPU_DESCRIPTOR_HANDLE handleGPU = descriptorHeap->GetGPUDescriptorHandleForHeapStart();
	handleGPU.ptr += ( static_cast< UINT64 >( descriptorSize ) * index );
	return handleGPU;
}

D3D12_CPU_DESCRIPTOR_HANDLE DirectXCommon::GetSRVCPUDescriptorHandle(uint32_t index) const
{
	return GetCPUDescriptorHandle(srvDescriptorHeap.Get(), descriptorSizeSRV, index);
}

D3D12_GPU_DESCRIPTOR_HANDLE DirectXCommon::GetSRVGPUDescriptorHandle(uint32_t index) const
{
	return GetGPUDescriptorHandle(srvDescriptorHeap.Get(), descriptorSizeSRV, index);
}

uint32_t DirectXCommon::AllocateSrvIndex()
{
	assert(nextSrvIndex < kSrvHeapSize && "SRV heap is full. Increase kSrvHeapSize.");
	return nextSrvIndex++;
}
#pragma endregion

//==================================================
// リソース生成ヘルパー
//==================================================
#pragma region リソース生成
ComPtr<ID3D12Resource> DirectXCommon::CreateBufferResource(size_t sizeInBytes) const
{
	// アップロードヒープ
	D3D12_HEAP_PROPERTIES uploadHeapProperties{};
	uploadHeapProperties.Type = D3D12_HEAP_TYPE_UPLOAD;

	// バッファリソースの設定
	D3D12_RESOURCE_DESC resourceDesc{};
	resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
	resourceDesc.Width = sizeInBytes;
	resourceDesc.Height = 1; // バッファの場合はこれは1にする決まり
	resourceDesc.DepthOrArraySize = 1;
	resourceDesc.MipLevels = 1;
	resourceDesc.SampleDesc.Count = 1;
	resourceDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR; // バッファの場合はこれにする決まり

	ComPtr<ID3D12Resource> resource;
	HRESULT hr = device->CreateCommittedResource(
		&uploadHeapProperties, D3D12_HEAP_FLAG_NONE, &resourceDesc,
		D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&resource));
	assert(SUCCEEDED(hr));
	return resource;
}

ComPtr<ID3D12Resource> DirectXCommon::CreateTextureResource(const DirectX::TexMetadata& metaData) const
{
	// metaDataを基にResourceの設定
	D3D12_RESOURCE_DESC resourceDesc{};
	resourceDesc.Width = UINT(metaData.width);
	resourceDesc.Height = UINT(metaData.height);
	resourceDesc.MipLevels = UINT16(metaData.mipLevels);
	resourceDesc.DepthOrArraySize = UINT16(metaData.arraySize);
	resourceDesc.Format = metaData.format;
	resourceDesc.SampleDesc.Count = 1;
	resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION(metaData.dimension);

	D3D12_HEAP_PROPERTIES heapProperties{};
	heapProperties.Type = D3D12_HEAP_TYPE_DEFAULT;

	ComPtr<ID3D12Resource> resource;
	HRESULT hr = device->CreateCommittedResource(
		&heapProperties, D3D12_HEAP_FLAG_NONE, &resourceDesc,
		D3D12_RESOURCE_STATE_COPY_DEST, // 初回のResourceState。転送先にしておく
		nullptr, IID_PPV_ARGS(&resource));
	assert(SUCCEEDED(hr));
	return resource;
}

ComPtr<ID3D12Resource> DirectXCommon::UploadTextureData(ID3D12Resource* texture, const DirectX::ScratchImage& mipImages)
{
	std::vector<D3D12_SUBRESOURCE_DATA> subresources;
	DirectX::PrepareUpload(device.Get(), mipImages.GetImages(), mipImages.GetImageCount(), mipImages.GetMetadata(), subresources);

	uint64_t intermediateSize = GetRequiredIntermediateSize(texture, 0, UINT(subresources.size()));
	ComPtr<ID3D12Resource> intermediateResource = CreateBufferResource(intermediateSize);
	UpdateSubresources(commandList.Get(), texture, intermediateResource.Get(), 0, 0, UINT(subresources.size()), subresources.data());

	// 転送後は利用できるよう COPY_DEST -> GENERIC_READ へ変更する
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

DirectX::ScratchImage DirectXCommon::LoadTexture(const std::string& filePath)
{
	DirectX::ScratchImage image;
	std::wstring filePathW = StringUtility::ConvertString(filePath);

	HRESULT hr = DirectX::LoadFromWICFile(filePathW.c_str(), DirectX::WIC_FLAGS_FORCE_SRGB, nullptr, image);
	assert(SUCCEEDED(hr));

	DirectX::ScratchImage mipImages;
	hr = DirectX::GenerateMipMaps(
		image.GetImages(), image.GetImageCount(), image.GetMetadata(),
		DirectX::TEX_FILTER_SRGB, 0, mipImages);

	if ( FAILED(hr) )
	{
		// 1x1 textures (and some other formats) don't need mipmaps.
		return image;
	}
	return mipImages;
}

ComPtr<ID3D12Resource> DirectXCommon::CreateDepthStencilTextureResource(int32_t width, int32_t height) const
{
	D3D12_RESOURCE_DESC resourceDesc{};
	resourceDesc.Width = width;
	resourceDesc.Height = height;
	resourceDesc.MipLevels = 1; // DepthStencilは基本的に1
	resourceDesc.DepthOrArraySize = 1; // DepthStencilは基本的に配列なし
	resourceDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
	resourceDesc.SampleDesc.Count = 1;
	resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	resourceDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL; // DepthStencilとして使う通知

	D3D12_HEAP_PROPERTIES heapProperties{};
	heapProperties.Type = D3D12_HEAP_TYPE_DEFAULT; // VRAM上に作る

	// 深度のクリア設定
	D3D12_CLEAR_VALUE depthClearValue{};
	depthClearValue.Format = resourceDesc.Format;
	depthClearValue.DepthStencil.Depth = 1.0f; // 最大値でクリア
	depthClearValue.DepthStencil.Stencil = 0;

	ComPtr<ID3D12Resource> resource;
	HRESULT hr = device->CreateCommittedResource(
		&heapProperties, D3D12_HEAP_FLAG_NONE, &resourceDesc,
		D3D12_RESOURCE_STATE_DEPTH_WRITE, // 深度値を書き込む状態にしておく
		&depthClearValue, IID_PPV_ARGS(&resource));
	assert(SUCCEEDED(hr));
	return resource;
}
#pragma endregion

//==================================================
// シェーダー
//==================================================
ComPtr<IDxcBlob> DirectXCommon::CompileShader(const std::wstring& filePath, const wchar_t* profile)
{
	// これからシェーダーをコンパイルする事をログに出す
	Logger::Log(StringUtility::ConvertString(std::format(L"Begin CompileShader, path:{}, profile:{}\n", filePath, profile)));

	// hlslファイルを読む。読めなかったら止める
	ComPtr<IDxcBlobEncoding> shaderSource;
	HRESULT hr = dxcUtils->LoadFile(filePath.c_str(), nullptr, &shaderSource);
	assert(SUCCEEDED(hr));

	DxcBuffer shaderSourceBuffer;
	shaderSourceBuffer.Ptr = shaderSource->GetBufferPointer();
	shaderSourceBuffer.Size = shaderSource->GetBufferSize();
	shaderSourceBuffer.Encoding = DXC_CP_UTF8; // UTF8の文字コードであることを通知

	LPCWSTR arguments[] = {
		filePath.c_str(),      // コンパイル対象のhlslファイル名
		L"-E", L"main",        // エントリーポイントの指定。基本的にmain以外にはしない
		L"-T", profile,        // ShaderProfileの設定
		L"-Zi", L"-Qembed_debug", // デバッグ用の情報を埋め込む
		L"-Od",                // 最適化を外しておく
		L"-Zpr",               // メモリレイアウトは行優先
	};

	ComPtr<IDxcResult> shaderResult;
	hr = dxcCompiler->Compile(
		&shaderSourceBuffer, arguments, _countof(arguments),
		includeHandler.Get(), IID_PPV_ARGS(&shaderResult));
	// コンパイルエラーではなくdxcが起動できないなど致命的な状況
	assert(SUCCEEDED(hr));

	// 警告・エラーが出てたらログに出して止める
	ComPtr<IDxcBlobUtf8> shaderError;
	shaderResult->GetOutput(DXC_OUT_ERRORS, IID_PPV_ARGS(&shaderError), nullptr);
	if ( shaderError != nullptr && shaderError->GetStringLength() != 0 )
	{
		Logger::Log(shaderError->GetStringPointer());
		assert(false);
	}

	// コンパイル結果から実行用のバイナリ部分を取得
	ComPtr<IDxcBlob> shaderBlob;
	hr = shaderResult->GetOutput(DXC_OUT_OBJECT, IID_PPV_ARGS(&shaderBlob), nullptr);
	assert(SUCCEEDED(hr));

	Logger::Log(StringUtility::ConvertString(std::format(L"Compile Succeeded, path:{}, profile:{}\n", filePath, profile)));
	return shaderBlob;
}

void DirectXCommon::InitializeVSync()
{
	//現在の時間を記録する
	referenceFPS_ = std::chrono::steady_clock::now();
};
void DirectXCommon::UpdateVSync()
{
	// 1/60秒ぴったりの時間
	const std::chrono::microseconds kMinTime(uint64_t(1000000.0f / 60.0f));
	// 1/60秒よりわずかに短い時間
	const std::chrono::microseconds kMinCheckTime(uint64_t(1000000.0f / 65.0f));

	//現在の時間を取得する
	std::chrono::steady_clock::time_point currentTime = std::chrono::steady_clock::now();
	// 前回の時間からの経過時間を計算する
	std::chrono::microseconds elapsedTime = std::chrono::duration_cast< std::chrono::microseconds >( currentTime - referenceFPS_ );

	// 1/60秒 (よりわずかに短い時間) 経ってない場合
	if ( elapsedTime < kMinCheckTime )
	{
		// 1/60秒経過するまで微小なスリープするスリープを振り返す
		while ( std::chrono::steady_clock::now() - referenceFPS_ < kMinTime)
		{
			// 1マイクロ秒スリープする
			std::this_thread::sleep_for(std::chrono::microseconds(1));
		}
	}
	//現在の時間を記録する
	referenceFPS_ = std::chrono::steady_clock::now();

	//// FPS固定用の更新
	//// 1秒間に60回描画するための時間(ナノ秒)
	//constexpr uint64_t kOneSecondInNanoseconds = 1000000000;
	//constexpr uint64_t kTargetFPS = 60;
	//constexpr uint64_t kFrameDuration = kOneSecondInNanoseconds / kTargetFPS;
	//// 現在の時間を取得
	//LARGE_INTEGER frequency;
	//QueryPerformanceFrequency(&frequency);
	//LARGE_INTEGER currentTime;
	//QueryPerformanceCounter(&currentTime);
	//// 次のフレームの開始時間まで待つ
	//if ( currentTime.QuadPart < nextFrameTime )
	//{
	//	uint64_t waitTime = nextFrameTime - currentTime.QuadPart;
	//	uint64_t waitMilliseconds = ( waitTime * 1000 ) / frequency.QuadPart;
	//	Sleep(static_cast< DWORD >(waitMilliseconds));
	//}
	//// 次のフレームの開始時間を計算
	//nextFrameTime += ( kFrameDuration * frequency.QuadPart ) / kOneSecondInNanoseconds;
};