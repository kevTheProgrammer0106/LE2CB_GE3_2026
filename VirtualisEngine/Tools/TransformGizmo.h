#pragma once

#include "VirtualisEngine/Graphics/DebugDraw.h"
#include "VirtualisEngine/Math/Collider.h"
#include "VirtualisEngine/Math/WorldTransform.h"

class InputManager;

/// ギズモの道具。数字キーで切り替える(1 / 2 / 3 / 4)
enum class GizmoTool : int
{
	Move = 0,      // 1: 矢印(赤X・緑Y・青Z)をドラッグして平行移動
	Rotate,        // 2: 輪をドラッグして回転
	Scale,         // 3: 軸の先の立方体をドラッグして拡大・縮小
	Transform,     // 4: 上の3つを全部出す
};

inline constexpr const char* kGizmoToolNames[] = { "Move (1)", "Rotate (2)", "Scale (3)", "Transform (4)" };

/// ギズモで動かす値(Euler角はラジアン。MakeAffineMatrix と同じ X→Y→Z の順)。
/// 呼ぶ側が持ち主の値を入れて渡し、変わったら書き戻す
struct GizmoTransform
{
	Vector3 translate{ 0.0f, 0.0f, 0.0f };
	Vector3 rotate{ 0.0f, 0.0f, 0.0f };
	Vector3 scale{ 1.0f, 1.0f, 1.0f };
	bool canRotate = true; // false なら回転の輪を出さない(エミッタなど)
	bool canScale = true;  // false なら拡大の立方体を出さない
};

/// <summary>
/// 選んだ物を動かすための、軸ごとの操作ハンドル(Unityの移動・回転・拡大ツールに近い)。
///
///   Move     : 軸に沿った矢印。ワールド軸(X赤・Y緑・Z青)に沿って動く
///   Rotate   : 軸ごとの輪。ドラッグした角度ぶん、その軸のEuler角が変わる。輪の向きはEuler角の掛け順どおり
///              (Z輪はワールドZで固定、Y輪はZ回転ぶん、X輪は物のローカルXに沿って傾く)
///   Scale    : 物のローカル軸の先にある立方体。ドラッグで倍率が変わる。Shiftを押しながらで3軸いっしょに
///   Transform: 3つ全部を出す(矢印が長く、立方体はその手前、輪は内側)
///
/// 操作中の補助: Ctrl を押している間(または snapEnabled)は、移動 0.5 / 回転 15° / 拡大 0.1 刻みにそろえる。
/// ギズモは画面上で一定の大きさに見えるよう、カメラからの距離に合わせて大きさを変える。
/// 当たり判定は画面上のピクセル距離で行う(細い線でも掴みやすい)
/// </summary>
class TransformGizmo
{
public:
	GizmoTool tool = GizmoTool::Move;
	bool snapEnabled = false;
	float moveSnap = 0.5f;
	float rotateSnapDegrees = 15.0f;
	float scaleSnap = 0.1f;

	/// 数字キー 1〜4 で道具を切り替える。切り替えたら true
	bool HandleToolKeys(const InputManager& input);

	/// マウス入力を処理して target を書き換える。変わったら true。
	/// viewProjection: カメラの View * Projection / clientWidth・Height: 描画領域のピクセルサイズ
	bool Update(GizmoTransform& target, const InputManager& input, const Matrix4x4& view, const Matrix4x4& projection,
		float clientWidth, float clientHeight);

	/// ハンドルを線で描く。Update と同じ target・カメラで呼ぶ。DebugDraw::Begin のあとに積む
	void Draw(DebugDraw& draw, const GizmoTransform& target, const Matrix4x4& view, const Matrix4x4& projection) const;

	/// カーソルがハンドルの上にある / ドラッグ中。このとき、クリックで物の選択を変えてはいけない
	bool IsHovering() const
	{
		return hover_.kind != HandleKind::None;
	}
	bool IsDragging() const
	{
		return dragging_;
	}
	/// 対象が無い(選択が外れた)ときに呼ぶ。ドラッグとハイライトをやめる
	void Release();

private:
	enum class HandleKind
	{
		None,
		Move,
		Rotate,
		Scale,
	};
	struct Handle
	{
		HandleKind kind = HandleKind::None;
		int axis = 0; // 0:X 1:Y 2:Z
	};

	// 1フレーム分の配置(原点・大きさ・各軸の向き)
	struct Layout
	{
		Vector3 origin;
		float length = 1.0f;        // 矢印の長さ(ワールド)
		Vector3 moveAxis[3];        // 移動の軸(ワールド軸)
		Vector3 rotateAxis[3];      // 回転の輪の軸(Euler角の各軸)
		Vector3 scaleAxis[3];       // 拡大の軸(物のローカル軸)
		bool showMove = false;
		bool showRotate = false;
		bool showScale = false;
	};

	Layout MakeLayout(const GizmoTransform& target, const Matrix4x4& view) const;
	float ScaleHandleDistance() const; // 拡大の立方体を、原点から矢印の長さの何倍の所に置くか
	float RingRadiusFactor() const;
	/// 輪の上の点(axisの垂直面)。angle はラジアン
	static void RingBasis(const Vector3& axis, Vector3& u, Vector3& v);

	Handle PickHandle(const Layout& layout, const Vector2& cursor, const Matrix4x4& viewProjection, float w, float h) const;
	void BeginDrag(const Handle& handle, const GizmoTransform& target, const Layout& layout, const Ray& ray);
	bool ApplyDrag(GizmoTransform& target, const Ray& ray, bool uniformScale, bool snap);

	Handle hover_;
	bool dragging_ = false;
	Handle active_;
	GizmoTransform dragStart_;
	Layout dragLayout_;       // ドラッグ開始時の配置(動かしている間、軸が変わらないように固定する)
	float startParam_ = 0.0f; // 移動・拡大: 軸上での開始位置
	float lastAngle_ = 0.0f;  // 回転: 前回のマウスの角度
	float accumAngle_ = 0.0f; // 回転: 開始からの合計角度
};
