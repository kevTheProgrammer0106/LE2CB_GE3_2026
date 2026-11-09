#include "WinApp.h"

// timeBeginPeriod / timeEndPeriod はImGuiの有無に関係なくmain.cppで使うので、ifdefの外に出す
#pragma comment(lib, "winmm.lib")

#ifdef USE_IMGUI
#include "externals/imgui/imgui.h"
#include "externals/imgui/imgui_impl_dx12.h"
#include "externals/imgui/imgui_impl_win32.h"

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
#endif

namespace
{
	// 全WinApp共通のウィンドウクラス名(登録は1回だけ)
	constexpr const wchar_t* kWindowClassName = L"EngineWindowClass";
}

// ウィンドウプロシージャ
LRESULT CALLBACK WinApp::WindowProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
	// どのWinApp宛のメッセージかを、ウィンドウに紐づけたポインタから取り出す
	WinApp* self = nullptr;
	if ( msg == WM_NCCREATE )
	{
		auto* createStruct = reinterpret_cast< CREATESTRUCTW* >( lparam );
		self = static_cast< WinApp* >( createStruct->lpCreateParams );
		SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast< LONG_PTR >( self ));
		if ( self )
		{
			self->hwnd_ = hwnd; // CreateWindowW が戻る前のメッセージでも使えるように
		}
	} else
	{
		self = reinterpret_cast< WinApp* >( GetWindowLongPtrW(hwnd, GWLP_USERDATA) );
	}

#ifdef USE_IMGUI
	if ( self && self->imguiContext_ )
	{
		// このウィンドウ専用のImGuiコンテキストに切り替えてから渡す
		ImGui::SetCurrentContext(static_cast< ImGuiContext* >( self->imguiContext_ ));
		if ( ImGui_ImplWin32_WndProcHandler(hwnd, msg, wparam, lparam) )
		{
			return TRUE;
		}
	}
#endif

	if ( self )
	{
		switch ( msg )
		{
			case WM_CLOSE:
				if ( self->closeBehavior_ == WindowCloseBehavior::HideWindow )
				{
					// 破棄せず隠すだけ。アプリは終了しない
					ShowWindow(hwnd, SW_HIDE);
					return 0;
				}
				break; // QuitApplication: DefWindowProc が DestroyWindow を呼ぶ

			case WM_SIZE:
				if ( wparam == SIZE_MINIMIZED )
				{
					self->minimized_ = true;
					return 0;
				}
				self->minimized_ = false;
				{
					const uint32_t width = LOWORD(lparam);
					const uint32_t height = HIWORD(lparam);
					if ( width != 0 && height != 0 && ( width != self->clientWidth_ || height != self->clientHeight_ ) )
					{
						self->clientWidth_ = width;
						self->clientHeight_ = height;
						self->resizePending_ = true;
					}
				}
				return 0;

			case WM_DESTROY:
				if ( self->closeBehavior_ == WindowCloseBehavior::QuitApplication )
				{
					PostQuitMessage(0);
				}
				self->hwnd_ = nullptr;
				break;
		}
	}

	return DefWindowProcW(hwnd, msg, wparam, lparam);
}

void WinApp::Initialize(HINSTANCE instance, const wchar_t* title, uint32_t width, uint32_t height, WindowCloseBehavior closeBehavior)
{
	assert(hwnd_ == nullptr && "WinApp::Initialize called twice");

	closeBehavior_ = closeBehavior;
	clientWidth_ = width;
	clientHeight_ = height;
	resizePending_ = false;
	minimized_ = false;

	// ウィンドウクラスの設定
	wc_ = {};
	wc_.lpfnWndProc = WindowProc;
	wc_.lpszClassName = kWindowClassName;
	wc_.hInstance = instance;
	wc_.hCursor = LoadCursor(nullptr, IDC_ARROW);

	// ウィンドウクラスはプロセスで1回だけ登録する(2つ目以降のウィンドウでは登録しない)
	static bool classRegistered = false;
	if ( !classRegistered )
	{
		ATOM atom = RegisterClassW(&wc_);
		assert(atom != 0);
		( void ) atom;
		classRegistered = true;
	}

	RECT wrc = { 0, 0, static_cast< LONG >( width ), static_cast< LONG >( height ) };
	AdjustWindowRect(&wrc, WS_OVERLAPPEDWINDOW, false);

	HWND hwnd = CreateWindowW(
		kWindowClassName, // クラス名
		title,            // ウィンドウのタイトル
		WS_OVERLAPPEDWINDOW,
		CW_USEDEFAULT,
		CW_USEDEFAULT,
		wrc.right - wrc.left,
		wrc.bottom - wrc.top,
		nullptr,
		nullptr,
		instance,
		this // WM_NCCREATE で GWLP_USERDATA に保存される
	);
	assert(hwnd != nullptr);
	hwnd_ = hwnd;

	ShowWindow(hwnd_, SW_SHOW);
	UpdateWindow(hwnd_);
}

void WinApp::Update()
{
}

void WinApp::Finalize()
{
	if ( hwnd_ )
	{
		HWND hwnd = hwnd_;
		hwnd_ = nullptr;
		// 自分で壊すときはWM_DESTROYの処理(PostQuitMessage等)を走らせない
		SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
		DestroyWindow(hwnd);
	}
}

bool WinApp::ProcessMessage()
{
	MSG msg{};

	// hwndにnullptrを渡すので、このスレッドの全ウィンドウ分がまとめて処理される
	while ( PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE) )
	{
		if ( msg.message == WM_QUIT )
		{
			return true;
		}

		TranslateMessage(&msg);
		DispatchMessageW(&msg);
	}

	return false;
}

void WinApp::Show()
{
	if ( hwnd_ )
	{
		ShowWindow(hwnd_, SW_SHOW);
	}
}

void WinApp::Hide()
{
	if ( hwnd_ )
	{
		ShowWindow(hwnd_, SW_HIDE);
	}
}

bool WinApp::IsVisible() const
{
	return hwnd_ != nullptr && IsWindowVisible(hwnd_) != FALSE;
}

bool WinApp::IsFocused() const
{
	return hwnd_ != nullptr && GetForegroundWindow() == hwnd_;
}

bool WinApp::ConsumeResize(uint32_t& width, uint32_t& height)
{
	if ( !resizePending_ )
	{
		return false;
	}
	resizePending_ = false;
	width = clientWidth_;
	height = clientHeight_;
	return true;
}