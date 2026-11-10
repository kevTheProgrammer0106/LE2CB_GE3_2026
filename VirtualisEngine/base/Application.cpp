#include "Application.h"

namespace
{
	constexpr uint32_t kGameWindowWidth = 1280;
	constexpr uint32_t kGameWindowHeight = 720;
	constexpr uint32_t kEditorWindowWidth = 960;
	constexpr uint32_t kEditorWindowHeight = 540;

	constexpr float kDeltaTime = 1.0f / 60.0f;
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
		kGameWindowWidth, kGameWindowHeight, WindowCloseBehavior::QuitApplication, true,
		AppWindow::kDefaultClearColor);

	// エディタウィンドウ: 閉じても隠すだけ。vsync待ちが2回重なってFPSが半減するのを防ぐ
	editorWindow_.Initialize(instance, dx_, L"Particle Editor",
		kEditorWindowWidth, kEditorWindowHeight, WindowCloseBehavior::HideWindow, false,
		Vector4{ 0.0f, 0.0f, 0.0f, 1.0f }); // エディタの背景は黒

	// 共有リソース(どちらのウィンドウもRTVフォーマットは同じ)
	pipelines_.Initialize(dx_, gameWindow_.GetRenderer().GetRtvFormat());
	assets_.Initialize(dx_);

	// シーン
	gameScene_.Initialize(dx_, assets_, particleLibrary_);
	particleEditor_.Initialize(dx_, particleLibrary_);

	initialized_ = true;
}

void Application::Run()
{
	while ( !WinApp::ProcessMessage() )
	{
		const bool gameRendered = RunWindowFrame(gameWindow_, gameScene_);
		RunWindowFrame(editorWindow_, particleEditor_);

		// F2 かUIのボタンでエディタウィンドウを(再)表示する
		if ( gameScene_.ConsumeOpenEditorRequest() || gameWindow_.GetInput().TriggerKey(DIK_F2) )
		{
			ShowEditorWindow();
		}

		// ゲームウィンドウが描画されなかった(最小化など)ときは垂直同期で待たされないので、ここで間引く
		if ( !gameRendered )
		{
			Sleep(16);
		}
	}

	Shutdown();
}

bool Application::RunWindowFrame(AppWindow& window, IScene& scene)
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
	scene.Update(kDeltaTime, window.GetInput(), width, height);

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