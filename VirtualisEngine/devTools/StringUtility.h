#pragma once
#include <string>

/// <summary>
/// 文字コード変換(UTF-8 <-> UTF-16)。インスタンスは作らず、静的関数だけを使う
/// </summary>
class StringUtility
{
public:
	StringUtility() = delete;

	// string(UTF-8) -> wstring
	static std::wstring ConvertString(const std::string& str);
	// wstring -> string(UTF-8)
	static std::string ConvertString(const std::wstring& str);
};