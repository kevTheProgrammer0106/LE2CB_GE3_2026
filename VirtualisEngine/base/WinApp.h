#pragma once
#include <Windows.h>
#include <cstdint>
#include <cassert>

// ウィンドウを閉じたときの挙動
enum class WindowCloseBehavior
{
	QuitApplication, // アプリ全体を終了する(メインウィンドウ用)
	HideWindow,      // 非表示にするだけ(エディタ等のサブウィンドウ用)
};

// 1ウィンドウ=1インスタンス。複数作っても良い(ウィンドウクラスの登録は1回だけ)
class WinApp
{
public:
	WinApp() = default;
	WinApp(const WinApp&) = delete;
	WinApp& operator=(const WinApp&) = delete;
	~WinApp()
	{
		Finalize();
	}

	// ウィンドウプロシージャ
	static LRESULT CALLBACK WindowProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);

	void Initialize(HINSTANCE instance, const wchar_t* title, uint32_t width, uint32_t height,
		WindowCloseBehavior closeBehavior = WindowCloseBehavior::QuitApplication);
	void Update();
	// 終了(デストラクタからも呼ばれる。二重に呼んでも安全)
	void Finalize();

	// このスレッドの全ウィンドウのメッセージを処理する。1フレームに1回だけ呼ぶ。
	// WM_QUITを受け取ったらtrueを返す
	static bool ProcessMessage();

	HWND GetHwnd() const
	{
		return hwnd_;
	}
	// 現在のクライアント領域サイズ(WM_SIZEで更新される)
	uint32_t GetClientWidth() const
	{
		return clientWidth_;
	}
	uint32_t GetClientHeight() const
	{
		return clientHeight_;
	}
	// ウィンドウクラスの設定を取得
	const WNDCLASSW& GetWindowClass() const
	{
		return wc_;
	}

	//--------------------------------------------------
	// 表示状態
	//--------------------------------------------------
	void Show();
	void Hide();
	bool IsVisible() const;
	bool IsMinimized() const
	{
		return minimized_;
	}
	// このウィンドウが最前面(キー入力を受け取れる状態)か
	bool IsFocused() const;

	//--------------------------------------------------
	// リサイズ
	//--------------------------------------------------
	// 前回の呼び出し以降にサイズが変わっていれば true を返し、新しいサイズを渡す。
	// 最小化中(サイズ0)は変化として扱わない
	bool ConsumeResize(uint32_t& width, uint32_t& height);

	//--------------------------------------------------
	// ImGui
	//--------------------------------------------------
	// このウィンドウのメッセージを処理するときにカレントにするImGuiContext(ImGuiContext*)。
	// WindowRendererが設定する。複数ウィンドウでそれぞれ別のコンテキストを持つために必要
	void SetImGuiContext(void* context)
	{
		imguiContext_ = context;
	}

private:
	// ウィンドウハンドル
	HWND hwnd_ = nullptr;
	static constexpr uint32_t kDefaultClientWidth = 1280;
	static constexpr uint32_t kDefaultClientHeight = 720;

	uint32_t clientWidth_ = kDefaultClientWidth;
	uint32_t clientHeight_ = kDefaultClientHeight;

	bool resizePending_ = false;
	bool minimized_ = false;
	WindowCloseBehavior closeBehavior_ = WindowCloseBehavior::QuitApplication;

	void* imguiContext_ = nullptr;

	// ウィンドウの設定
	WNDCLASSW wc_{};
};