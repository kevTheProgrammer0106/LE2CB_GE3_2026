#include <Windows.h>
#include <cassert>

#include "Engine/Core/Application.h"
#include "Engine/Core/CrashHandler.h"
#include "Engine/Core/D3DResourceLeakChecker.h"
#include "GameAssets.h"
#include "GameScene.h"
#include "Engine/Platform/InputManager.h"
#include "Engine/Tools/ParticleEditor.h"
#include "Engine/Particle/ParticleLibrary.h"

//===================================================================
// エントリーポイント
//   エンジン(Engine/)の部品を組み合わせて、このゲーム(Game/)を作る。
//   別のゲームを作るときは、このファイルと GameScene / GameAssets を差し替える
//===================================================================
int WINAPI WinMain(_In_ HINSTANCE hInstance, _In_opt_ HINSTANCE, _In_ LPSTR, _In_ int)
{
	//システムタイマーの分解能を上げる
	timeBeginPeriod(1);

	HRESULT hr = CoInitializeEx(0, COINIT_MULTITHREADED); //マルチスレッドでCOMを利用するための初期化
	assert(SUCCEEDED(hr));
	( void ) hr;

	// 誰も補足しなかった場合にダンプを出す。main関数はじまってすぐに登録すると良い
	CrashHandler::Install();

	// 最後に破棄されるよう、最初に宣言しておく
	D3DResourceLeakChecker leakCheck;

	{
		// D3Dオブジェクトを持つものは、leakCheckより先に破棄されるようブロックで囲む。
		// 宣言順 = 生成順。破棄は逆順なので、シーン → Application(DirectX) の順に壊れる

		// ---- 土台: DirectX・パイプライン・ゲームウィンドウ ----
		Application app;
		ApplicationDesc desc;
		desc.mainWindow.title = L"GameEngine";
		desc.mainWindow.width = 1280;
		desc.mainWindow.height = 720;
		desc.targetFps = 60;
		app.Initialize(hInstance, desc);

		// ---- アセット ----
		GameAssets::Load(app.GetAssets(), app.GetDx());

		// ---- パーティクルエディタ用のウィンドウ(閉じても隠すだけ。垂直同期は取らない) ----
		WindowDesc editorDesc;
		editorDesc.title = L"Particle Editor";
		editorDesc.width = 960;
		editorDesc.height = 540;
		editorDesc.closeBehavior = WindowCloseBehavior::HideWindow;
		editorDesc.vsync = false;
		editorDesc.clearColor = { 0.0f, 0.0f, 0.0f, 1.0f }; // エディタの背景は黒
		AppWindow& editorWindow = app.AddWindow(editorDesc);

		// ---- シーン ----
		// パーティクル設定の共有置き場(GameSceneのエミッタとParticleEditorを繋ぐ。ホットリロードの中継点)
		ParticleLibrary particleLibrary;
		GameScene gameScene;
		ParticleEditor particleEditor;

		// GameSceneが先: エミッタがライブラリに登録されてから、エディタが一覧を作る
		gameScene.Initialize(app.GetDx(), app.GetAssets(), particleLibrary);
		particleEditor.Initialize(app.GetDx(), particleLibrary);

		app.AddScene(app.GetMainWindow(), gameScene);
		app.AddScene(editorWindow, particleEditor);

		// ---- フレームごとの処理 ----
		// resources/particles/*.particle の外部更新を監視する(変わっていればホットリロード)
		app.SetPreFrameCallback([&](float deltaTime)
		{
			particleLibrary.Update(deltaTime);
		});

		app.SetPostFrameCallback([&](float)
		{
			// F2 かUIのボタンでエディタウィンドウを(再)表示する
			if ( gameScene.ConsumeOpenEditorRequest() || app.GetMainWindow().GetInput().TriggerKey(DIK_F2) )
			{
				Application::BringToFront(editorWindow);
			}

			// ゲーム側の「Edit in Particle Editor」ボタン: そのエミッタを選んでエディタを表示する
			int editIndex = -1;
			if ( particleLibrary.ConsumeEditRequest(editIndex) )
			{
				particleEditor.SelectEntry(editIndex);
				Application::BringToFront(editorWindow);
			}
		});

		app.Run();
	}

	CoUninitialize();
	timeEndPeriod(1);
	return 0;
}
