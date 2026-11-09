#pragma once

#include <vector>

#include "WorldTransform.h"

//===================================================================
// コライダー(当たり判定)とクリック選択用のレイ判定。GPUには依存しない
//
// Unity / Roblox と同じ考え方:
//   ・どのオブジェクトも、まず「箱(BoxCollider)」を標準の当たり判定として持つ
//   ・既定では見た目(メッシュ)の大きさにぴったり合わせる(autoFit)
//   ・箱はオブジェクトのワールド行列で回転・拡大される(OBB)ので、回しても形に沿う
//===================================================================

// 軸に平行な箱(ワールド座標)。DebugDraw::Box() など、AABB を受け取る関数に使う
struct AABB
{
	Vector3 min{ 0.0f, 0.0f, 0.0f };
	Vector3 max{ 0.0f, 0.0f, 0.0f };
};

struct Ray
{
	Vector3 origin{ 0.0f, 0.0f, 0.0f };
	Vector3 direction{ 0.0f, 0.0f, 1.0f }; // 長さ1
};

/// <summary>
/// オブジェクト1つぶんの箱コライダー。center / size はオブジェクトの「ローカル座標」で持ち、
/// ワールドへはオブジェクトのワールド行列(拡大・回転・平行移動)で運ばれる
/// </summary>
struct BoxCollider
{
	bool enabled = true;  // falseならクリックで選べない・デバッグ表示もされない
	bool autoFit = true;  // trueなら毎フレーム、メッシュの範囲に合わせて center / size を更新する
	Vector3 center{ 0.0f, 0.0f, 0.0f };
	Vector3 size{ 1.0f, 1.0f, 1.0f }; // 全幅(半径ではない)

	/// ローカルの範囲に合わせる。minThickness より薄い軸は、その厚みまで広げる(線・板でもクリックできるように)
	void FitToBounds(const AABB& localBounds, float minThickness = 0.0f);

	/// Propertiesパネルに出す編集欄
	void DrawUi();
};

/// 頂点列のバウンディングボックス(頂点が無ければ原点の大きさ0)
AABB ComputeBounds(const VertexData* vertices, size_t count);

/// 画面上の点(クライアント座標・ピクセル)から、カメラの奥へ向かうレイを作る
Ray ScreenPointToRay(const Matrix4x4& viewProjection, float pixelX, float pixelY, float clientWidth, float clientHeight);

/// レイと箱の交差判定。world はオブジェクトのワールド行列。当たったら true で、レイの始点からの距離を返す
/// (軸が直交している拡大・回転・平行移動の行列を想定している。レイの始点が箱の中なら距離0)
bool RayIntersectsBox(const Ray& ray, const BoxCollider& box, const Matrix4x4& world, float* outDistance);

/// 箱の8つの角をワールド座標で返す。並びは bit0=x, bit1=y, bit2=z (0なら最小側、1なら最大側)
void GetBoxCorners(const BoxCollider& box, const Matrix4x4& world, Vector3 outCorners[8]);

/// 箱の8つの角を囲む、軸に平行な箱(ワールド座標)
AABB GetWorldAABB(const BoxCollider& box, const Matrix4x4& world);