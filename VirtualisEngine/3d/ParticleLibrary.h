#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "ParticleConfig.h"

/// <summary>
/// パーティクル設定の共有置き場(ホットリロードの中継点)。
///
///   GameScene のエミッタ ── Register() ──▶ ここ ◀── Apply() ── ParticleEditor
///
/// エミッタは毎フレーム entry->revision を見て、変わっていたら新しい設定を取り込む。
/// つまりエディタの「Apply」を押すと、次に発生するパーティクルから新しい値になる。
/// resources/particles/名前.particle の更新も監視していて、メモ帳などで保存しても同じ経路で反映される。
///
/// GPUにもウィンドウにも依存しない。アプリで1つだけ持ち、両方のシーンに参照を渡す
/// </summary>
class ParticleLibrary
{
public:
	struct Entry
	{
		std::string name;       // エミッタの名前(ファイル名にもなる)
		ParticleConfig config;  // 今ゲームに適用されている設定
		uint32_t revision = 1;  // 設定が変わるたびに増える(エミッタが変化を検知する)

		// ファイルの最終更新時刻(自分で書いた保存を、外部更新と勘違いして読み直さないため)
		std::filesystem::file_time_type fileTime{};
		bool hasFileTime = false;
	};

	/// 保存先フォルダ(実行ファイルの作業ディレクトリからの相対パス)
	static constexpr const char* kDirectory = "resources/particles";

	/// 名前で登録する。同じ名前が既にあればそのEntryを返す(設定は上書きしない)。
	/// ファイルがあればそちらの値を優先する。無ければ defaultConfig を使う
	Entry* Register(const std::string& name, const ParticleConfig& defaultConfig);

	size_t GetCount() const
	{
		return entries_.size();
	}
	Entry* GetAt(size_t index);
	Entry* Find(const std::string& name);
	/// 見つからなければ -1
	int IndexOf(const std::string& name) const;

	/// 設定を差し替えて revision を進める(= ホットリロード)。Sanitize済みの値が入る
	void Apply(Entry& entry, const ParticleConfig& config);

	/// ファイルへ書き出す。成功したら true
	bool Save(Entry& entry);
	/// ファイルから読み直して Apply する。ファイルが無い・読めないときは false で、設定は変えない
	bool Load(Entry& entry);

	/// ファイルの更新を監視する(0.5秒ごと)。毎フレーム呼んでよい
	void Update(float deltaTime);

	/// ゲーム側の「エディタで開く」ボタンから呼ぶ。Application が拾ってエディタを表示する
	void RequestEdit(int index)
	{
		editRequest_ = index;
	}
	bool ConsumeEditRequest(int& outIndex);

	std::filesystem::path GetFilePath(const Entry& entry) const;

private:
	// Entry* を配るので、vectorが伸びても動かないようunique_ptrで持つ
	std::vector<std::unique_ptr<Entry>> entries_;
	float pollTimer_ = 0.0f;
	int editRequest_ = -1;
};