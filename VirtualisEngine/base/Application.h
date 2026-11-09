#pragma once

#include <Windows.h>

#include "AppWindow.h"
#include "AssetCache.h"
#include "DirectXCommon.h"
#include "FrameTimer.h"
#include "GameScene.h"
#include "ParticleEditor.h"
#include "ParticleLibrary.h"
#include "PipelineManager.h"

/// <summary>
/// アプリ全体の所有者。ウィンドウ2つ(ゲーム / パーティクルエディタ)と、それらが共有するリソースを持つ。
///
/// メンバーの宣言順 = 生成順。破棄は逆順になるので、
///   scene → window → AssetCache → PipelineManager → DirectXCommon
/// の順に壊れ、依存先が先に消えることはない
/// </summary>
class Application
{
public:
	Application() = default;
	Application(const Application&) = delete;
	Application& operator=(const Application&) = delete;
	~Application();

	void Initialize(HINSTANCE instance);
	/// ゲームウィンドウが閉じられるまで回る
	void Run();

private:
	/// 1ウィンドウ分の「UI → 更新 → 描画」。描画したら true
	bool RunWindowFrame(AppWindow& window, IScene& scene, float deltaTime);
	/// エディタウィンドウを表示して最前面に出す
	void ShowEditorWindow();
	/// GPUを待ち、シーンのリソースを解放できる状態にする
	void Shutdown();

	// 共有リソース
	DirectXCommon dx_;
	PipelineManager pipelines_;
	AssetCache assets_;
	// パーティクル設定の共有置き場(GameSceneのエミッタとParticleEditorを繋ぐ。ホットリロードの中継点)
	ParticleLibrary particleLibrary_;

	// ウィンドウ
	AppWindow gameWindow_;
	AppWindow editorWindow_;

	// フレーム時間の計測とFPS固定
	FrameTimer frameTimer_;

	// ウィンドウの中身
	GameScene gameScene_;
	ParticleEditor particleEditor_;

	bool initialized_ = false;
};