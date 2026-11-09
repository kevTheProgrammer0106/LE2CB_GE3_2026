#pragma once
#include "WinApp.h"

#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl.h>

#include <dbghelp.h>
#include <strsafe.h>
#include <dxgidebug.h>
#include <dxcapi.h>

#include "DirectXTex.h"

#include <array>
#include <cstdint>
#include <format>
#include <string>
#include <thread>
#include <chrono>
#include <cassert>

class DirectXCommon;
struct ImGuiContext; // imgui.h を include しなくてもポインタを持てるように前方宣言

// 1ウィンドウ分の描画先(スワップチェーン / RTV / 深度 / コマンド / ImGuiコンテキスト)
class WindowRenderer
{
public:
	WindowRenderer() = default;
	WindowRenderer(const WindowRenderer&) = delete;
	WindowRenderer& operator=(const WindowRenderer&) = delete;
	~WindowRenderer();

	static constexpr uint32_t kBackBufferCount = 2;
	static constexpr uint32_t kRtvHeapSize = kBackBufferCount;
	static constexpr uint32_t kDsvHeapSize = 1;

	// ==================================================

	void Initialize(WinApp* winApp, DirectXCommon* dx);

	// ==================================================

	void InitializeSwapChain();
	// レンダリング
	void InitializeRenderTargetView();
	// 深度バッファ
	void InitializeDepthBuffer();
	// 深度ビュー
	void InitializeDepthStencilView();
	// フェンス
	void InitializeFence();
	// ビューポート
	void InitializeViewportRectangle();
	// シザー矩形
	void InitializeScissoring();

	void InitializeCommands();

	void InitializeDescriptorHeaps();

	//--------------------------------------------------
	// フレーム制御
	//--------------------------------------------------
	/// フレーム先頭で呼ぶ。保留中のリサイズを適用する。
	/// false が返ったら(最小化中・非表示中)このフレームの更新・描画はスキップする
	bool BeginFrame();
	/// 描画開始: バリア / RTV・DSV設定 / クリア / ビューポート / SRVヒープ設定
	void PreDraw();
	/// 描画終了: (ImGuiは呼び出し側がRenderImGuiで積んでおく) バリア / Close / 実行 / Present / 待機 / リセット
	void PostDraw();
	/// GPUが現在までのコマンドを終えるまで待つ
	void WaitForGpu();
	/// 積んだコマンドを実行して待ち、コマンドリストを再びオープンする
	void FlushCommands();
	/// スワップチェーン・深度バッファ・ビューポート等を新しいサイズに作り直す
	void Resize(uint32_t width, uint32_t height);
	/// true: 垂直同期あり(Present(1,0)) / false: 同期なし(Present(0,0))。
	/// 複数ウィンドウでvsync待ちが重なってFPSが半減するのを避けるため、サブウィンドウではfalseにする
	void SetVSync(bool enabled)
	{
		syncInterval_ = enabled ? 1 : 0;
	}

	Microsoft::WRL::ComPtr<ID3D12Resource> CreateDepthStencilTextureResource(int32_t width, int32_t height) const;
	void TransitionBackBuffer(D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after);

	ID3D12GraphicsCommandList* GetCommandList() const
	{
		return commandList.Get();
	}
	DXGI_FORMAT GetRtvFormat() const
	{
		return rtvDesc.Format;
	}
	uint32_t GetBackBufferCount() const
	{
		return swapChainDesc.BufferCount;
	}
	uint32_t GetWidth() const
	{
		return width_;
	}
	uint32_t GetHeight() const
	{
		return height_;
	}

	//--------------------------------------------------
	// ImGui (USE_IMGUI未定義なら何もしない)
	// ウィンドウごとに専用のコンテキストを持つ。各関数の先頭でカレントを切り替える
	//--------------------------------------------------
	void InitializeImGui();
	void BeginImGuiFrame();
	/// ImGui::Render() と描画コマンドの積み込み。PostDrawの前に呼ぶ
	void RenderImGui();
	ImGuiContext* GetImGuiContext() const
	{
		return imguiContext_;
	}

private:
	// WindowsAPI
	WinApp* winApp = nullptr;
	DirectXCommon* dx = nullptr;

	// 現在のバッファサイズ
	uint32_t width_ = 0;
	uint32_t height_ = 0;
	UINT syncInterval_ = 1;

	// スワップチェーン
	Microsoft::WRL::ComPtr<IDXGISwapChain4> swapChain;
	std::array<Microsoft::WRL::ComPtr<ID3D12Resource>, kBackBufferCount> swapChainResources;
	DXGI_SWAP_CHAIN_DESC1 swapChainDesc{};
	UINT backBufferIndex = 0; // PreDrawで更新される、今フレームの書き込み先

	// コマンド
	Microsoft::WRL::ComPtr<ID3D12CommandAllocator> commandAllocator;
	Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> commandList;

	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> rtvDescriptorHeap;
	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> dsvDescriptorHeap;
	uint32_t descriptorSizeRTV = 0;
	uint32_t descriptorSizeDSV = 0;

	// RTV / DSV
	D3D12_RENDER_TARGET_VIEW_DESC rtvDesc{};
	std::array<D3D12_CPU_DESCRIPTOR_HANDLE, kBackBufferCount> rtvHandles{};
	Microsoft::WRL::ComPtr<ID3D12Resource> depthStencilResource;

	// フェンス
	Microsoft::WRL::ComPtr<ID3D12Fence> fence;
	uint64_t fenceValue = 0;
	HANDLE fenceEvent = nullptr;

	// ビューポート / シザー
	D3D12_VIEWPORT viewport{};
	D3D12_RECT scissorRect{};

	// ImGui(ウィンドウごとのコンテキストと、フォント用SRVスロット)
	ImGuiContext* imguiContext_ = nullptr;
	uint32_t imguiSrvIndex_ = 0;
	bool imguiInitialized = false;
};