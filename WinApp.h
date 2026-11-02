#pragma once
#include <Windows.h>
#include"WorldTransform.h"
#include <cstdint>



class WinApp
{
public: // 静的メンバー関数
	// ウィンドウプロシージャ
	static LRESULT CALLBACK WindowProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);
		// ウィンドウハンドルを取得
	HWND GetHwnd() const
	{
		return hwnd_;
	}

	// ウィンドウクラスの設定を取得
	const WNDCLASSW& GetWindowClass() const
	{
		return wc_;
	}
		// getter
	HINSTANCE GetHInstance() const
	{
		return wc_.hInstance;
	}

	void Initialize();
	void Update();
	//終了
	void Finalize();

	// メッセージ処理
	bool ProcessMessage();

	static const int32_t kClientWidth = 1280;
	static const int32_t kClientHeight = 720;
private:
	// ウィンドウハンドル
	HWND hwnd_ = nullptr;
	// ウィンドウの設定
	WNDCLASS wc_{};
};

