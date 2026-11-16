#pragma once

#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl.h>

#include <dxcapi.h>

#include "DirectXTex.h"

#include <array>
#include <cassert>
#include <cstdint>
#include <cstddef>
#include <format>
#include <string>
#include <thread>
#include <chrono>

#include "VirtualisEngine/Core/Logger.h"
#include "VirtualisEngine/Core/StringUtility.h"

//==================================================
// DirectX基盤(ウィンドウに依存しない共有部分)
//   - デバイス / コマンドキュー / SRVヒープ / DXC / アップロード用コマンド
//   - バッファ・テクスチャ・シェーダーの生成ヘルパー
// スワップチェーン・深度・フレーム制御・ImGuiは WindowRenderer が受け持つ(ウィンドウごとに1つ)
//==================================================
class DirectXCommon
{
public:
	// SRVヒープの既定の大きさ。ApplicationDesc::srvHeapSize で変えられる
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
	/// SRVヒープの大きさを決める(Initialize より前に呼ぶ)
	void SetSrvHeapSize(uint32_t size)
	{
		srvHeapSize_ = size;
	}
	/// SRVの空きスロット数(エミッタ・子レイヤーを足せるかの確認に使う)
	uint32_t GetFreeSrvCount() const
	{
		return srvHeapSize_ - nextSrvIndex;
	}

	//--------------------------------------------------
	// リソース生成ヘルパー
	//--------------------------------------------------
	/// アップロードヒープ上のバッファ
	Microsoft::WRL::ComPtr<ID3D12Resource> CreateBufferResource(size_t sizeInBytes) const;
	/// アップロードヒープ上のバッファを作り、Mapして、CPUから書き込めるポインタを mapped に入れる。
	/// 「CreateBufferResource → Map」の2行がどのクラスにも出てくるので、ここにまとめてある。
	/// out は ComPtr<ID3D12Resource> でも ResourceObject でもよい。Mapしたままにする(アップロードヒープなので解除は不要)
	template <typename ResourceT, typename T>
	void CreateMappedBuffer(ResourceT& out, size_t sizeInBytes, T** mapped) const
	{
		out = CreateBufferResource(sizeInBytes);
		HRESULT hr = out.Get()->Map(0, nullptr, reinterpret_cast< void** >( mapped ));
		assert(SUCCEEDED(hr) && *mapped != nullptr && "Map() failed on upload buffer");
		( void ) hr;
	}
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
	/// シェーダーを探すフォルダ(例: L"VirtualisEngine/Shaders/")。CompileShader の相対パスの前に付き、#include の検索先にもなる。
	/// ライブラリのシェーダーをexeの隣以外に置くときに使う。空なら今までどおり作業フォルダ基準
	void SetShaderDirectory(const std::wstring& directory)
	{
		shaderDirectory = directory;
	}
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
	uint32_t srvHeapSize_ = kSrvHeapSize; // 実際のヒープの大きさ

	// DXC
	Microsoft::WRL::ComPtr<IDxcUtils> dxcUtils;
	Microsoft::WRL::ComPtr<IDxcCompiler3> dxcCompiler;
	Microsoft::WRL::ComPtr<IDxcIncludeHandler> includeHandler;
	std::wstring shaderDirectory;

	// アップロード用フェンス(WindowRendererのフレーム用フェンスとは別)
	Microsoft::WRL::ComPtr<ID3D12Fence> uploadFence;
	uint64_t uploadFenceValue = 0;
	HANDLE uploadFenceEvent = nullptr;
};
