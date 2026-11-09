#pragma once

#include <memory>
#include <string>
#include <vector>

#include "BlendModes.h"
#include "Collider.h"
#include "DebugDraw.h"
#include "IScene.h"
#include "InstancingObject.h"
#include "LightBuffer.h"
#include "Particle.h"
#include "RenderObject.h"
#include "SceneCamera.h"
#include "ShapeObject.h"
#include "Sprite.h"

class DirectXCommon;
class AssetCache;
class ParticleLibrary;

// Hierarchyに並ぶ項目の種類
enum class SceneItemKind
{
	None,
	Camera,
	Light,
	Model,          // RenderObject(object1_ / object2_)
	Shape,          // ShapeObject(箱・線・三角形・球・円錐。追加・削除できる)
	Sprite,
	InstancingDemo,
	Emitter,        // ParticleEmitter
};

// Hierarchyで選んでいる項目(kind + その種類の中での番号)
struct SceneSelection
{
	SceneItemKind kind = SceneItemKind::None;
	int index = 0;

	bool Is(SceneItemKind k, int i = 0) const
	{
		return kind == k && index == i;
	}
	bool operator==(const SceneSelection& other) const
	{
		return kind == other.kind && index == other.index;
	}
};

/// <summary>
/// ゲームウィンドウの中身。モデル2つ + 図形 + スプライト + インスタンシング + パーティクル。
/// カメラ・ライト・定数バッファはこのシーンだけのもの(パーティクルエディタとは共有しない)。
///
/// 「Hierarchy」と「Properties」の2つのImGuiウィンドウ(仮のオブジェクト管理UI)を持つ:
///   Hierarchy  : シーン内の物を一覧にする。表示の切り替え・選択・図形の追加
///   Properties : 選んだ物の値を編集する
///
/// クリック選択(Unity / Roblox 風):
///   どの物も、まず「箱(BoxCollider)」を標準の当たり判定として持つ(既定では見た目にぴったり)。
///   ゲーム画面で左クリックすると、カーソルからのレイが最初に当たった物が選ばれ(Hierarchyにも反映)、
///   何もない所をクリックすると選択が外れる。カーソルが乗っている物は黄色、選択中はオレンジの枠で表示する。
///   F4(または Game Debug のチェック)で、全コライダーの枠(緑)を常に表示する
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

	/// 図形をシーンに追加する(コードからもHierarchyの「Add」ボタンからも使う)。
	/// 名前が空なら「Box 1」のように自動で付ける。追加した図形は Hierarchy に現れる
	/// flat が true なら2D版(Box → Quad、Sphere → Circle)
	ShapeObject* AddShape(ShapeType type, const std::string& name = "", bool flat = false);

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

	// --- 図形 ---
	DirectXCommon* dx_ = nullptr; // 図形を後から追加するときのGPUバッファ生成に使う
	std::vector<std::unique_ptr<ShapeObject>> shapes_;
	int shapeCounters_[kShapeTypeCount][2] = {}; // 自動で付ける名前の連番([種類][0:3D版 1:2D版])

	// --- Hierarchy / Properties ---
	static constexpr int kModelCount = 2;
	RenderObject* models_[kModelCount] = { &object1_, &object2_ };
	bool modelVisible_[kModelCount] = { true, true };

	SceneSelection selection_;
	SceneSelection hover_; // カーソルが乗っている物(毎フレーム作り直す)

	// --- コライダーとクリック選択 ---
	const AssetCache* assets_ = nullptr;
	DebugDraw debug_;
	BoxCollider modelColliders_[kModelCount]; // モデルの標準コライダー(自動でモデルの範囲に合わせる)
	bool showColliders_ = false;

	// コライダーを持つ項目(クリック判定とデバッグ表示で共通に使う)
	struct ColliderItem
	{
		SceneSelection id;
		BoxCollider collider;
		Matrix4x4 world;
		bool isEmitter;
	};
	/// 今シーンにある、3D空間のコライダーを全部集める(非表示の物は含めない)
	std::vector<ColliderItem> CollectColliders() const;
	/// カーソル位置にある物を返す(何も無ければ kind = None)。スプライトは画面の手前なので最優先
	SceneSelection PickAt(const Vector2& cursor, float clientWidth, float clientHeight) const;
	void UpdatePicking(InputManager& input, float clientWidth, float clientHeight);
	void BuildDebugLines();

	void DrawHierarchyUi();
	void DrawPropertiesUi();
	/// Hierarchyの1行(表示チェック + 選択)。visibleがnullptrならチェックなし
	void HierarchyRow(const char* label, SceneItemKind kind, int index, bool* visible);
	void DeleteShape(int index);

	bool openEditorRequested_ = false;
};