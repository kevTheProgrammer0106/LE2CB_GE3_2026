#include "VirtualisEngine/Core/Application.h"

Application::~Application()
{
	Shutdown();
}

void Application::Initialize(HINSTANCE instance, const ApplicationDesc& desc)
{
	instance_ = instance;

	dx_.SetShaderDirectory(desc.shaderDirectory);
	dx_.SetSrvHeapSize(desc.srvHeapSize); // Initialize より前に決める
	dx_.Initialize();

	// メインウィンドウ: 閉じたらアプリ終了(desc 側で変えられる)
	AddWindow(desc.mainWindow);

	// 共有リソース(どのウィンドウもRTVフォーマットは同じ)
	pipelines_.Initialize(dx_, GetMainWindow().GetRenderer().GetRtvFormat());
	assets_.Initialize(dx_);

	frameTimer_.Initialize(desc.targetFps);

	initialized_ = true;
}

AppWindow& Application::AddWindow(const WindowDesc& desc)
{
	WindowSlot slot;
	slot.window = std::make_unique<AppWindow>();
	slot.window->Initialize(instance_, dx_, desc.title.c_str(), desc.width, desc.height,
		desc.closeBehavior, desc.vsync, desc.clearColor);
	windows_.push_back(std::move(slot));
	return *windows_.back().window;
}

void Application::AddScene(AppWindow& window, IScene& scene)
{
	for ( WindowSlot& slot : windows_ )
	{
		if ( slot.window.get() == &window )
		{
			slot.scene = &scene;
			return;
		}
	}
	assert(false && "AddScene: the window does not belong to this Application.");
}

void Application::Run()
{
	assert(initialized_ && "Call Initialize() before Run().");

	while ( !WinApp::ProcessMessage() )
	{
		// 目標FPSまで待ち、前フレームからの実測経過時間を得る(固定の1/60秒ではない)
		const float deltaTime = frameTimer_.Update();

		if ( preFrame_ )
		{
			preFrame_(deltaTime);
		}

		bool mainRendered = false;
		for ( size_t i = 0; i < windows_.size(); ++i )
		{
			const bool rendered = RunWindowFrame(windows_[i], deltaTime);
			if ( i == 0 )
			{
				mainRendered = rendered;
			}
		}

		if ( postFrame_ )
		{
			postFrame_(deltaTime);
		}

		// メインウィンドウが描画されなかった(最小化など)ときは垂直同期で待たされないので、ここで間引く。
		// FPS固定が有効ならFrameTimerが待つので不要
		if ( !mainRendered && !frameTimer_.IsLimiting() )
		{
			Sleep(16);
		}
	}

	Shutdown();
}

bool Application::RunWindowFrame(WindowSlot& slot, float deltaTime)
{
	AppWindow& window = *slot.window;

	// 最小化中・非表示中は更新も描画もしない。リサイズもここで適用される
	if ( !window.BeginFrame() )
	{
		return false;
	}
	if ( slot.scene == nullptr )
	{
		return false;
	}

	const float width = static_cast< float >( window.GetRenderer().GetWidth() );
	const float height = static_cast< float >( window.GetRenderer().GetHeight() );

	// UI → 更新 → 描画 の順
	window.BeginUi();
	slot.scene->DrawUi();
	slot.scene->Update(deltaTime, window.GetInput(), width, height);

	window.PreDraw();
	RenderContext context;
	context.commandList = window.GetRenderer().GetCommandList();
	context.pipelines = &pipelines_;
	context.assets = &assets_;
	context.width = width;
	context.height = height;
	slot.scene->Draw(context);
	window.PostDraw();

	return true;
}

void Application::BringToFront(AppWindow& window)
{
	WinApp& winApp = window.GetWinApp();
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
	for ( WindowSlot& slot : windows_ )
	{
		slot.window->GetRenderer().WaitForGpu();
	}
	for ( WindowSlot& slot : windows_ )
	{
		if ( slot.scene )
		{
			slot.scene->Shutdown();
			slot.scene = nullptr;
		}
	}
}
