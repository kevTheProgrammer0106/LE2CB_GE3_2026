#define NOMINMAX
#include "TransformGizmo.h"

#include <algorithm>
#include <cmath>

#include "VirtualisEngine/Math/Collider.h"
#include "VirtualisEngine/Platform/InputManager.h"

namespace
{
	constexpr float kPickPixels = 10.0f;    // 矢印・輪をつかめる画面上の距離
	constexpr float kCubePickPixels = 13.0f; // 立方体をつかめる距離
	constexpr int kRingSegments = 48;
	constexpr float kMinScale = 0.01f;

	// ワールドの点 → クライアント座標(ピクセル)。カメラの後ろなら false
	bool WorldToScreen(const Matrix4x4& vp, const Vector3& p, float w, float h, Vector2& out)
	{
		const float cx = p.x * vp.m[0][0] + p.y * vp.m[1][0] + p.z * vp.m[2][0] + vp.m[3][0];
		const float cy = p.x * vp.m[0][1] + p.y * vp.m[1][1] + p.z * vp.m[2][1] + vp.m[3][1];
		const float cw = p.x * vp.m[0][3] + p.y * vp.m[1][3] + p.z * vp.m[2][3] + vp.m[3][3];
		if ( cw <= 1e-4f )
		{
			return false;
		}
		out.x = ( cx / cw * 0.5f + 0.5f ) * w;
		out.y = ( 1.0f - ( cy / cw * 0.5f + 0.5f ) ) * h;
		return true;
	}

	// 点から線分までの距離(2D)
	float DistanceToSegment(const Vector2& p, const Vector2& a, const Vector2& b)
	{
		const float abx = b.x - a.x;
		const float aby = b.y - a.y;
		const float lengthSq = abx * abx + aby * aby;
		float t = 0.0f;
		if ( lengthSq > 1e-6f )
		{
			t = std::clamp(( ( p.x - a.x ) * abx + ( p.y - a.y ) * aby ) / lengthSq, 0.0f, 1.0f);
		}
		const float dx = a.x + abx * t - p.x;
		const float dy = a.y + aby * t - p.y;
		return std::sqrt(dx * dx + dy * dy);
	}

	// レイと「点origin・向きaxis の直線」がいちばん近づく所の、直線上のパラメータ。平行なら false
	bool ClosestParamOnAxis(const Ray& ray, const Vector3& origin, const Vector3& axis, float& out)
	{
		const Vector3 w0 = origin - ray.origin;
		const float b = Dot(axis, ray.direction);
		const float d = Dot(axis, w0);
		const float e = Dot(ray.direction, w0);
		const float denom = 1.0f - b * b;
		if ( denom < 1e-4f )
		{
			return false;
		}
		out = ( b * e - d ) / denom;
		return true;
	}

	// レイと「原点を通る法線normalの平面」の交点
	bool IntersectPlane(const Ray& ray, const Vector3& origin, const Vector3& normal, Vector3& out)
	{
		const float denom = Dot(normal, ray.direction);
		if ( std::fabs(denom) < 1e-4f )
		{
			return false;
		}
		const float t = Dot(normal, origin - ray.origin) / denom;
		if ( t < 0.0f )
		{
			return false;
		}
		out = ray.origin + ray.direction * t;
		return true;
	}

	float WrapAngle(float a)
	{
		while ( a > kPi )
		{
			a -= 2.0f * kPi;
		}
		while ( a < -kPi )
		{
			a += 2.0f * kPi;
		}
		return a;
	}

	float SnapTo(float value, float step)
	{
		return step > 1e-6f ? std::round(value / step) * step : value;
	}

	const Vector4& AxisColor(int axis)
	{
		return axis == 0 ? DebugDraw::Color::kAxisX : ( axis == 1 ? DebugDraw::Color::kAxisY : DebugDraw::Color::kAxisZ );
	}
}

//===================================================================
// 配置
//===================================================================
float TransformGizmo::ScaleHandleDistance() const
{
	return tool == GizmoTool::Scale ? 1.0f : 0.62f; // Transform のときは矢印の手前に置いて、重ならないようにする
}

float TransformGizmo::RingRadiusFactor() const
{
	return tool == GizmoTool::Rotate ? 0.9f : 0.45f;
}

void TransformGizmo::RingBasis(const Vector3& axis, Vector3& u, Vector3& v)
{
	const Vector3 helper = ( std::fabs(axis.y) < 0.9f ) ? Vector3{ 0.0f, 1.0f, 0.0f } : Vector3{ 1.0f, 0.0f, 0.0f };
	u = Normalize(Cross(helper, axis));
	v = Cross(axis, u); // u → v が「正の回転」(rotateのEuler角が増える向き)
}

