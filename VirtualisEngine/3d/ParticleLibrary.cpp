#include "ParticleLibrary.h"

#include <fstream>
#include <sstream>
#include <system_error>

namespace fs = std::filesystem;

namespace
{
	constexpr float kPollInterval = 0.5f; // ファイル監視の間隔(秒)

	// ファイル全体を読む。開けなければ false
	bool ReadAll(const fs::path& path, std::string& out)
	{
		std::ifstream file(path, std::ios::binary);
		if ( !file )
		{
			return false;
		}
		std::ostringstream buffer;
		buffer << file.rdbuf();
		out = buffer.str();
		return true;
	}
}

fs::path ParticleLibrary::GetFilePath(const Entry& entry) const
{
	return fs::path(kDirectory) / ( entry.name + ".particle" );
}

ParticleLibrary::Entry* ParticleLibrary::Register(const std::string& name, const ParticleConfig& defaultConfig)
{
	if ( Entry* existing = Find(name) )
	{
		return existing;
	}

	auto entry = std::make_unique<Entry>();
	entry->name = name;
	entry->config = defaultConfig;
	entry->config.Sanitize();

	// 保存済みファイルがあれば、コードのデフォルトよりそちらを優先する
	const fs::path path = GetFilePath(*entry);
	std::string text;
	if ( ReadAll(path, text) )
	{
		ParticleConfigSerializer::FromText(text, entry->config);
	}
	std::error_code ec;
	const fs::file_time_type time = fs::last_write_time(path, ec);
	if ( !ec )
	{
		entry->fileTime = time;
		entry->hasFileTime = true;
	}

	entries_.push_back(std::move(entry));
	return entries_.back().get();
}

ParticleLibrary::Entry* ParticleLibrary::GetAt(size_t index)
{
	return index < entries_.size() ? entries_[index].get() : nullptr;
}

ParticleLibrary::Entry* ParticleLibrary::Find(const std::string& name)
{
	for ( auto& entry : entries_ )
	{
		if ( entry->name == name )
		{
			return entry.get();
		}
	}
	return nullptr;
}

int ParticleLibrary::IndexOf(const std::string& name) const
{
	for ( size_t i = 0; i < entries_.size(); ++i )
	{
		if ( entries_[i]->name == name )
		{
			return static_cast< int >(i);
		}
	}
	return -1;
}

void ParticleLibrary::Apply(Entry& entry, const ParticleConfig& config)
{
	entry.config = config;
	entry.config.Sanitize();
	++entry.revision; // エミッタはこの番号の変化を見て新しい設定を取り込む
}

bool ParticleLibrary::Save(Entry& entry)
{
	const fs::path path = GetFilePath(entry);

	std::error_code ec;
	fs::create_directories(path.parent_path(), ec);

	{
		std::ofstream file(path, std::ios::binary | std::ios::trunc);
		if ( !file )
		{
			return false;
		}
		file << ParticleConfigSerializer::ToText(entry.config);
		if ( !file )
		{
			return false;
		}
	}

	// 自分で書いた更新を、外部からの更新と勘違いしないよう時刻を覚えておく
	const fs::file_time_type time = fs::last_write_time(path, ec);
	if ( !ec )
	{
		entry.fileTime = time;
		entry.hasFileTime = true;
	}
	return true;
}

bool ParticleLibrary::Load(Entry& entry)
{
	const fs::path path = GetFilePath(entry);

	std::string text;
	if ( !ReadAll(path, text) )
	{
		return false;
	}

	ParticleConfig loaded = entry.config; // 書かれていないキーは今の値のまま
	if ( !ParticleConfigSerializer::FromText(text, loaded) )
	{
		return false;
	}
	Apply(entry, loaded);

	std::error_code ec;
	const fs::file_time_type time = fs::last_write_time(path, ec);
	if ( !ec )
	{
		entry.fileTime = time;
		entry.hasFileTime = true;
	}
	return true;
}

void ParticleLibrary::Update(float deltaTime)
{
	pollTimer_ += deltaTime;
	if ( pollTimer_ < kPollInterval )
	{
		return;
	}
	pollTimer_ = 0.0f;

	for ( auto& entry : entries_ )
	{
		std::error_code ec;
		const fs::file_time_type time = fs::last_write_time(GetFilePath(*entry), ec);
		if ( ec )
		{
			continue; // ファイルが無い
		}
		if ( entry->hasFileTime && time == entry->fileTime )
		{
			continue; // 変わっていない
		}

		// 書き込み途中で読めなかった場合も時刻は更新する。完成すると時刻がまた変わるので、そのとき読み直せる
		const bool loaded = Load(*entry);
		if ( !loaded )
		{
			entry->fileTime = time;
			entry->hasFileTime = true;
		}
	}
}

bool ParticleLibrary::ConsumeEditRequest(int& outIndex)
{
	if ( editRequest_ < 0 )
	{
		return false;
	}
	outIndex = editRequest_;
	editRequest_ = -1;
	return true;
}