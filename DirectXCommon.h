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

//==================================================
// ユーティリティ
//==================================================
// ログ出力
namespace Logger
{
	void Log(const std::string& message);
}
// 文字コードユーティリティ
namespace StringUtility
{
	std::wstring ConvertString(const std::string& str);
	// wstring -> string
	std::string ConvertString(const std::wstring& str);
}

//==================================================
// DirectX基盤
//   - デバイス / コマンド / スワップチェーン / ヒープ / 深度 / フェンス / DXC / ImGui の初期化
//   - フレームの開始(PreDraw)と終了(PostDraw)
//   - バッファ・テクスチャ・シェーダーの生成ヘルパー
//==================================================
class DirectXCommon
{
public:
	static constexpr uint32_t kBackBufferCount = 2;
	static constexpr uint32_t kRtvHeapSize = kBackBufferCount;
	static constexpr uint32_t kSrvHeapSize = 128;
	static constexpr uint32_t kDsvHeapSize = 1;

	~DirectXCommon();

	//--------------------------------------------------
	// ライフサイクル
	//--------------------------------------------------
	void Initialize(WinApp* winApp);

	//--------------------------------------------------
	// フレーム制御
	//--------------------------------------------------
	/// 描画開始: バリア / RTV・DSV設定 / クリア / ビューポート / SRVヒープ設定
	void PreDraw();
	/// 描画終了: (ImGuiは呼び出し側がRenderImGuiで積んでおく) バリア / Close / 実行 / Present / 待機 / リセット
	void PostDraw();
	/// GPUが現在までのコマンドを終えるまで待つ
	void WaitForGpu();
	/// 積んだコマンドを実行して待ち、コマンドリストを再びオープンする(初期化時のテクスチャアップロード用)
	void FlushCommands();

	//--------------------------------------------------
	// ImGui (USE_IMGUI未定義なら何もしない)
	//--------------------------------------------------
	void BeginImGuiFrame();
	/// ImGui::Render() と描画コマンドの積み込み。PostDrawの前に呼ぶ
	void RenderImGui();

	//--------------------------------------------------
	// ゲッター
	//--------------------------------------------------
	ID3D12Device* GetDevice() const
	{
		return device.Get();
	}
	ID3D12GraphicsCommandList* GetCommandList() const
	{
		return commandList.Get();
	}
	ID3D12CommandQueue* GetCommandQueue() const
	{
		return commandQueue.Get();
	}
	ID3D12DescriptorHeap* GetSrvDescriptorHeap() const
	{
		return srvDescriptorHeap.Get();
	}
	DXGI_FORMAT GetRtvFormat() const
	{
		return rtvDesc.Format;
	}
	uint32_t GetBackBufferCount() const
	{
		return swapChainDesc.BufferCount;
	}

//--------------------------------------------------
// ディスクリプタ
//--------------------------------------------------
	static Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> CreateDescriptorHeap(
		ID3D12Device* device, D3D12_DESCRIPTOR_HEAP_TYPE type, UINT numDescriptors, bool shaderVisible);
	static D3D12_CPU_DESCRIPTOR_HANDLE GetCPUDescriptorHandle(ID3D12DescriptorHeap* descriptorHeap, uint32_t descriptorSize, uint32_t index);
	static D3D12_GPU_DESCRIPTOR_HANDLE GetGPUDescriptorHandle(ID3D12DescriptorHeap* descriptorHeap, uint32_t descriptorSize, uint32_t index);

	/// SRVヒープの指定番号のハンドル
	D3D12_CPU_DESCRIPTOR_HANDLE GetSRVCPUDescriptorHandle(uint32_t index) const;
	D3D12_GPU_DESCRIPTOR_HANDLE GetSRVGPUDescriptorHandle(uint32_t index) const;
	/// SRVヒープの空き番号を1つ確保して返す(0番はImGuiのフォント用に予約済みなので1から始まる)
	uint32_t AllocateSrvIndex();

	//--------------------------------------------------
	// リソース生成ヘルパー
	//--------------------------------------------------
	/// アップロードヒープ上のバッファ
	Microsoft::WRL::ComPtr<ID3D12Resource> CreateBufferResource(size_t sizeInBytes) const;
	/// テクスチャ本体(COPY_DEST状態で生成される)
	Microsoft::WRL::ComPtr<ID3D12Resource> CreateTextureResource(const DirectX::TexMetadata& metaData) const;
	/// テクスチャのアップロードコマンドを積む。戻り値の中間リソースは FlushCommands() が終わるまで保持すること
	[[nodiscard]]
	Microsoft::WRL::ComPtr<ID3D12Resource> UploadTextureData(ID3D12Resource* texture, const DirectX::ScratchImage& mipImages);
	/// 画像を読み込んでミップマップを生成する
	static DirectX::ScratchImage LoadTexture(const std::string& filePath);

	//--------------------------------------------------
	// シェーダー
	//--------------------------------------------------
	Microsoft::WRL::ComPtr<IDxcBlob> CompileShader(const std::wstring& filePath, const wchar_t* profile);

private:

	// 記録時間(FPS固定用)
	std::chrono::steady_clock::time_point referenceFPS_;

	//--------------------------------------------------
	// 初期化(Initializeから順に呼ばれる)
	//--------------------------------------------------
	void InitializeDevice();
	void InitializeCommands();
	void InitializeSwapChain();
	void InitializeDescriptorHeap();
	void InitializeRenderTargetView();
	void InitializeDepthBuffer();
	void InitializeDepthStencilView();
	void InitializeFence();
	void InitializeViewportRectangle();
	void InitializeScissoring();
	void InitializeDxcCompiler();
	void InitializeImGui();

	Microsoft::WRL::ComPtr<ID3D12Resource> CreateDepthStencilTextureResource(int32_t width, int32_t height) const;
	void TransitionBackBuffer(D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after);

	// WindowsAPI
	WinApp* winApp = nullptr;

	// デバイス周り
	Microsoft::WRL::ComPtr<ID3D12Device> device;
	Microsoft::WRL::ComPtr<IDXGIFactory7> dxgiFactory;
	Microsoft::WRL::ComPtr<IDXGIAdapter4> useAdapter;

	// コマンド
	Microsoft::WRL::ComPtr<ID3D12CommandQueue> commandQueue;
	Microsoft::WRL::ComPtr<ID3D12CommandAllocator> commandAllocator;
	Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> commandList;

	// スワップチェーン
	Microsoft::WRL::ComPtr<IDXGISwapChain4> swapChain;
	std::array<Microsoft::WRL::ComPtr<ID3D12Resource>, kBackBufferCount> swapChainResources;
	DXGI_SWAP_CHAIN_DESC1 swapChainDesc{};
	UINT backBufferIndex = 0; // PreDrawで更新される、今フレームの書き込み先

	// ディスクリプタヒープ
	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> rtvDescriptorHeap;
	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> srvDescriptorHeap;
	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> dsvDescriptorHeap;
	uint32_t descriptorSizeSRV = 0;
	uint32_t descriptorSizeRTV = 0;
	uint32_t descriptorSizeDSV = 0;
	uint32_t nextSrvIndex = 1; // 0番はImGui用

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

	// DXC
	Microsoft::WRL::ComPtr<IDxcUtils> dxcUtils;
	Microsoft::WRL::ComPtr<IDxcCompiler3> dxcCompiler;
	Microsoft::WRL::ComPtr<IDxcIncludeHandler> includeHandler;

	// FPS
	// FPS固定用の初期化
	void InitializeVSync(); // Frames per Second固定用の初期化
	void UpdateVSync(); // Frames per Second固定用の更新

	bool imguiInitialized = false;
};