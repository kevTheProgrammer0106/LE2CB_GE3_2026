#include "WinApp.h"

#include <string>
#include <format>
#ifdef USE_IMGUI
#include "externals/imgui/imgui.h"
#include "externals/imgui/imgui_impl_dx12.h"
#include "externals/imgui/imgui_impl_win32.h"

#pragma comment(lib, "winmm.lib") // timeGetTime / timeBeginPeriod / timeEndPeriod

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
#endif 

// ウィンドウプロシージャ
LRESULT CALLBACK WinApp::WindowProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
#ifdef USE_IMGUI
	if ( ImGui_ImplWin32_WndProcHandler(hwnd, msg, wparam, lparam) )
	{
		return TRUE;
	}
#endif

	switch ( msg )
	{
		case WM_DESTROY:
			PostQuitMessage(0);
			break;
	}

	return DefWindowProcW(hwnd, msg, wparam, lparam);
}


void WinApp::Initialize()
{
	//システムタイマーの分解能を上げる
	timeBeginPeriod(1);

	HRESULT hr = CoInitializeEx(0, COINIT_MULTITHREADED); //マルチスレッドでCOMを利用するための初期化
	wc_.lpfnWndProc = WindowProc;
	wc_.lpszClassName = L"CG2WindowClass";
	wc_.hInstance = GetModuleHandle(nullptr);
	wc_.hCursor = LoadCursor(nullptr, IDC_ARROW);
	RegisterClass(&wc_);
	RECT wrc = { 0, 0, kClientWidth, kClientHeight };
	AdjustWindowRect(&wrc, WS_OVERLAPPEDWINDOW, false);

	hwnd_ = CreateWindow(
		wc_.lpszClassName, //クラス名
		L"LE2C_29_メンドーザ_ケビン_ラルフ_パレルモ", //タイトルバーの文字
		WS_OVERLAPPEDWINDOW, //ウィンドウスタイル
		CW_USEDEFAULT, //初期X座標
		CW_USEDEFAULT, //初期Y座標
		wrc.right - wrc.left, //ウィンドウ幅
		wrc.bottom - wrc.top, //ウィンドウ高
		nullptr, //親ウィンドウハンドル
		nullptr, //メニューハンドル
		GetHInstance(), //インスタンスハンドル
		nullptr //追加パラメータ
	);
	assert(hwnd_ != nullptr);


//
//#ifdef _DEBUG
//
//	Microsoft::WRL::ComPtr<ID3D12Debug1> debugController = nullptr;
//	if ( SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debugController))) )
//	{
//		//デバッグレイヤーを有効化する
//		debugController->EnableDebugLayer();
//		// さらにGPU側でもチェックを行うようにする
//		debugController->SetEnableGPUBasedValidation(TRUE);
//	}
//
//#endif


	//ウィンドウの表示
	ShowWindow(hwnd_, SW_SHOW);
}

void WinApp::Update()
{

}

void WinApp::Finalize()
{
	// main.cppのコメント
	//ImGuiの終了処理。詳細はさひて重要ではないので解決は省略する。
	//こういもんである。初期化と逆順に行う
	CloseWindow(hwnd_);
	//COMの終了処理
	CoUninitialize();
}
bool WinApp::ProcessMessage()
{
	MSG msg{};

	if ( PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE) )
	{
		TranslateMessage(&msg);
		DispatchMessage(&msg);
	}

	if ( msg.message == WM_QUIT )
	{
		return true;
	}
	return false;
}