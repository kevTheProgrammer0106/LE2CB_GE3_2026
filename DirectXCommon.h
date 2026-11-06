#pragma once

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
#include <cstddef>
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
// DirectX基盤(ウィンドウに依存しない共有部分)
//   - デバイス / コマンドキュー / SRVヒープ / DXC / アップロード用コマンド
//   - バッファ・テクスチャ・シェーダーの生成ヘルパー
// スワップチェーン・深度・フレーム制御・ImGuiは WindowRenderer が受け持つ(ウィンドウごとに1つ)
//==================================================
class DirectXCommon
{
public:
	// 全ウィンドウのImGuiフォント・テクスチャで共有するSRVヒープの大きさ
	static constexpr uint32_t kSrvHeapSize = 128;

	DirectXCommon() = default;
	DirectXCommon(const DirectXCommon&) = delete;
	DirectXCommon& operator=(const DirectXCommon&) = delete;
	~DirectXCommon();

	void Initialize();

	//--------------------------------------------------
	// ゲッター
	//--------------------------------------------------
	ID3D12Device* GetDevice() const
	{
		return device.Get();
	}

	ID3D12DescriptorHeap* GetSrvDescriptorHeap() const
	{
		return srvDescriptorHeap.Get();
	}
	ID3D12CommandQueue* GetCommandQueue() const
	{
		return commandQueue.Get();
	}
	ID3D12GraphicsCommandList* GetUploadCommandList() const
	{
		return uploadCommandList.Get();
	}
	ID3D12CommandAllocator* GetUploadCommandAllocator() const
	{
		return uploadCommandAllocator.Get();
	}

	IDXGIFactory7* GetDxgiFactory() const
	{
		return dxgiFactory.Get();
	}

	//--------------------------------------------------
	// アップロード用コマンド(テクスチャ転送など、描画フレームとは別に流すもの)
	//--------------------------------------------------
	/// 積んだアップロードコマンドを実行し、完了を待ってからコマンドリストを再オープンする。
	/// UploadTextureData() が返した中間リソースは、これが終わるまで保持すること
	void FlushUploadCommands();
	/// アップロード用フェンスでGPU完了を待つ
	void WaitForUploadGpu();

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
	/// SRVヒープの空き番号を1つ確保して返す。
	/// ImGuiのフォント用スロットも、ウィンドウごとにここから確保する(共有すると上書きし合うため)
	uint32_t AllocateSrvIndex();

	//--------------------------------------------------
	// リソース生成ヘルパー
	//--------------------------------------------------
	/// アップロードヒープ上のバッファ
	Microsoft::WRL::ComPtr<ID3D12Resource> CreateBufferResource(size_t sizeInBytes) const;
	/// テクスチャ本体(COPY_DEST状態で生成される)
	Microsoft::WRL::ComPtr<ID3D12Resource> CreateTextureResource(const DirectX::TexMetadata& metaData) const;
	/// テクスチャのアップロードコマンドを積む。戻り値の中間リソースは FlushUploadCommands() が終わるまで保持すること
	[[nodiscard]]
	Microsoft::WRL::ComPtr<ID3D12Resource> UploadTextureData(ID3D12Resource* texture, const DirectX::ScratchImage& mipImages);
	/// 画像を読み込んでミップマップを生成する
	static DirectX::ScratchImage LoadTexture(const std::string& filePath);

	//--------------------------------------------------
	// シェーダー
	//--------------------------------------------------
	Microsoft::WRL::ComPtr<IDxcBlob> CompileShader(const std::wstring& filePath, const wchar_t* profile);

private:

	//--------------------------------------------------
	// 初期化(Initializeから順に呼ばれる)
	//--------------------------------------------------
	void InitializeDevice();
	void InitializeCommandQueue();
	void InitializeUploadCommands();
	void InitializeDescriptorHeap();
	void InitializeDxcCompiler();

	// デバイス周り
	Microsoft::WRL::ComPtr<ID3D12Device> device;
	Microsoft::WRL::ComPtr<IDXGIFactory7> dxgiFactory;
	Microsoft::WRL::ComPtr<IDXGIAdapter4> useAdapter;

	// コマンドキュー
	Microsoft::WRL::ComPtr<ID3D12CommandQueue> commandQueue;
	Microsoft::WRL::ComPtr<ID3D12CommandAllocator> uploadCommandAllocator;
	Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> uploadCommandList;

	// ディスクリプタヒープ
	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> srvDescriptorHeap;
	uint32_t descriptorSizeSRV = 0;
	uint32_t nextSrvIndex = 0; // 予約スロットは無い。ImGuiも含めてAllocateSrvIndex()で確保する

	// DXC
	Microsoft::WRL::ComPtr<IDxcUtils> dxcUtils;
	Microsoft::WRL::ComPtr<IDxcCompiler3> dxcCompiler;
	Microsoft::WRL::ComPtr<IDxcIncludeHandler> includeHandler;

	// アップロード用フェンス(WindowRendererのフレーム用フェンスとは別)
	Microsoft::WRL::ComPtr<ID3D12Fence> uploadFence;
	uint64_t uploadFenceValue = 0;
	HANDLE uploadFenceEvent = nullptr;
};