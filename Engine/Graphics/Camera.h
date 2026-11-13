#pragma once

#include "Engine/Math/WorldTransform.h"

class InputManager;

//===================================================================
// カメラまわりをこの1ファイルにまとめた(旧 Camera / DebugCamera / SceneCamera)
//   CameraPose        : 位置・回転・拡大(1台分の姿勢)
//   CameraProjection  : 射影の設定(視野角・クリップ・平行投影・アスペクト)
//   CameraControls    : デバッグカメラの操作設定(速度・感度・ズームなど)
//   CameraSettings    : 上の3つ + 初期状態。ウィンドウ(ビュー)ごとに別の値で Camera::Initialize に渡せる
//   Camera            : 通常カメラ + デバッグカメラを1つに持ち、「今有効な方」のview/projectionを提供する
//===================================================================

/// <summary>
/// 1台分の姿勢。初期位置は (0, 0, -5)
/// </summary>
struct CameraPose
{
	Vector3 scale{ 1.0f, 1.0f, 1.0f };
	Vector3 rotation{ 0.0f, 0.0f, 0.0f }; // XYZ軸回りのローカル回転角(ラジアン)
	Vector3 translation{ 0.0f, 0.0f, -5.0f };
};

/// <summary>
/// 射影の設定
/// </summary>
struct CameraProjection
{
	float fovY = 0.45f;       // 垂直視野角(ラジアン)
	float nearClip = 0.1f;
	float farClip = 100.0f;
	bool orthographic = false; // true: 平行投影(2D風・設計図風の表示)
	float orthoHeight = 10.0f; // 平行投影のときの、画面に映る高さ(ワールド単位)。幅はアスペクトで決まる
	float aspect = 0.0f;       // 0: 毎フレーム「クライアント領域の幅 / 高さ」を使う(ウィンドウのリサイズに追従)。0より大きければ固定
};

/// <summary>
/// デバッグカメラの操作モード
/// </summary>
enum class DebugCameraMode
{
	FreeRoam, // 自由移動(WASD + 右ドラッグで視点回転)
	Pivot,    // 注視点を中心に周回(左ドラッグで回転 / ホイールでズーム)
};

/// <summary>
/// デバッグカメラの操作設定
/// </summary>
struct CameraControls
{
	float moveSpeed = 5.0f;         // FreeRoamの移動速度(ワールド単位 / 秒)
	float rotSensitivity = 0.005f;  // マウス1ピクセルあたりの回転量(ラジアン)
	float pitchLimit = 1.5f;        // 上下の回転の上限(ラジアン。約85度)
	float zoomSensitivity = 0.001f; // Pivotのホイール感度(ホイール1目盛りで距離が掛かる割合の指数)
	float pivotDistance = 5.0f;     // Pivotに入るときの注視点までの距離
	float minPivotDistance = 0.5f;  // 近づきすぎ防止
	float maxPivotDistance = 500.0f;
	bool allowToggle = true;        // F1で通常カメラ/デバッグカメラを切り替えられるか
	bool allowModeSwitch = true;    // Fで FreeRoam / Pivot を切り替えられるか
};

/// <summary>
/// カメラ1台ぶんの全設定。ウィンドウ(ビュー)ごとに作って Camera::Initialize に渡す
/// </summary>
struct CameraSettings
{
	CameraPose pose;                  // 通常カメラの初期姿勢
	CameraPose debugPose;             // デバッグカメラの初期姿勢
	CameraProjection projection;
	CameraControls controls;
	bool startWithDebugCamera = false;
	DebugCameraMode startMode = DebugCameraMode::FreeRoam;
	Vector3 pivotPoint{ 0.0f, 0.0f, 0.0f }; // Pivotモードの注視点(初期値)
};

/// <summary>
/// 通常カメラ(Transformに従うだけ)とデバッグカメラ(入力で動く)を1つにまとめたカメラ。
/// ウィンドウ(シーン)ごとに1つ持つので、ウィンドウ間でカメラは共有されない
/// </summary>
class Camera
{
public:
	/// 設定を渡して初期化する(省略すると既定値: 位置 (0,0,-5)、通常カメラから開始)
	void Initialize(const CameraSettings& settings = CameraSettings{});

	/// F1で通常/デバッグを切り替え、有効なカメラを更新して view/projection を作り直す。
	/// clientWidth / clientHeight: このカメラを使うウィンドウのクライアント領域(アスペクト比に使う)
	/// deltaTime: 秒(FreeRoamの移動に使う)
	void Update(InputManager& input, float clientWidth, float clientHeight, float deltaTime);

	/// 入力を使わずに行列だけ作り直す(スクリプトでポーズを変えたあとなど)
	void UpdateMatrices(float clientWidth, float clientHeight);

	/// Properties用のUI(姿勢・射影・操作設定をその場で編集できる)
	void DrawUi();

	/// 初期設定(Initializeに渡した値)に戻す
	void Reset();

	//----------------------------------------------------------------
	// 行列
	//----------------------------------------------------------------
	const Matrix4x4& GetViewMatrix() const
	{
		return view_;
	}
	const Matrix4x4& GetProjectionMatrix() const
	{
		return projection_;
	}
	const Matrix4x4& GetViewProjectionMatrix() const
	{
		return viewProjection_;
	}

	//----------------------------------------------------------------
	// 状態・設定
	//----------------------------------------------------------------
	bool IsDebugCameraActive() const
	{
		return useDebugCamera_;
	}
	void SetDebugCameraActive(bool active)
	{
		useDebugCamera_ = active;
	}
	DebugCameraMode GetMode() const
	{
		return mode_;
	}

	/// 今有効な方の姿勢(通常 or デバッグ)
	CameraPose& GetActivePose()
	{
		return useDebugCamera_ ? debugPose_ : pose_;
	}
	const CameraPose& GetActivePose() const
	{
		return useDebugCamera_ ? debugPose_ : pose_;
	}
	CameraPose& GetPose()
	{
		return pose_;
	}
	CameraPose& GetDebugPose()
	{
		return debugPose_;
	}
	CameraProjection& GetProjection()
	{
		return settings_.projection;
	}
	CameraControls& GetControls()
	{
		return settings_.controls;
	}
	const CameraSettings& GetSettings() const
	{
		return settings_;
	}

private:
	void UpdateFreeRoam(InputManager& input, float deltaTime);
	void UpdatePivot(InputManager& input);
	void UpdateDebugCamera(InputManager& input, float deltaTime);
	/// 回転角からローカルのforward(第2行)を算出。両モード共通
	Vector3 CalculateForward(const CameraPose& pose) const;

	CameraSettings settings_;
	CameraPose pose_;       // 通常カメラ
	CameraPose debugPose_;  // デバッグカメラ
	bool useDebugCamera_ = false;

	// デバッグカメラの状態
	DebugCameraMode mode_ = DebugCameraMode::FreeRoam;
	Vector3 pivotPoint_{ 0.0f, 0.0f, 0.0f };
	float pivotDistance_ = 5.0f;
	bool isCursorLocked_ = false; // カーソルをウィンドウ中央に閉じ込めるか(Qで切り替え)

	Matrix4x4 view_ = MakeIdentityMatrix();
	Matrix4x4 projection_ = MakeIdentityMatrix();
	Matrix4x4 viewProjection_ = MakeIdentityMatrix();
};
