#pragma once

#include <d3d12.h>

#include <string>
#include <vector>

#include "AssetCache.h"
#include "BlendModes.h"
#include "ResourceObject.h"
#include "ShapeGeometry.h"
#include "WorldTransform.h"

class DirectXCommon;
class PipelineManager;

/// <summary>
/// 箱・線・三角形・球・円錐のどれか1つ分(図形 + トランスフォーム + マテリアル + WVP)。
/// 定数バッファと頂点バッファは自分で持つので、ウィンドウ(シーン)ごとに別のインスタンスを作ること。
///
/// トランスフォームは2段:
///   Transform All : transform。図形全体(全頂点)に掛かる。RenderObjectと同じ使い方
///   Individual    : partTransforms[i]。パーツ(面・端点・半球など)ごと。useIndividual が true のときだけ有効。
///                   パーツの中心を基準に 拡大 → 回転 → 平行移動 し、そのあと Transform All が全体に掛かる
/// パーツの一覧は ShapeGeometry.h を参照
/// </summary>
class ShapeObject
{
public:
	void Initialize(DirectXCommon& dx, ShapeType type, const std::string& objectName);
	/// パーツの変形を反映した頂点をバッファに書き、WVPとUV変換を更新する
	void Update(const Matrix4x4& viewProjection);
	/// RootSignature/DescriptorHeap/Viewportは呼び出し側で設定済みの前提。
	/// PSOとトポロジはここで設定する(線はLINELIST、それ以外はTRIANGLELIST。終わったらTRIANGLELISTに戻す)
	void Draw(ID3D12GraphicsCommandList* commandList, const PipelineManager& pipelines, const AssetCache& assets,
		D3D12_GPU_VIRTUAL_ADDRESS lightAddress, ParticleBlendMode blendMode) const;
	/// Propertiesパネルの中身(図形ごとの値)
	void DrawUi();
	/// GPU待ちが済んだ後に呼ぶ
	void Shutdown();

	ShapeType GetType() const
	{
		return type_;
	}
	const ShapeGeometry& GetGeometry() const
	{
		return geometry_;
	}
	Material* GetMaterial() const
	{
		return materialData_;
	}

	// 外部から直接編集してよい
	std::string name;
	bool visible = true;

	// Transform All
	TransformData transform{ { 1.0f, 1.0f, 1.0f, 0.0f }, { 0.0f, 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f, 0.0f } };
	TransformData uvTransform{ { 1.0f, 1.0f, 1.0f, 0.0f }, { 0.0f, 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f, 0.0f } };

	// Individual
	bool useIndividual = false;
	std::vector<TransformData> partTransforms; // geometry_.parts と同じ数

	// 1: UVチェッカー / 0: 白
	int textureIndex = 0;

private:
	/// 球・円錐の分割数が変わったとき、頂点とパーツを作り直す
	void RebuildGeometry();

	ShapeType type_ = ShapeType::Box;
	int segments_ = 0;
	ShapeGeometry geometry_;

	ResourceObject vertexResource_;
	VertexData* mappedVertices_ = nullptr;
	D3D12_VERTEX_BUFFER_VIEW vbv_{};
	UINT vertexCount_ = 0;

	ResourceObject materialResource_;
	Material* materialData_ = nullptr;

	ResourceObject wvpResource_;
	TransformationMatrix* wvpData_ = nullptr;
};