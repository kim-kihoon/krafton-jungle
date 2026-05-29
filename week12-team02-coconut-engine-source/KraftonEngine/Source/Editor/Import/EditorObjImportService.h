#pragma once

#include "Core/CoreTypes.h"
#include "Math/Vector.h"
#include "Mesh/MeshImportOptions.h"
#include "Mesh/MeshManager.h"

struct FStaticMesh;
struct FStaticMeshSection;
struct FStaticMaterial;
class UStaticMesh;
struct ID3D11Device;

// Raw Data — OBJ 파싱 직후 상태
struct FObjInfo
{
	TArray<FVector>  Positions;       // v
	TArray<FVector2> UVs;             // vt
	TArray<FVector>  Normals;         // vn
	TArray<uint32>   PosIndices;      // f - position index
	TArray<uint32>   UVIndices;       // f - uv index
	TArray<uint32>   NormalIndices;   // f - normal index

	FString ObjectName; // object name (optional)

	FString MaterialLibraryFilePath;
	TArray<FStaticMeshSection> Sections;
};

// MTL 재질 정보
struct FObjMaterialInfo
{
	FString MaterialSlotName = "None"; // newmtl
	FVector Kd; // diffuse color
	FString map_Kd; // diffuse texture file path
	FString map_Bump; // normal/bump texture file path

	FVector Ka; // ambient color
	FVector Ks; // specular color
	float Ns; // specular exponent
	float Ni; // optical density
	int32 illum; // illumination model
};


// Editor-only OBJ import path. Runtime code should load cooked .uasset files through FMeshManager.
struct FEditorObjImportService
{
	static bool ImportStaticMeshFromObj(const FString& ObjFilePath, ID3D11Device* Device, UStaticMesh*& OutStaticMesh, bool bRefreshAssetLists = true);
	static bool ImportStaticMeshFromObj(const FString& ObjFilePath, const FImportOptions& Options, ID3D11Device* Device, UStaticMesh*& OutStaticMesh, bool bRefreshAssetLists = true);

	static FString GetStaticMeshPackagePathForObj(const FString& ObjFilePath);

	static void ScanObjSourceFiles();
	static const TArray<FMeshAssetListItem>& GetAvailableObjFiles() { return AvailableObjFiles; }

private:
	static bool ParseObj(const FString& ObjFilePath, FObjInfo& OutObjInfo);
	static bool ParseMtl(const FString& MtlFilePath, TArray<FObjMaterialInfo>& OutMaterials);
	static bool Convert(const FString& ObjFilePath, const FObjInfo& ObjInfo, const TArray<FObjMaterialInfo>& MtlInfos, const FImportOptions& Options, FStaticMesh& OutMesh, TArray<FStaticMaterial>& OutMaterials);

	static FString ConvertMtlInfoToJson(const FString& ObjFilePath, const FObjMaterialInfo* MtlInfo);
	static FString ConvertMtlInfoToMat(const FString& ObjFilePath, const FObjMaterialInfo* MtlInfo);
	static FVector RemapPosition(const FVector& ObjPos, EForwardAxis Axis);

private:
	static TArray<FMeshAssetListItem> AvailableObjFiles;
};
