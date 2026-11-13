#pragma once

class AssetCache;
class DirectXCommon;

/// <summary>
/// このゲームで使うアセット(モデル・テクスチャ)の一覧。
/// エンジン(ライブラリ)は何を読むかを知らないので、ゲームごとにここを書き換える。
/// 新しいモデルは Load() に1行足し、名前を下の定数にも足す
/// </summary>
namespace GameAssets
{
	// モデルの名前(AssetCache::GetModel / RenderObject::Initialize に渡す)
	inline constexpr const char* kSphere = "Sphere";
	inline constexpr const char* kPlane = "Plane";
	inline constexpr const char* kAxis = "Axis";
	inline constexpr const char* kBunny = "Bunny";
	inline constexpr const char* kMultiMaterial = "MultiMaterial";
	inline constexpr const char* kMultiMesh = "MultiMesh";
	inline constexpr const char* kSuzanne = "Suzanne";
	inline constexpr const char* kTeapot = "Teapot";

	/// 画像・モデルを全部読み込み、GPUへの転送が終わるまで待つ
	void Load(AssetCache& assets, DirectXCommon& dx);
}
