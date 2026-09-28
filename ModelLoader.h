#pragma once
#include <string>
#include <vector>
#include <unordered_map>

#include "WorldTransform.h" // Vector2/3/4, VertexData, MaterialData, ModelData

struct SubMeshData
{
	std::vector<VertexData> vertices;
	MaterialData material;
};

struct MultiMeshModelData
{
	std::vector<SubMeshData> subMeshes;
};

MaterialData LoadMaterialTemplateFile(const std::string& directoryPath, const std::string& filename);
std::unordered_map<std::string, MaterialData> LoadMaterialTemplateFileMulti(const std::string& directoryPath, const std::string& filename);
ModelData LoadObjFile(const std::string& directoryPath, const std::string& filename);
MultiMeshModelData LoadObjFileMulti(const std::string& directoryPath, const std::string& filename);

