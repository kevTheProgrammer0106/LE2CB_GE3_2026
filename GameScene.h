#pragma once

#include "BlendModes.h"
#include "IScene.h"
#include "InstancingObject.h"
#include "LightBuffer.h"
#include "RenderObject.h"
#include "SceneCamera.h"
#include "Sprite.h"

class DirectXCommon;
class AssetCache;

/// <summary>
/// ゲームウィンドウの中身。モデル2つ + スプライト + インスタンシングのデモ。
/// カメラ・ライト・定数バッファはこのシーンだけのもの(パーティクルエディタとは共有しない)
/// </summary>
class GameScene : public IScene
{
public:
	void Initialize(DirectXCommon& dx, const AssetCache& assets);

	void DrawUi() override;
	void Update(float deltaTime, InputManager& input, float clientWidth, float clientHeight) override;
	void Draw(const RenderContext& context) override;

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

	bool openEditorRequested_ = false;
};