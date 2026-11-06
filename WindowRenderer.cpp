#include "WindowRenderer.h"
#include "DirectXCommon.h"

#ifdef USE_IMGUI
#include "externals/imgui/imgui.h"
#include "externals/imgui/imgui_impl_dx12.h"
#include "externals/imgui/imgui_impl_win32.h"
#endif

using namespace Microsoft::WRL;

WindowRenderer::~WindowRenderer()
{
	// GPUが使っている間にリソースを壊さないよう、まず待つ
	if ( fence )
	{
		WaitForGpu();
	}

#ifdef USE_IMGUI
	if ( imguiInitialized )
	{
		// このウィンドウのコンテキストをカレントにしてから後始末する
		ImGui::SetCurrentContext(imguiContext_);
		ImGui_ImplDX12_Shutdown();
		ImGui_ImplWin32_Shutdown();
		ImGui::DestroyContext(imguiContext_);
		ImGui::SetCurrentContext(nullptr);
		imguiContext_ = nullptr;
		if ( winApp )
		{
			winApp->SetImGuiContext(nullptr);
		}
		imguiInitialized = false;
	}
#endif

	if ( fenceEvent )
	{
		CloseHandle(fenceEvent);
		fenceEvent = nullptr;
	}
}
void WindowRenderer::Initialize(
	WinApp* winApp,
	DirectXCommon* dx)
{
	assert(winApp);
	assert(dx);

	this->winApp = winApp;
	this->dx = dx;

	// 作成時点のクライアントサイズで作るので、保留中のリサイズ通知は捨てる
	uint32_t pendingWidth = 0;
	uint32_t pendingHeight = 0;
	winApp->ConsumeResize(pendingWidth, pendingHeight);
	width_ = winApp->GetClientWidth();
	height_ = winApp->GetClientHeight();

	// ウィンドウ専用のコマンドを初期化
	InitializeCommands();

	InitializeSwapChain();
	InitializeDescriptorHeaps();

	InitializeRenderTargetView();
	InitializeDepthBuffer();
	InitializeDepthStencilView();

	InitializeFence();
	InitializeViewportRectangle();
	InitializeScissoring();

	InitializeImGui();
}

//==================================================
// ImGui
//==================================================
void WindowRenderer::InitializeImGui()
{
#ifdef USE_IMGUI
	IMGUI_CHECKVERSION();

	// ウィンドウごとに独立したコンテキストを作る
	imguiContext_ = ImGui::CreateContext();
	ImGui::SetCurrentContext(imguiContext_);
	ImGui::StyleColorsDark();

	// WinAppがこのウィンドウのメッセージを処理するときに、このコンテキストに切り替えられるようにする
	winApp->SetImGuiContext(imguiContext_);

	// フォント用のSRVスロットはウィンドウごとに別々に確保する(共有すると上書きし合う)
	imguiSrvIndex_ = dx->AllocateSrvIndex();

	ImGui_ImplWin32_Init(winApp->GetHwnd());
	ImGui_ImplDX12_Init(
		dx->GetDevice(), swapChainDesc.BufferCount,
		rtvDesc.Format,
		dx->GetSrvDescriptorHeap(),
		dx->GetSRVCPUDescriptorHandle(imguiSrvIndex_),
		dx->GetSRVGPUDescriptorHandle(imguiSrvIndex_));
	ImGui::GetIO().Fonts->Build();
	imguiInitialized = true;
#endif
}
void WindowRenderer::BeginImGuiFrame()
{
#ifdef USE_IMGUI
	ImGui::SetCurrentContext(imguiContext_);
	ImGui_ImplDX12_NewFrame();
	ImGui_ImplWin32_NewFrame();
	ImGui::NewFrame();
#endif
}

void WindowRenderer::RenderImGui()
{
#ifdef USE_IMGUI
	ImGui::SetCurrentContext(imguiContext_);
	// ImGuiの内部コマンドを生成し、実際のcommandListに描画コマンドを積む
	ImGui::Render();
	ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), commandList.Get());
#endif
}

