#pragma once
#include <Windows.h>

/// <summary>
/// 未処理例外が起きたときにミニダンプ(./Dumps/CrashDump_*.dmp)を書き出す
/// </summary>
class CrashHandler
{
public:
	CrashHandler() = delete;

	/// WinMainの先頭で1回だけ呼ぶ
	static void Install();

private:
	static LONG WINAPI ExportDump(EXCEPTION_POINTERS* exception);
};