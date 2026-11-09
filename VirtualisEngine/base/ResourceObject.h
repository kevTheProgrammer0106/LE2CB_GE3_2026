#pragma once

#include <wrl.h>
#include <d3d12.h>

#include "DirectXTex.h"
#include "externals/DirectXTex/d3dx12.h"
#include <vector>

#define _USE_MATH_DEFINES
#include <math.h>

#include <fstream>
#include <sstream>

class ResourceObject
{
public:
	Microsoft::WRL::ComPtr<ID3D12Resource> resource;

	// デフォルト構築(中身は空)。std::vector<ResourceObject>や、
	// ResourceObjectをメンバーに持つ構造体のデフォルト構築に必要
	ResourceObject() = default;

	// "ID3D12Resource* から ResourceObject に変換できません" のエラーはここが無いと起きる。
	// 生ポインタからの暗黙変換を許可する(ComPtrの生ポインタ用コンストラクタと同じくAddRefされる)
	ResourceObject(ID3D12Resource* ptr)
	{
		resource.Attach(ptr); // takes ownership of an already-AddRef'd pointer, no extra AddRef
	}

	  // すでにComPtrを持っている場合の変換も許可しておく
	ResourceObject(Microsoft::WRL::ComPtr<ID3D12Resource> ptr) : resource(std::move(ptr))
	{
	}

	ID3D12Resource* Get() const
	{
		return resource.Get();
	}

	void Reset()
	{
		resource.Reset();
	}

	explicit operator bool() const
	{
		return resource != nullptr;
	}
};