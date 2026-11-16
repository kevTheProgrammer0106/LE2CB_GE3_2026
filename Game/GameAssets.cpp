#include "GameAssets.h"

#include "VirtualisEngine/Graphics/AssetCache.h"
#include "VirtualisEngine/Graphics/ShapeGeometry.h"

namespace GameAssets
{
	void Load(AssetCache& assets, DirectXCommon& dx)
	{
		assets.BeginLoad();

		// 組み込みテクスチャを、ゲームの画像で差し替える(ShapeObject・Sprite・パーティクルの既定が使う)
		assets.LoadTexture(dx, AssetCache::kCheckerTexture, "resources/uvChecker.png");
		assets.LoadTexture(dx, AssetCache::kParticleTexture, "resources/particle.jpg");

		// Sphere(プロシージャル生成。半径1。テクスチャはuvCheckerを流用)
		assets.AddModel(dx, kSphere, ShapeFactory::MakeLatLongSphere(16),
			Bounds{ { -1.0f, -1.0f, -1.0f }, { 1.0f, 1.0f, 1.0f }, true }, "resources/uvChecker.png");

		// objモデルの一覧。モデルを増やすときは、ここに1行足す。
		// ModelImportSettings には読み込み時の値(倍率・オフセット・反転・テクスチャ上書き)を書ける。
		//
		// textureOverrideの理由:
		//  - Suzanne: 常にwhite1x1を貼る。
		//  - Teapot : teapot.objはvt(テクスチャ座標)を持たず全頂点のUVが(0,0)になる。
		//             絵柄付きテクスチャだと単色の塊に見えてしまうので、white1x1にしてHalf-Lambertの陰影で形を見せる。
		ModelImportSettings whiteTexture;
		whiteTexture.textureOverride = "resources/white1x1.png";

		assets.LoadModel(dx, kPlane, "resources", "plane.obj");
		assets.LoadModel(dx, kAxis, "resources", "axis.obj");
		assets.LoadModel(dx, kBunny, "resources", "bunny.obj");
		assets.LoadModel(dx, kSuzanne, "resources", "suzanne.obj", whiteTexture);
		assets.LoadModel(dx, kTeapot, "resources", "teapot.obj", whiteTexture);
		assets.LoadModel(dx, kMultiMaterial, "resources", "multiMaterial.obj");
		assets.LoadModel(dx, kMultiMesh, "resources", "multiMesh.obj");

		assets.EndLoad(dx);
	}
}
