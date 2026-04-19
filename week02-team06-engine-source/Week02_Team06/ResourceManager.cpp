#include "pch.h"
#include "ResourceManager.h"
#include "json.hpp"
#include <fstream>

using json = nlohmann::json;

void UResourceManager::Initialize(ID3D11Device& Device)
{
	LoadResourceData(Device, "Sphere", "Content/Meshes/Sphere.json", nullptr, 0);
	LoadResourceData(Device, "Cube", "Content/Meshes/Cube.json", nullptr, 0);
	LoadResourceData(Device, "Triangle", "Content/Meshes/Triangle.json", nullptr, 0);
	LoadResourceData(Device, "GizmoLocation", "Content/Meshes/GizmoLocation.json", nullptr, 0);
	LoadResourceData(Device, "GizmoRotation", "Content/Meshes/GizmoRotation.json", nullptr, 0);
	LoadResourceData(Device, "GizmoScale", "Content/Meshes/GizmoScale.json", nullptr, 0);
}

void UResourceManager::LoadResourceData(ID3D11Device& Device, const FString& MeshName, const FString& FilePath, const uint32* Indices, UINT IndexCount)
{
	if (MeshDatas.Find(MeshName) != nullptr)
		return;

	std::ifstream File(FilePath);
	if (!File.is_open())
		return;

	json Root = json::parse(File);
	const auto& VerticesJson = Root["vertices"];
	const UINT VertexCount = static_cast<UINT>(VerticesJson.size());
	TArray<FVertexSimple> Vertices;
	Vertices.SetNum(VertexCount);

	for (UINT i = 0; i < VertexCount; ++i)
	{
		const auto& V = VerticesJson[i];
		Vertices[i] = { V[0].get<float>(), V[1].get<float>(), V[2].get<float>(),
		                V[3].get<float>(), V[4].get<float>(), V[5].get<float>(), V[6].get<float>() };
	}

	UMesh* Mesh = new UMesh;
	Mesh->Load(Device, &Vertices[0], VertexCount, Indices, IndexCount);

	MeshDatas.Insert({ MeshName, Mesh });
}

void UResourceManager::Release()
{
	for (auto& iter : MeshDatas)
	{
		if (iter.second)
		{
			iter.second->Release();
			delete iter.second;
			iter.second = nullptr;
		}
	}
	MeshDatas.Clear();
}

UMesh* UResourceManager::FindMeshData(const FString& DataTypaName)
{
	if (nullptr == MeshDatas.Find(DataTypaName))
	{
		return nullptr;
	}

	return MeshDatas[DataTypaName];
}
