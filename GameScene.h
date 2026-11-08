#pragma once

#include "BlendModes.h"
#include "IScene.h"
#include "InstancingObject.h"
#include "LightBuffer.h"
#include "Particle.h"
#include "RenderObject.h"
#include "SceneCamera.h"
#include "Sprite.h"

class DirectXCommon;
class AssetCache;
class ParticleLibrary;

/// <summary>
/// ゲームウィンドウの中身。モデル2つ + スプライト + インスタンシングのデモ。
/// カメラ・ライト・定数バッファはこのシーンだけのもの(パーティクルエディタとは共有しない)
/// </summary>
class GameScene : public IScene
{
public:
	/// library: パーティクル設定の共有置き場。ここで宣言したエミッタは自動で登録され、ParticleEditorから編集できる
	void Initialize(DirectXCommon& dx, const AssetCache& assets, ParticleLibrary& library);

	void DrawUi() override;
	void Update(float deltaTime, InputManager& input, float clientWidth, float clientHeight) override;
	void Draw(const RenderContext& context) override;
	void Shutdown() override;

	/// UIの「Open Particle Editor」ボタンが押されていたら true を返し、フラグを下ろす
	bool ConsumeOpenEditorRequest();

private:
	SceneCamera camera_;
	LightBuffer light_;

	RenderObject object1_;
	RenderObject object2_;
	ParticleBlendMode modelBlendMode_ = ParticleBlendMode::None;

	Sprite sprite_;
	InstancingObject instancing_;

	// --- パーティクル ---
	// エミッタを増やすときは、ここに宣言して Initialize() で名前と初期設定を渡すだけ。
	// 名前でライブラリに登録され、ParticleEditorの一覧に出る(値の編集・Apply・保存はエディタ側)
	ParticleEmitter fire_;      // 3Dビルボード(ワールドに置く炎)
	ParticleEmitter sparkle_;   // 2Dスクリーン(画面にピクセル座標で重ねる火花)
	ParticleEmitter* emitters_[2] = { &fire_, &sparkle_ }; // 更新・描画のループ用

	/// 指定した描画方式のエミッタだけ描く(3D → 2D の順で呼ぶ)
	void DrawEmitters(const RenderContext& context, ParticleRenderMode mode, D3D12_GPU_VIRTUAL_ADDRESS lightAddress);

	bool openEditorRequested_ = false;
};