void WindowRenderer::InitializeCommands()
{
	HRESULT hr;

	hr = dx->GetDevice()->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&commandAllocator));
	assert(SUCCEEDED(hr));

	hr = dx->GetDevice()->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, commandAllocator.Get(), nullptr, IID_PPV_ARGS(&commandList));
	assert(SUCCEEDED(hr));
}
void WindowRenderer::InitializeSwapChain()
{
	swapChainDesc.Width = width_; // ウィンドウのクライアント領域と同じにしておく
	swapChainDesc.Height = height_;
	swapChainDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM; // 色の形式
	swapChainDesc.SampleDesc.Count = 1; // マルチサンプリングしない
	swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT; // 描画のターゲットとして利用する
	swapChainDesc.BufferCount = kBackBufferCount; // ダブルバッファ
	swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD; // モニタにうつしたら、中身を破棄

	ComPtr<IDXGISwapChain1> tempSwapChain;

	HRESULT hr = dx->GetDxgiFactory()->CreateSwapChainForHwnd(
		dx->GetCommandQueue(), winApp->GetHwnd(), &swapChainDesc,
		nullptr, nullptr, &tempSwapChain
	);
	assert(SUCCEEDED(hr));

	hr = tempSwapChain.As(&swapChain);
	assert(SUCCEEDED(hr));
}

void WindowRenderer::InitializeRenderTargetView()
{
	rtvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB; // 出力結果をsRGBに変換して書き込み
	rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D; // 2Dテクスチャとして書き込む

	for ( uint32_t i = 0; i < kBackBufferCount; ++i )
	{
		HRESULT hr = swapChain->GetBuffer(i, IID_PPV_ARGS(&swapChainResources[i]));
		assert(SUCCEEDED(hr));

		rtvHandles[i] = dx->GetCPUDescriptorHandle(rtvDescriptorHeap.Get(), descriptorSizeRTV, i);
		dx->GetDevice()->CreateRenderTargetView(swapChainResources[i].Get(), &rtvDesc, rtvHandles[i]);
	}
}

void WindowRenderer::InitializeDepthBuffer()
{
	// 現在のバッファサイズで深度バッファを作成する
	depthStencilResource = CreateDepthStencilTextureResource(static_cast< int32_t >( width_ ), static_cast< int32_t >( height_ ));
	assert(depthStencilResource != nullptr);
}

void WindowRenderer::InitializeDepthStencilView()
{
	D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc{};
	dsvDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT; // 基本的にはResourceに合わせる
	dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D; // 2Dテクスチャとして使う

	// DSVヒープの先頭にビューを作成する
	dx->GetDevice()->CreateDepthStencilView(depthStencilResource.Get(), &dsvDesc, dsvDescriptorHeap->GetCPUDescriptorHandleForHeapStart());
}

void WindowRenderer::InitializeFence()
{
	fenceValue = 0;
	HRESULT hr = dx->GetDevice()->CreateFence(fenceValue, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence));
	assert(SUCCEEDED(hr));

	// FenceのSignalを待つためのイベントを作成する
	fenceEvent = CreateEvent(NULL, FALSE, FALSE, NULL);
	assert(fenceEvent != nullptr);
}

void WindowRenderer::InitializeViewportRectangle()
{
	// クライアント領域のサイズと一緒にして画面全体に表示
	viewport.Width = static_cast< float >( width_ );
	viewport.Height = static_cast< float >( height_ );
	viewport.TopLeftX = 0;
	viewport.TopLeftY = 0;
	viewport.MinDepth = 0.0f;
	viewport.MaxDepth = 1.0f;
}

void WindowRenderer::InitializeScissoring()
{
	// 基本的にビューポートと同じ矩形が構成されるようにする
	scissorRect.left = 0;
	scissorRect.right = static_cast< LONG >( width_ );
	scissorRect.top = 0;
	scissorRect.bottom = static_cast< LONG >( height_ );
}

//==================================================
// フレーム制御
//==================================================
#pragma region フレーム制御
bool WindowRenderer::BeginFrame()
{
	// 最小化中や非表示中は描画しない(サイズ0のバッファは作れないため)
	if ( winApp->IsMinimized() || !winApp->IsVisible() )
	{
		return false;
	}

	uint32_t newWidth = 0;
	uint32_t newHeight = 0;
	if ( winApp->ConsumeResize(newWidth, newHeight) )
	{
		Resize(newWidth, newHeight);
	}
	return true;
}

void WindowRenderer::Resize(uint32_t width, uint32_t height)
{
	if ( width == 0 || height == 0 )
	{
		return;
	}
	if ( width == width_ && height == height_ )
	{
		return;
	}

	// GPUが旧バッファを使い終わってから解放する
	WaitForGpu();

	// ResizeBuffersの前に、バックバッファへの参照をすべて手放す
	for ( auto& resource : swapChainResources )
	{
		resource.Reset();
	}
	depthStencilResource.Reset();

	HRESULT hr = swapChain->ResizeBuffers(kBackBufferCount, width, height, DXGI_FORMAT_UNKNOWN, 0);
	assert(SUCCEEDED(hr));

	width_ = width;
	height_ = height;
	swapChainDesc.Width = width;
	swapChainDesc.Height = height;

	InitializeRenderTargetView();
	InitializeDepthBuffer();
	InitializeDepthStencilView();
	InitializeViewportRectangle();
	InitializeScissoring();
}

