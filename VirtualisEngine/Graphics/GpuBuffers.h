#pragma once

#include "VirtualisEngine/Graphics/DirectXCommon.h"
#include "VirtualisEngine/Math/WorldTransform.h"

//===================================================================
// どの描画クラスも同じ形で作る定数バッファの作成ヘルパー。
// 「バッファ作成 → Map → 初期値を入れる」を毎回書かずに済むようにまとめてある。
// どちらも「CPUから書き込めるポインタ」を返す(アップロードヒープなのでMapしたままでよい)。
// out は ComPtr<ID3D12Resource> でも ResourceObject でもよい。
//===================================================================

/// マテリアル(色は白、UV変換は単位行列)を作る
template <typename ResourceT>
Material* CreateMaterialBuffer(const DirectXCommon& dx, ResourceT& out, LightingMode lightingMode)
{
	Material* material = nullptr;
	dx.CreateMappedBuffer(out, sizeof(Material), &material);
	*material = {};
	material->color = { 1.0f, 1.0f, 1.0f, 1.0f };
	material->lightingMode = lightingMode;
	material->uvTransform = MakeIdentityMatrix();
	return material;
}

/// WVP・Worldを単位行列にした変換行列用バッファを作る
template <typename ResourceT>
TransformationMatrix* CreateTransformBuffer(const DirectXCommon& dx, ResourceT& out)
{
	TransformationMatrix* transform = nullptr;
	dx.CreateMappedBuffer(out, sizeof(TransformationMatrix), &transform);
	transform->WVP = MakeIdentityMatrix();
	transform->World = MakeIdentityMatrix();
	return transform;
}
