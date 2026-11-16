#pragma once

#include <Windows.h>

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "VirtualisEngine/Platform/AppWindow.h"
#include "VirtualisEngine/Graphics/AssetCache.h"
#include "VirtualisEngine/Graphics/DirectXCommon.h"
#include "VirtualisEngine/Core/FrameTimer.h"
#include "VirtualisEngine/Core/IScene.h"
#include "VirtualisEngine/Graphics/PipelineManager.h"

/// <summary>ウィンドウ1枚の設定</summary>
struct WindowDesc
{
	std::wstring title = L"Game";
	uint32_t width = 1280;
	uint32_t height = 720;
	/// QuitApplication: 閉じたらアプリ終了 / HideWindow: 閉じても隠すだけ(エディタなど)
	WindowCloseBehavior closeBehavior = WindowCloseBehavior::QuitApplication;
	/// 垂直同期。ウィンドウが複数あるときは、1つだけ true にする(待ちが重なってFPSが半減するため)
	bool vsync = true;
	Vector4 clearColor = AppWindow::kDefaultClearColor;
};

/// <summary>アプリ全体の設定</summary>
struct ApplicationDesc
{
	/// 最初のウィンドウ(閉じるとアプリが終わるメインウィンドウにする)
	WindowDesc mainWindow;
	/// 目標FPS。0にすると固定しない(垂直同期=モニタのリフレッシュレートに任せる)
	uint32_t targetFps = 60;
	/// エンジンのシェーダー(*.hlsl)があるフォルダ。exeを動かす作業フォルダからの相対パス
	std::wstring shaderDirectory = L"resources/GraphicShaders/";
};

/// <summary>
/// ゲームを動かす土台。DirectX・パイプライン・アセットを持ち、ウィンドウとその中身(IScene)を回す。
///
///   Application app;
///   app.Initialize(hInstance, desc);                    // DX・パイプライン・組み込みテクスチャ
///   (ここでアセットを読む)  MyAssets::Load(app.GetAssets(), app.GetDx());
///   MyScene scene;  scene.Initialize(app.GetDx(), app.GetAssets());
///   app.AddScene(app.GetMainWindow(), scene);           // メインウィンドウに表示する
///   app.Run();                                          // メインウィンドウが閉じるまで回る
///
/// 2枚目以降のウィンドウは AddWindow() で作り、AddScene() で中身を載せる。
/// シーンは Application より「後に」宣言すること(先に破棄されるので、DirectXが先に消えない)。
/// 破棄は逆順: scene → window → AssetCache → PipelineManager → DirectXCommon
/// </summary>
class Application
{
public:
	Application() = default;
	Application(const Application&) = delete;
	Application& operator=(const Application&) = delete;
	~Application();

	/// DirectXを初期化し、メインウィンドウとパイプライン・組み込みアセットを作る
	void Initialize(HINSTANCE instance, const ApplicationDesc& desc = {});

	/// 2枚目以降のウィンドウを作る(Initialize の後)
	AppWindow& AddWindow(const WindowDesc& desc);
	/// ウィンドウにシーンを載せる。1つのウィンドウに載せられるシーンは1つ(複数なら最後のもの)
	void AddScene(AppWindow& window, IScene& scene);

	/// 毎フレーム、ウィンドウの更新・描画の前 / 後に呼ばれる処理(省略可)。引数は経過時間(秒)。
	/// 例: 設定ファイルの監視は前、F2キーなどの入力による操作は後
	void SetPreFrameCallback(std::function<void(float)> callback)
	{
		preFrame_ = std::move(callback);
	}
	void SetPostFrameCallback(std::function<void(float)> callback)
	{
		postFrame_ = std::move(callback);
	}

	/// メインウィンドウが閉じられるまで回る。終わるとき、載せたシーンの Shutdown() が呼ばれる
	void Run();

	/// ウィンドウを表示して最前面に出す(HideWindow で隠れていたものを戻す)
	static void BringToFront(AppWindow& window);

	DirectXCommon& GetDx()
	{
		return dx_;
	}
	PipelineManager& GetPipelines()
	{
		return pipelines_;
	}
	AssetCache& GetAssets()
	{
		return assets_;
	}
	AppWindow& GetMainWindow()
	{
		return *windows_.front().window;
	}

private:
	struct WindowSlot
	{
		std::unique_ptr<AppWindow> window;
		IScene* scene = nullptr;
	};

	/// 1ウィンドウ分の「UI → 更新 → 描画」。描画したら true
	bool RunWindowFrame(WindowSlot& slot, float deltaTime);
	/// GPUを待ち、シーンのリソースを解放できる状態にする
	void Shutdown();

	// 共有リソース
	DirectXCommon dx_;
	PipelineManager pipelines_;
	AssetCache assets_;

	// ウィンドウ(先頭がメイン)
	HINSTANCE instance_ = nullptr;
	std::vector<WindowSlot> windows_;

	// フレーム時間の計測とFPS固定
	FrameTimer frameTimer_;

	std::function<void(float)> preFrame_;
	std::function<void(float)> postFrame_;

	bool initialized_ = false;
};
