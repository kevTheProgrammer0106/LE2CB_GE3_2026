#pragma once

/// <summary>
/// リソースリークチェック。WinMainの最初に宣言し、最後に破棄されるようにする
/// (破棄時にDXGIが残っているオブジェクトをデバッグ出力へ報告する)
/// </summary>
class D3DResourceLeakChecker
{
public:
	~D3DResourceLeakChecker();
};