TransformGizmo::Layout TransformGizmo::MakeLayout(const GizmoTransform& target, const Matrix4x4& view) const
{
	Layout layout;
	layout.origin = target.translate;

	// カメラ位置 = ビュー行列の逆行列の平行移動。遠いほどギズモを大きくして、画面上で同じ大きさに見せる
	const Matrix4x4 camera = Inverse(view);
	const Vector3 cameraPosition = { camera.m[3][0], camera.m[3][1], camera.m[3][2] };
	layout.length = std::max(Length(layout.origin - cameraPosition), 0.5f) * 0.2f;

	layout.moveAxis[0] = { 1.0f, 0.0f, 0.0f };
	layout.moveAxis[1] = { 0.0f, 1.0f, 0.0f };
	layout.moveAxis[2] = { 0.0f, 0.0f, 1.0f };

	// Euler角(R = Rx * Ry * Rz、行ベクトルなので Rx が最初に掛かる)の各角を変えたとき、
	// その回転は「あとから掛かる回転」に運ばれる。ワールドでの回転軸は:
	//   X角 → X * Ry * Rz(物のローカルX軸) / Y角 → Y * Rz / Z角 → ワールドZ
	const Matrix4x4 rz = MakeRotateZMatrix(target.rotate.z);
	const Matrix4x4 ryz = Multiply(MakeRotateYMatrix(target.rotate.y), rz);
	layout.rotateAxis[0] = Normalize(Transform(Vector3{ 1.0f, 0.0f, 0.0f }, ryz));
	layout.rotateAxis[1] = Normalize(Transform(Vector3{ 0.0f, 1.0f, 0.0f }, rz));
	layout.rotateAxis[2] = { 0.0f, 0.0f, 1.0f };

	// 拡大はローカル軸(回転行列の各行)に沿う
	const Matrix4x4 rotation = Multiply(MakeRotateXMatrix(target.rotate.x),
		Multiply(MakeRotateYMatrix(target.rotate.y), MakeRotateZMatrix(target.rotate.z)));
	for ( int axis = 0; axis < 3; ++axis )
	{
		layout.scaleAxis[axis] = Normalize({ rotation.m[axis][0], rotation.m[axis][1], rotation.m[axis][2] });
	}

	layout.showMove = ( tool == GizmoTool::Move || tool == GizmoTool::Transform );
	layout.showRotate = ( tool == GizmoTool::Rotate || tool == GizmoTool::Transform ) && target.canRotate;
	layout.showScale = ( tool == GizmoTool::Scale || tool == GizmoTool::Transform ) && target.canScale;
	return layout;
}

//===================================================================
// 入力
//===================================================================
bool TransformGizmo::HandleToolKeys(const InputManager& input)
{
	static const struct { BYTE key; GizmoTool tool; } kKeys[] = {
		{ DIK_1, GizmoTool::Move }, { DIK_2, GizmoTool::Rotate }, { DIK_3, GizmoTool::Scale }, { DIK_4, GizmoTool::Transform },
	};
	for ( const auto& entry : kKeys )
	{
		if ( input.TriggerKey(entry.key) )
		{
			if ( tool != entry.tool )
			{
				tool = entry.tool;
				Release(); // ドラッグ中に道具が変わらないように
				return true;
			}
		}
	}
	return false;
}

void TransformGizmo::Release()
{
	dragging_ = false;
	hover_ = {};
	active_ = {};
}

