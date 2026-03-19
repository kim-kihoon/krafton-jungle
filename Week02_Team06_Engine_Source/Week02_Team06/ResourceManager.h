#pragma once
#include "Mesh.h"

class UResourceManager
{
public:
	UResourceManager() = default;
	~UResourceManager() = default;

public:
	void Initialize(ID3D11Device& Device);
	void Release();
	void LoadResourceData(ID3D11Device& Device, const FString& MeshName, const FString& FilePath, const uint32* Indices, UINT IndexCount);

public:
	UMesh* FindMeshData(const FString& DataTypaName);

private:
	TMap<FString, UMesh*> MeshDatas;
};
