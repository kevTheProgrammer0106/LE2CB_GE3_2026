#pragma once

#include <d3d12.h>

#include <string>

#include "Engine/Graphics/AssetCache.h"
#include "Engine/Graphics/ResourceObject.h"
#include "Engine/Math/WorldTransform.h"

class DirectXCommon;

/// <summary>
/// 描画対象1つ分(モデル名 + トランスフォーム + マテリアル + WVP)。
/// 定数バッファは自分で持つので、ウィンドウ(シーン)ごとに別のインスタンスを作ること
/// </summary>
class RenderObject
{
public:
	void Initialize(DirectXCommon& dx, const std::string& modelName);
	/// カメラで作った共通のviewProjectionを使ってWVPとUV変換を更新する
	void Update(const Matrix4x4& viewProjection);
	/// RootSignature/PSO/DescriptorHeap/Viewportは呼び出し側で設定済みの前提
	void Draw(ID3D12GraphicsCommandList* commandList, const AssetCache& assets, D3D12_GPU_VIRTUAL_ADDRESS lightAddress) const;
	/// assets: モデルの一覧(Modelコンボ)を作るために使う
	void DrawUi(const char* label, const AssetCache& assets);

	// 外部から直接編集してよい
	std::string modelName; // AssetCache に登録したモデルの名前
	TransformData transform{ { 1.0f, 1.0f, 1.0f, 0.0f }, { 0.0f, 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f, 0.0f } };
	TransformData uvTransform{ { 1.0f, 1.0f, 1.0f, 0.0f }, { 0.0f, 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f, 0.0f } };

	Material* GetMaterial() const
	{
		return materialData_;
	}

private:
	ResourceObject materialResource_;
	Material* materialData_ = nullptr;

	ResourceObject wvpResource_;
	TransformationMatrix* wvpData_ = nullptr;
};
