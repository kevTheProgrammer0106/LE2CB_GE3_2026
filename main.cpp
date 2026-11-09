#include <Windows.h>
#include <cassert>

#include "Application.h"
#include "CrashHandler.h"
#include "D3DResourceLeakChecker.h"

//===================================================================
// エントリーポイント
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
		// Applicationの中身はD3Dオブジェクトを持つので、leakCheckより先に破棄されるようブロックで囲む
		Application app;
		app.Initialize(hInstance);
		app.Run();
	}

	CoUninitialize();
	timeEndPeriod(1);
	return 0;
}