TransformGizmo::Handle TransformGizmo::PickHandle(const Layout& layout, const Vector2& cursor,
	const Matrix4x4& viewProjection, float w, float h) const
{
	Handle best;
	float bestScore = 1e30f; // 「距離 / つかめる距離」が小さいほど優先

	auto consider = [&](HandleKind kind, int axis, float distance, float limit)
	{
		const float score = distance / limit;
		if ( score <= 1.0f && score < bestScore )
		{
			bestScore = score;
			best = { kind, axis };
		}
	};

	Vector2 screenOrigin{};
	if ( !WorldToScreen(viewProjection, layout.origin, w, h, screenOrigin) )
	{
		return best;
	}

	for ( int axis = 0; axis < 3; ++axis )
	{
		if ( layout.showScale )
		{
			Vector2 cube{};
			if ( WorldToScreen(viewProjection, layout.origin + layout.scaleAxis[axis] * ( layout.length * ScaleHandleDistance() ), w, h, cube) )
			{
				const float dx = cube.x - cursor.x;
				const float dy = cube.y - cursor.y;
				consider(HandleKind::Scale, axis, std::sqrt(dx * dx + dy * dy), kCubePickPixels);
			}
		}
		if ( layout.showMove )
		{
			Vector2 tip{};
			if ( WorldToScreen(viewProjection, layout.origin + layout.moveAxis[axis] * layout.length, w, h, tip) )
			{
				consider(HandleKind::Move, axis, DistanceToSegment(cursor, screenOrigin, tip), kPickPixels);
			}
		}
		if ( layout.showRotate )
		{
			Vector3 u, v;
			RingBasis(layout.rotateAxis[axis], u, v);
			const float radius = layout.length * RingRadiusFactor();
			Vector2 previous{};
			bool hasPrevious = false;
			float ringDistance = 1e30f;
			for ( int i = 0; i <= kRingSegments; ++i )
			{
				const float a = 2.0f * kPi * static_cast< float >( i ) / kRingSegments;
				Vector2 point{};
				const bool visible = WorldToScreen(viewProjection, layout.origin + ( u * std::cos(a) + v * std::sin(a) ) * radius, w, h, point);
				if ( visible && hasPrevious )
				{
					ringDistance = std::min(ringDistance, DistanceToSegment(cursor, previous, point));
				}
				previous = point;
				hasPrevious = visible;
			}
			consider(HandleKind::Rotate, axis, ringDistance, kPickPixels * 0.8f); // 輪は矢印より少し優先度を下げる
		}
	}
	return best;
}

void TransformGizmo::BeginDrag(const Handle& handle, const GizmoTransform& target, const Layout& layout, const Ray& ray)
{
	dragging_ = true;
	active_ = handle;
	dragStart_ = target;
	dragLayout_ = layout;
	accumAngle_ = 0.0f;
	startParam_ = 0.0f;

	if ( handle.kind == HandleKind::Move )
	{
		ClosestParamOnAxis(ray, layout.origin, layout.moveAxis[handle.axis], startParam_);
	} else if ( handle.kind == HandleKind::Scale )
	{
		if ( !ClosestParamOnAxis(ray, layout.origin, layout.scaleAxis[handle.axis], startParam_) || std::fabs(startParam_) < 1e-3f )
		{
			startParam_ = layout.length * ScaleHandleDistance(); // 取れないときは、立方体の位置を基準にする
		}
	} else if ( handle.kind == HandleKind::Rotate )
	{
		Vector3 point{};
		Vector3 u, v;
		RingBasis(layout.rotateAxis[handle.axis], u, v);
		if ( IntersectPlane(ray, layout.origin, layout.rotateAxis[handle.axis], point) )
		{
			const Vector3 rel = point - layout.origin;
			lastAngle_ = std::atan2(Dot(rel, v), Dot(rel, u));
		} else
		{
			lastAngle_ = 0.0f;
		}
	}
}

bool TransformGizmo::ApplyDrag(GizmoTransform& target, const Ray& ray, bool uniformScale, bool snap)
{
	const int axis = active_.axis;
	GizmoTransform result = dragStart_;

	if ( active_.kind == HandleKind::Move )
	{
		float param = 0.0f;
		if ( !ClosestParamOnAxis(ray, dragLayout_.origin, dragLayout_.moveAxis[axis], param) )
		{
			return false;
		}
		float delta = param - startParam_;
		if ( snap )
		{
			delta = SnapTo(delta, moveSnap);
		}
		result.translate = dragStart_.translate + dragLayout_.moveAxis[axis] * delta;
	} else if ( active_.kind == HandleKind::Scale )
	{
		float param = 0.0f;
		if ( !ClosestParamOnAxis(ray, dragLayout_.origin, dragLayout_.scaleAxis[axis], param) )
		{
			return false;
		}
		const float ratio = param / startParam_;
		auto scaled = [&](float start)
		{
			float value = start * ratio;
			if ( snap )
			{
				value = SnapTo(value, scaleSnap);
			}
			return std::max(value, kMinScale);
		};
		if ( uniformScale )
		{
			result.scale = { scaled(dragStart_.scale.x), scaled(dragStart_.scale.y), scaled(dragStart_.scale.z) };
		} else
		{
			float* components[3] = { &result.scale.x, &result.scale.y, &result.scale.z };
			*components[axis] = scaled(*components[axis]);
		}
	} else if ( active_.kind == HandleKind::Rotate )
	{
		Vector3 point{};
		Vector3 u, v;
		RingBasis(dragLayout_.rotateAxis[axis], u, v);
		if ( !IntersectPlane(ray, dragLayout_.origin, dragLayout_.rotateAxis[axis], point) )
		{
			return false;
		}
		const Vector3 rel = point - dragLayout_.origin;
		const float angle = std::atan2(Dot(rel, v), Dot(rel, u));
		accumAngle_ += WrapAngle(angle - lastAngle_); // 何周回しても続くように、差を足していく
		lastAngle_ = angle;

		float delta = accumAngle_;
		if ( snap )
		{
			delta = SnapTo(delta, rotateSnapDegrees * kDegToRad);
		}
		float* components[3] = { &result.rotate.x, &result.rotate.y, &result.rotate.z };
		*components[axis] += delta;
	} else
	{
		return false;
	}

	target = result;
	return true;
}

