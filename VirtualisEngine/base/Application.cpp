#include "Application.h"

namespace
{
	constexpr uint32_t kGameWindowWidth = 1280;
	constexpr uint32_t kGameWindowHeight = 720;
	constexpr uint32_t kEditorWindowWidth = 960;
	constexpr uint32_t kEditorWindowHeight = 540;

	// 目標FPS。0にすると固定しない(ゲームウィンドウの垂直同期=モニタのリフレッシュレートに任せる)
	constexpr uint32_t kTargetFps = 60;
}

Application::~Application()
{
	Shutdown();
}

void Application::Initialize(HINSTANCE instance)
{
	dx_.Initialize();

	// ゲームウィンドウ: 閉じたらアプリ終了。垂直同期はここだけで取る
	gameWindow_.Initialize(instance, dx_, L"GameEngine",
		kGameWindowWidth, kGameWindowHeight, WindowCloseBehavior::QuitApplication, true);

	// エディタウィンドウ: 閉じても隠すだけ。vsync待ちが2回重なってFPSが半減するのを防ぐ
	editorWindow_.Initialize(instance, dx_, L"Particle Editor",
		kEditorWindowWidth, kEditorWindowHeight, WindowCloseBehavior::HideWindow, false);

	// 共有リソース(どちらのウィンドウもRTVフォーマットは同じ)
	pipelines_.Initialize(dx_, gameWindow_.GetRenderer().GetRtvFormat());
	assets_.Initialize(dx_);

	// シーン
	// GameSceneが先: エミッタがライブラリに登録されてから、エディタが一覧を作る
	gameScene_.Initialize(dx_, assets_, particleLibrary_);
	particleEditor_.Initialize(dx_, particleLibrary_);

	frameTimer_.Initialize(kTargetFps);

	initialized_ = true;
}

void Application::Run()
{
	while ( !WinApp::ProcessMessage() )
	{
		// 目標FPSまで待ち、前フレームからの実測経過時間を得る(固定の1/60秒ではない)
		const float deltaTime = frameTimer_.Update();

		// resources/particles/*.particle の外部更新を監視する(変わっていればホットリロード)
		particleLibrary_.Update(deltaTime);

		const bool gameRendered = RunWindowFrame(gameWindow_, gameScene_, deltaTime);
		RunWindowFrame(editorWindow_, particleEditor_, deltaTime);

		// F2 かUIのボタンでエディタウィンドウを(再)表示する
		if ( gameScene_.ConsumeOpenEditorRequest() || gameWindow_.GetInput().TriggerKey(DIK_F2) )
		{
			ShowEditorWindow();
		}

		// ゲーム側の「Edit in Particle Editor」ボタン: そのエミッタを選んでエディタを表示する
		int editIndex = -1;
		if ( particleLibrary_.ConsumeEditRequest(editIndex) )
		{
			particleEditor_.SelectEntry(editIndex);
			ShowEditorWindow();
		}

		// ゲームウィンドウが描画されなかった(最小化など)ときは垂直同期で待たされないので、ここで間引く。
		// FPS固定が有効ならFrameTimerが待つので不要
		if ( !gameRendered && !frameTimer_.IsLimiting() )
		{
			Sleep(16);
		}
	}

	Shutdown();
}

bool Application::RunWindowFrame(AppWindow& window, IScene& scene, float deltaTime)
{
	// 最小化中・非表示中は更新も描画もしない。リサイズもここで適用される
	if ( !window.BeginFrame() )
	{
		return false;
	}

	const float width = static_cast< float >( window.GetRenderer().GetWidth() );
	const float height = static_cast< float >( window.GetRenderer().GetHeight() );

	// UI → 更新 → 描画 の順
	window.BeginUi();
	scene.DrawUi();
	scene.Update(deltaTime, window.GetInput(), width, height);

	window.PreDraw();
	RenderContext context;
	context.commandList = window.GetRenderer().GetCommandList();
	context.pipelines = &pipelines_;
	context.assets = &assets_;
	context.width = width;
	context.height = height;
	scene.Draw(context);
	window.PostDraw();

	return true;
}

void Application::ShowEditorWindow()
{
	WinApp& winApp = editorWindow_.GetWinApp();
	winApp.Show();
	if ( winApp.GetHwnd() )
	{
		// 最小化されていたら元に戻してから最前面へ
		if ( winApp.IsMinimized() )
		{
			ShowWindow(winApp.GetHwnd(), SW_RESTORE);
		}
		SetForegroundWindow(winApp.GetHwnd());
	}
}

void Application::Shutdown()
{
	if ( !initialized_ )
	{
		return;
	}
	initialized_ = false;

	// シーンのリソースを解放する前に、GPUの処理が終わっていることを保証する
	gameWindow_.GetRenderer().WaitForGpu();
	editorWindow_.GetRenderer().WaitForGpu();

	gameScene_.Shutdown();
	particleEditor_.Shutdown();
}