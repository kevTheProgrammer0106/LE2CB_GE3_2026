#pragma once
#include <string>

/// <summary>
/// ログ出力(Visual Studioの出力ウィンドウへ)。インスタンスは作らず、静的関数だけを使う
/// </summary>
class Logger
{
public:
	Logger() = delete;

	static void Log(const std::string& message);
};