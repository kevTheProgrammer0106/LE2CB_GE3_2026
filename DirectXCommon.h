#pragma once
#include "WinApp.h"

#include <d3d12.h>
#include <dxgi1_6.h>
#include<wrl.h>

#include <dbghelp.h>
#include<strsafe.h>
#include <dxgidebug.h>
#include <dxcapi.h>

#include <string>
#include <format>
#include <array>

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

// DirectX基盤
class DirectXCommon
{
public: // メンバ関数
	// 初期化
	void Initialize(WinApp* winApp);

	/// <summary>
	///  デスクリプタヒープを生成する
	/// </summary>
	/// <param name="device"></param>
	/// <param name="type"></param>
	/// <param name="numDescriptors"></param>
	/// <param name="shaderVisible"></param>
	/// <returns></returns>
	Microsoft::WRL::ComPtr <ID3D12DescriptorHeap> CreateDescriptorHeap(ID3D12Device* device, D3D12_DESCRIPTOR_HEAP_TYPE type, UINT numDescriptors, bool shaderVisible);
	/// <summary>
/// 指定番号のCPUデスクリプタハンドルを取得する
/// </summary>
/// <param name="descriptorHeap"></param>
/// <param name="descriptorSize"></param>
/// <param name="index"></param>
/// <returns></returns>
	D3D12_CPU_DESCRIPTOR_HANDLE GetCPUDescriptorHandle(ID3D12DescriptorHeap* descriptorHeap, uint32_t descriptorSize, uint32_t index);
	/// <summary>
/// 指定番号のGPUデスクリプタハンドルを取得する
/// </summary>
/// <param name="descriptorHeap"></param>
/// <param name="descriptorSize"></param>
/// <param name="index"></param>
/// <returns></returns>
	D3D12_GPU_DESCRIPTOR_HANDLE GetGPUDescriptorHandle(ID3D12DescriptorHeap* descriptorHeap, uint32_t descriptorSize, uint32_t index);

	/// <summary>
	/// SRVの指定番号のCPUデズクリプタハンドルを取得する
	/// </summary>
	/// <param name="index"></param>
	/// <returns></returns>
	D3D12_CPU_DESCRIPTOR_HANDLE GetSRVCPUDescriptorHandle(uint32_t index);
	/// <summary>
	/// SRVの指定番号のGPUデズクリプタハンドルを取得する
	/// </summary>
	/// <param name="index"></param>
	/// <returns></returns>
	D3D12_GPU_DESCRIPTOR_HANDLE GetSRVGPUDescriptorHandle(uint32_t index);

private:
	// WindowsAPI
	WinApp* winApp = nullptr;

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

	// DirectX12デバイス
	Microsoft::WRL::ComPtr<ID3D12Device> device;
	//DXGIファクトリーの生成
	Microsoft::WRL::ComPtr<IDXGIFactory7> dxgiFactory = nullptr;
		// 使用するアダプタ用の変数。最初にはnullptrを入れておく
	Microsoft::WRL::ComPtr<IDXGIAdapter4> useAdapter = nullptr;
		// コマンドキューを生成する
	Microsoft::WRL::ComPtr<ID3D12CommandQueue> commandQueue = nullptr;
	//コマンドアロケータを生成する
	Microsoft::WRL::ComPtr<ID3D12CommandAllocator> commandAllocator = nullptr;
		// コマンドリストを生成する
	Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> commandList = nullptr;

	//スワップチェーンを生成する
	Microsoft::WRL::ComPtr<IDXGISwapChain4> swapChain = nullptr;
	//スワップチェーンリソース
	std::array<Microsoft::WRL::ComPtr<ID3D12Resource>, 2> swapChainResources;

	DXGI_SWAP_CHAIN_DESC1 swapChainDesc{};


	//ディスクリプタヒープの生成
	// RTV用のヒープでディスクリプタの数は2。RTVはShader内で触るものではないので、ShaderVisibleはfalse
	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> rtvDescriptorHeap = nullptr;
	// SRV用のヒープでディスクリプタの数は128。SRVはShader内で触るものなので、ShaderVisibleはtrue
	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> srvDescriptorHeap = nullptr;
	// DSV用のヒープでディスクリプタの数は1。DSVはShader内で触るものではないので、ShaderVisibleはfalse
	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> dsvDescriptorHeap = nullptr;

	//RTVの設定
	D3D12_RENDER_TARGET_VIEW_DESC rtvDesc{};

	Microsoft::WRL::ComPtr<ID3D12Resource> depthStencilResource = nullptr;

	//初期値0でFenceを作る
	Microsoft::WRL::ComPtr<ID3D12Fence> fence = nullptr;
	//ビューポート
	D3D12_VIEWPORT viewport{};
	//シザー矩形
	D3D12_RECT scissorRect{};

	// dxcCompilerを初期化
	Microsoft::WRL::ComPtr <IDxcUtils> dxcUtils = nullptr;
	Microsoft::WRL::ComPtr <IDxcCompiler3> dxcCompiler = nullptr;
};