bool TransformGizmo::Update(GizmoTransform& target, const InputManager& input, const Matrix4x4& view,
	const Matrix4x4& projection, float clientWidth, float clientHeight)
{
	const Matrix4x4 viewProjection = Multiply(view, projection);
	const Layout layout = MakeLayout(target, view);

	Vector2 cursor{};
	const bool hasCursor = input.GetCursorClientPos(cursor);

	// --- ドラッグ中 ---
	if ( dragging_ )
	{
		if ( !input.PushMouseButton(0) || !hasCursor )
		{
			dragging_ = false; // 離した
			return false;
		}
		const Ray ray = ScreenPointToRay(viewProjection, cursor.x, cursor.y, clientWidth, clientHeight);
		const bool snap = snapEnabled || input.PushKey(DIK_LCONTROL) || input.PushKey(DIK_RCONTROL);
		const bool uniform = input.PushKey(DIK_LSHIFT) || input.PushKey(DIK_RSHIFT);
		return ApplyDrag(target, ray, uniform, snap);
	}

	// --- ハンドルの上にあるか ---
	hover_ = {};
	// UIの上・カメラ操作中(右クリック)はつかめない
	if ( !hasCursor || input.IsMouseCaptured() || input.PushMouseButton(1) )
	{
		return false;
	}
	hover_ = PickHandle(layout, cursor, viewProjection, clientWidth, clientHeight);

	if ( hover_.kind != HandleKind::None && input.TriggerMouseButton(0) )
	{
		const Ray ray = ScreenPointToRay(viewProjection, cursor.x, cursor.y, clientWidth, clientHeight);
		BeginDrag(hover_, target, layout, ray);
	}
	return false;
}

//===================================================================
// 描画
//===================================================================
void TransformGizmo::Draw(DebugDraw& draw, const GizmoTransform& target, const Matrix4x4& view, const Matrix4x4& projection) const
{
	( void ) projection;
	// ドラッグ中は開始時の配置で描く(掴んでいる軸が動かない)。それ以外は今の値で作る
	const Layout layout = dragging_ ? dragLayout_ : MakeLayout(target, view);
	const Vector3 origin = dragging_ ? target.translate : layout.origin;

	auto highlighted = [&](HandleKind kind, int axis) -> bool
	{
		const Handle& h = dragging_ ? active_ : hover_;
		return h.kind == kind && h.axis == axis;
	};

	for ( int axis = 0; axis < 3; ++axis )
	{
		if ( layout.showMove )
		{
			const Vector4& color = highlighted(HandleKind::Move, axis) ? DebugDraw::Color::kHover : AxisColor(axis);
			draw.Arrow(origin, origin + layout.moveAxis[axis] * layout.length, color);
		}
		if ( layout.showScale )
		{
			const Vector4& color = highlighted(HandleKind::Scale, axis) ? DebugDraw::Color::kHover : AxisColor(axis);
			const Vector3 center = origin + layout.scaleAxis[axis] * ( layout.length * ScaleHandleDistance() );
			if ( tool == GizmoTool::Scale )
			{
				draw.Line(origin, center, color);
			}
			const float half = layout.length * 0.06f;
			draw.BoxAt(center, half, half, half, color);
		}
		if ( layout.showRotate )
		{
			const Vector4& color = highlighted(HandleKind::Rotate, axis) ? DebugDraw::Color::kHover : AxisColor(axis);
			Vector3 u, v;
			RingBasis(layout.rotateAxis[axis], u, v);
			const float radius = layout.length * RingRadiusFactor();
			Vector3 previous{};
			for ( int i = 0; i <= kRingSegments; ++i )
			{
				const float a = 2.0f * kPi * static_cast< float >( i ) / kRingSegments;
				const Vector3 point = origin + ( u * std::cos(a) + v * std::sin(a) ) * radius;
				if ( i > 0 )
				{
					draw.Line(previous, point, color);
				}
				previous = point;
			}
		}
	}
}