void WindowRenderer::TransitionBackBuffer(D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after)
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

void WindowRenderer::PreDraw()
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
	ID3D12DescriptorHeap* descriptorHeaps[] = { dx->GetSrvDescriptorHeap() };
	commandList->SetDescriptorHeaps(1, descriptorHeaps);
}
void WindowRenderer::InitializeDescriptorHeaps()
{
	// RTVはShader内で触るものではないので、ShaderVisibleはfalse
	rtvDescriptorHeap = dx->CreateDescriptorHeap(dx->GetDevice(), D3D12_DESCRIPTOR_HEAP_TYPE_RTV, kRtvHeapSize, false);
	// DSVはShader内で触るものではないので、ShaderVisibleはfalse
	dsvDescriptorHeap = dx->CreateDescriptorHeap(dx->GetDevice(), D3D12_DESCRIPTOR_HEAP_TYPE_DSV, kDsvHeapSize, false);

	// ディスクリプタのサイズは一度だけ取得してキャッシュする
	descriptorSizeRTV = dx->GetDevice()->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
	descriptorSizeDSV = dx->GetDevice()->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);
}

void WindowRenderer::PostDraw()
{
	// 描画が終わったので RenderTarget -> Present
	TransitionBackBuffer(D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);

	// コマンドリストの内容を確定させる。すべてのコマンドを積んでからCloseすること
	HRESULT hr = commandList->Close();
	assert(SUCCEEDED(hr));

	// GPUにコマンドリストの実行を行わせる
	ID3D12CommandList* commandLists[] = { commandList.Get() };
	dx->GetCommandQueue()->ExecuteCommandLists(1, commandLists);

	// GPUとOSに画面の交換を行うように通知する(syncInterval_が1なら垂直同期あり)
	hr = swapChain->Present(syncInterval_, 0);
	assert(SUCCEEDED(hr));

	// このフレームのGPU処理が終わるまで待つ
	WaitForGpu();
	
	// 次のフレーム用のコマンドリストを準備
	hr = commandAllocator->Reset();
	assert(SUCCEEDED(hr));
	hr = commandList->Reset(commandAllocator.Get(), nullptr);
	assert(SUCCEEDED(hr));
}

void WindowRenderer::WaitForGpu()
{
	// GPUがここまでたどり着いた時に、Fenceに指定した値を代入するようにSignalを送る
	++fenceValue;
	HRESULT hr = dx->GetCommandQueue()->Signal(fence.Get(), fenceValue);
	assert(SUCCEEDED(hr));

	// まだたどり着いていなければ、たどり着くまでイベントで待つ
	if ( fence->GetCompletedValue() < fenceValue )
	{
		hr = fence->SetEventOnCompletion(fenceValue, fenceEvent);
		assert(SUCCEEDED(hr));

		WaitForSingleObject(fenceEvent, INFINITE);
	}
}

void WindowRenderer::FlushCommands()
{
	// ウィンドウ専用のコマンドリストを確定する
	HRESULT hr = commandList->Close();
	assert(SUCCEEDED(hr));

	ID3D12CommandList* commandLists[] = {
		commandList.Get()
	};

	dx->GetCommandQueue()->ExecuteCommandLists(1, commandLists);

	// このレンダラーのGPU処理を待つ
	WaitForGpu();

	hr = commandAllocator->Reset();
	assert(SUCCEEDED(hr));

	hr = commandList->Reset(
		commandAllocator.Get(), nullptr);
	assert(SUCCEEDED(hr));
}
#pragma endregion

ComPtr<ID3D12Resource> WindowRenderer::CreateDepthStencilTextureResource(int32_t width, int32_t height) const
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
	HRESULT hr = dx->GetDevice()->CreateCommittedResource(
		&heapProperties, D3D12_HEAP_FLAG_NONE, &resourceDesc,
		D3D12_RESOURCE_STATE_DEPTH_WRITE, // 深度値を書き込む状態にしておく
		&depthClearValue, IID_PPV_ARGS(&resource));
	assert(SUCCEEDED(hr));
	return resource;
}