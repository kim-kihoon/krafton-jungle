#include "Editor/Importer/ObjStaticMeshImporter.h"
#include "Asset/FileUtils.h"
#include "Asset/StaticMeshTypes.h"
#include "Core/Paths.h"
#include "Math/Utils.h"
#include "Core/Logger.h"
#include "Core/PlatformTime.h"
#include "Core/ResourceManager.h"
#include "Core/StringUtils.h"
#include "Editor/Importer/ObjMtlImporter.h"

#include <algorithm>
#include <cfloat>
#include <chrono>
#include <cctype>
#include <filesystem>
#include <fstream>

namespace
{
	constexpr const char* DefaultUberLitShaderPath = "Shaders/UberLit.hlsl";

	FString ToLowerAscii(FString Value)
	{
		std::transform(Value.begin(), Value.end(), Value.begin(), [](unsigned char Ch)
		{
			return static_cast<char>(std::tolower(Ch));
		});
		return Value;
	}

	bool IsObjSourcePath(const FString& Path)
	{
		return ToLowerAscii(std::filesystem::path(FPaths::ToWide(Path)).extension().string()) == ".obj";
	}

	FString SanitizeAssetFileToken(FString Value)
	{
		if (Value.empty())
		{
			return "DefaultWhite";
		}

		for (char& Ch : Value)
		{
			const unsigned char Byte = static_cast<unsigned char>(Ch);
			if (!std::isalnum(Byte) &&
				Ch != '_' &&
				Ch != '-' &&
				Ch != '.')
			{
				Ch = '_';
			}
		}

		return Value;
	}

	FString ReadObjMtllibPath(const FString& SourcePath)
	{
		namespace fs = std::filesystem;

		const fs::path ObjPath(FPaths::ToAbsolute(FPaths::ToWide(SourcePath)));
		std::ifstream ObjFile(ObjPath);
		if (!ObjFile.is_open())
		{
			return {};
		}

		FString Line;
		while (std::getline(ObjFile, Line))
		{
			Line = StringUtils::Trim(Line);
			if (!StringUtils::StartWith(Line, "mtllib "))
			{
				continue;
			}

			FString MtlToken = StringUtils::Trim(Line.substr(7));
			const size_t SpacePos = MtlToken.find_first_of(" \t");
			if (SpacePos != FString::npos)
			{
				MtlToken = MtlToken.substr(0, SpacePos);
			}

			if (MtlToken.empty())
			{
				return {};
			}

			fs::path MtlPath = fs::path(FPaths::ToWide(MtlToken));
			if (MtlPath.is_relative())
			{
				MtlPath = ObjPath.parent_path() / MtlPath;
			}
			MtlPath = MtlPath.lexically_normal();

			if (!fs::exists(MtlPath))
			{
				return {};
			}

			return FPaths::ToRelativeString(MtlPath.generic_wstring());
		}

		return {};
	}

	uint64 GetFileWriteTimeTicks(const FString& Path)
	{
		namespace fs = std::filesystem;

		fs::path FilePath(FPaths::ToAbsolute(FPaths::ToWide(Path)));
		if (!fs::exists(FilePath))
		{
			return 0;
		}

		auto WriteTime = fs::last_write_time(FilePath);
		auto Duration = WriteTime.time_since_epoch();

		return static_cast<uint64>(
			std::chrono::duration_cast<std::chrono::seconds>(Duration).count());
	}

	uint64 HashFileFNV1a(const FString& Path)
	{
		std::ifstream File(std::filesystem::path(FPaths::ToAbsolute(FPaths::ToWide(Path))), std::ios::binary);
		if (!File.is_open())
		{
			return 0;
		}

		uint64 Hash = 14695981039346656037ull;
		char Buffer[64 * 1024];
		while (File.good())
		{
			File.read(Buffer, sizeof(Buffer));
			const std::streamsize ReadBytes = File.gcount();
			for (std::streamsize Index = 0; Index < ReadBytes; ++Index)
			{
				Hash ^= static_cast<unsigned char>(Buffer[Index]);
				Hash *= 1099511628211ull;
			}
		}

		return Hash;
	}

	FString MakeObjMaterialAssetPath(const FString& SourcePath, const FString& SlotName)
	{
		namespace fs = std::filesystem;

		const fs::path SourceFsPath(FPaths::ToWide(FPaths::Normalize(SourcePath)));
		const std::wstring MaterialFileName =
			SourceFsPath.stem().wstring() + L"_" + FPaths::ToWide(SanitizeAssetFileToken(SlotName)) + L".mat";
		fs::path MaterialPath = SourceFsPath.parent_path() / MaterialFileName;

		return FPaths::ToUtf8(MaterialPath.lexically_normal().generic_wstring());
	}

	bool TryLoadKnownMaterialShader(FResourceManager& ResourceManager, const FString& ShaderName)
	{
		if (ShaderName == "Shaders/UberLit.hlsl" || ShaderName == "Shaders/UberUnlit.hlsl")
		{
			return ResourceManager.LoadShader(ShaderName, "mainVS", "mainPS", static_cast<const D3D_SHADER_MACRO*>(nullptr));
		}

		if (ShaderName == "Shaders/OutlinePostProcess.hlsl")
		{
			return ResourceManager.LoadShader(ShaderName, "VS", "PS", static_cast<const D3D_SHADER_MACRO*>(nullptr));
		}

		return false;
	}

	UShader* GetOrTryLoadMaterialShader(FResourceManager& ResourceManager, const FString& ShaderName)
	{
		if (UShader* Shader = ResourceManager.GetShader(ShaderName))
		{
			return Shader;
		}

		UE_LOG("Shader cache miss for imported material: %s. Attempting lazy load.", ShaderName.c_str());
		if (!TryLoadKnownMaterialShader(ResourceManager, ShaderName))
		{
			return nullptr;
		}

		return ResourceManager.GetShader(ShaderName);
	}

	void BuildFallbackBasis(const FVector& InNormal, FVector& OutTangent, FVector& OutBitangent)
	{
		FVector Normal = InNormal.GetSafeNormal();
		if (Normal.IsNearlyZero())
		{
			Normal = FVector(0.0f, 0.0f, 1.0f);
		}

		Normal.FindBestAxisVectors(OutTangent, OutBitangent);
		OutTangent = OutTangent.GetSafeNormal();
		OutBitangent = OutBitangent.GetSafeNormal();
	}

	void BuildTangentsAndBitangents(FStaticMesh* StaticMesh)
	{
		if (StaticMesh == nullptr || StaticMesh->Vertices.empty())
		{
			return;
		}

		TArray<FVector> AccumulatedTangents(StaticMesh->Vertices.size(), FVector::ZeroVector);
		TArray<FVector> AccumulatedBitangents(StaticMesh->Vertices.size(), FVector::ZeroVector);

		for (size_t TriangleIndex = 0; TriangleIndex + 2 < StaticMesh->Indices.size(); TriangleIndex += 3)
		{
			const uint32 Index0 = StaticMesh->Indices[TriangleIndex + 0];
			const uint32 Index1 = StaticMesh->Indices[TriangleIndex + 1];
			const uint32 Index2 = StaticMesh->Indices[TriangleIndex + 2];

			if (Index0 >= StaticMesh->Vertices.size() ||
				Index1 >= StaticMesh->Vertices.size() ||
				Index2 >= StaticMesh->Vertices.size())
			{
				continue;
			}

			const FNormalVertex& Vertex0 = StaticMesh->Vertices[Index0];
			const FNormalVertex& Vertex1 = StaticMesh->Vertices[Index1];
			const FNormalVertex& Vertex2 = StaticMesh->Vertices[Index2];

			const FVector Edge01 = Vertex1.Position - Vertex0.Position;
			const FVector Edge02 = Vertex2.Position - Vertex0.Position;
			const FVector2 UV01 = Vertex1.UVs - Vertex0.UVs;
			const FVector2 UV02 = Vertex2.UVs - Vertex0.UVs;

			const float Determinant = UV01.X * UV02.Y - UV01.Y * UV02.X;
			if (MathUtil::Abs(Determinant) <= MathUtil::Epsilon)
			{
				continue;
			}

			const float InverseDeterminant = 1.0f / Determinant;
			const FVector TriangleTangent = (Edge01 * UV02.Y - Edge02 * UV01.Y) * InverseDeterminant;
			const FVector TriangleBitangent = (Edge02 * UV01.X - Edge01 * UV02.X) * InverseDeterminant;

			if (TriangleTangent.IsNearlyZero() || TriangleBitangent.IsNearlyZero())
			{
				continue;
			}

			AccumulatedTangents[Index0] += TriangleTangent;
			AccumulatedTangents[Index1] += TriangleTangent;
			AccumulatedTangents[Index2] += TriangleTangent;

			AccumulatedBitangents[Index0] += TriangleBitangent;
			AccumulatedBitangents[Index1] += TriangleBitangent;
			AccumulatedBitangents[Index2] += TriangleBitangent;
		}

		for (size_t VertexIndex = 0; VertexIndex < StaticMesh->Vertices.size(); ++VertexIndex)
		{
			FNormalVertex& Vertex = StaticMesh->Vertices[VertexIndex];
			FVector Normal = Vertex.Normal.GetSafeNormal();
			if (Normal.IsNearlyZero())
			{
				Normal = FVector(0.0f, 0.0f, 1.0f);
			}

			FVector Tangent = AccumulatedTangents[VertexIndex] - Normal * FVector::DotProduct(Normal, AccumulatedTangents[VertexIndex]);
			if (!Tangent.Normalize())
			{
				BuildFallbackBasis(Normal, Tangent, Vertex.Bitangent);
				Vertex.Tangent = Tangent;
				continue;
			}

			FVector Bitangent = AccumulatedBitangents[VertexIndex];
			Bitangent = Bitangent - Normal * FVector::DotProduct(Normal, Bitangent);
			Bitangent = Bitangent - Tangent * FVector::DotProduct(Tangent, Bitangent);

			if (!Bitangent.Normalize())
			{
				Bitangent = FVector::CrossProduct(Normal, Tangent);
				if (!Bitangent.Normalize())
				{
					BuildFallbackBasis(Normal, Tangent, Bitangent);
				}
			}

			Vertex.Tangent = Tangent;
			Vertex.Bitangent = Bitangent;
		}
	}
}

//	v, vt, vn, mtllib, usemtl, f
FStaticMesh* FObjStaticMeshImporter::ImportStaticMesh(const FString& Path, const FStaticMeshLoadOptions& LoadOptions)
{
	FStaticMesh* StaticMesh = new FStaticMesh();
	BuiltMaterialSlotName.clear();

	const double StartTime = FPlatformTime::Seconds();
	UE_LOG("[ObjStaticMeshImporter] Start loading OBJ: %s", Path.c_str());

	FObjRawData RawData;

	/* Obj Parse - Build Raw Data */
	if (!ParseObj(Path, RawData))
	{
		UE_LOG("[ObjStaticMeshImporter] Failed to parse OBJ: %s", Path.c_str());
		delete StaticMesh;
		return nullptr;
	}

	/* Normalize positions before building if requested */
	if (LoadOptions.bNormalizeToUnitCube)
	{
		NormalizeObjRawData(RawData);
	}

	/* Build Cooked Data from Raw Data */
	if (!BuildStaticMesh(Path, StaticMesh, RawData))
	{
		UE_LOG("[ObjStaticMeshImporter] Failed to build static mesh: %s", Path.c_str());
		delete StaticMesh;
		return nullptr;
	}

	UE_LOG("[ObjStaticMeshImporter] OBJ Loaded: %s (Vertices: %zu, Indices: %zu, Sections: %zu, Slots: %zu)",
		Path.c_str(),
		StaticMesh->Vertices.size(),
		StaticMesh->Indices.size(),
		StaticMesh->Sections.size(),
		StaticMesh->Slots.size());

	const double EndTime = FPlatformTime::Seconds();
	UE_LOG("[ObjStaticMeshImporter] Loaded %s in %.3f sec", Path.c_str(), EndTime - StartTime);

	return StaticMesh;
}

bool FObjStaticMeshImporter::ImportIfNeeded(FResourceManager& ResourceManager, const FString& SourcePath, TArray<FString>* OutMaterialPaths)
{
	if (!IsObjSourcePath(SourcePath))
	{
		return false;
	}

	TMap<FString, FString> MaterialAssetPaths;
	EnsureMaterialAssets(ResourceManager, SourcePath, MaterialAssetPaths, OutMaterialPaths);

	const FString AssetPath = ResourceManager.MakeStaticMeshAssetPath(SourcePath);
	if (!IsStaticMeshAssetValid(SourcePath, AssetPath))
	{
		const auto ObjStart = std::chrono::steady_clock::now();
		FStaticMeshLoadOptions LoadOptions;
		LoadOptions.bNormalizeToUnitCube = false;
		FStaticMesh* MeshData = ImportStaticMesh(SourcePath, LoadOptions);
		const auto ObjEnd = std::chrono::steady_clock::now();
		const double ObjLoadSec = std::chrono::duration<double>(ObjEnd - ObjStart).count();

		if (MeshData == nullptr)
		{
			UE_LOG("[ObjStaticMeshImporter] Failed to compile static mesh source: %s", SourcePath.c_str());
			return false;
		}

		MeshData->PathFileName = AssetPath;
		for (FStaticMeshMaterialSlot& Slot : MeshData->Slots)
		{
			auto It = MaterialAssetPaths.find(Slot.SlotName);
			Slot.MaterialAssetPath = (It != MaterialAssetPaths.end()) ? It->second : FString("DefaultWhite");
		}

		const bool bSaveAssetOk = BinarySerializer.SaveStaticMesh(AssetPath, SourcePath, *MeshData);
		UE_LOG("[ObjStaticMeshImporter] Source=%s | Asset=%s | ObjSec=%.6f | AssetSave=%s",
			SourcePath.c_str(),
			AssetPath.c_str(),
			ObjLoadSec,
			bSaveAssetOk ? "OK" : "FAIL");

		delete MeshData;

		if (!bSaveAssetOk)
		{
			return false;
		}
	}

	return ResourceManager.RegisterStaticMeshAsset(AssetPath);
}

bool FObjStaticMeshImporter::EnsureMaterialAssets(FResourceManager& ResourceManager, const FString& SourcePath, TMap<FString, FString>& OutMaterialAssetPaths, TArray<FString>* OutMaterialPaths)
{
	const FString MtlPath = ReadObjMtllibPath(SourcePath);
	if (MtlPath.empty())
	{
		return false;
	}

	TMap<FString, UMaterial*> ParsedMaterials;
	if (!FObjMtlImporter::Load(MtlPath, ParsedMaterials, ResourceManager.GetCachedDevice()))
	{
		UE_LOG("[ObjStaticMeshImporter] Failed to parse MTL for %s: %s", SourcePath.c_str(), MtlPath.c_str());
		return false;
	}

	UShader* Shader = GetOrTryLoadMaterialShader(ResourceManager, DefaultUberLitShaderPath);
	for (auto& [SlotName, Material] : ParsedMaterials)
	{
		if (Material == nullptr)
		{
			continue;
		}

		const FString MaterialPath = MakeObjMaterialAssetPath(SourcePath, SlotName);
		OutMaterialAssetPaths[SlotName] = MaterialPath;

		if (!std::filesystem::exists(std::filesystem::path(FPaths::ToAbsolute(FPaths::ToWide(MaterialPath)))))
		{
			Material->Name = SlotName;
			Material->FilePath = MaterialPath;
			Material->SetShader(Shader);
			if (ResourceManager.SerializeMaterial(MaterialPath, Material))
			{
				UE_LOG("[ObjStaticMeshImporter] Created material asset: %s", MaterialPath.c_str());
			}
		}

		if (OutMaterialPaths &&
			std::find(OutMaterialPaths->begin(), OutMaterialPaths->end(), MaterialPath) == OutMaterialPaths->end())
		{
			OutMaterialPaths->push_back(MaterialPath);
		}
	}

	for (auto& [SlotName, Material] : ParsedMaterials)
	{
		UObjectManager::Get().DestroyObject(Material);
	}

	return !OutMaterialAssetPaths.empty();
}

bool FObjStaticMeshImporter::IsStaticMeshAssetValid(const FString& SourcePath, const FString& AssetPath) const
{
	FStaticMeshBinaryHeader Header;
	if (!BinarySerializer.ReadStaticMeshHeader(AssetPath, Header))
	{
		return false;
	}

	const uint64 SourceWriteTime = GetFileWriteTimeTicks(SourcePath);
	if (SourceWriteTime == 0 || Header.SourceFileWriteTime != SourceWriteTime)
	{
		return false;
	}

	const uint64 SourceHash = HashFileFNV1a(SourcePath);
	if (SourceHash == 0 || Header.SourceFileHash != SourceHash)
	{
		return false;
	}

	return true;
}

bool FObjStaticMeshImporter::ParseObj(const FString& Path, FObjRawData& InRawData)
{
	TArray<FString> Lines;

	if (!FFileUtils::LoadFileToLines(Path, Lines))
	{
		return false;
	}

	FString CurrentMaterialName;

	for (const auto& RawLine : Lines)
	{
		FString Line = StringUtils::Trim(RawLine);

		if (Line.empty() || StringUtils::StartWith(Line, "#"))
		{
			continue;
		}

		if (StringUtils::StartWith(Line, "v "))
		{
			if (!ParsePositionLine(Line, InRawData))
			{
				return false;
			}
		}
		else if (StringUtils::StartWith(Line, "vt "))
		{
			if (!ParseTexCoordLine(Line, InRawData))
			{
				return false;
			}
		}
		else if (StringUtils::StartWith(Line, "vn "))
		{
			if (!ParseNormalLine(Line, InRawData))
			{
				return false;
			}
		}
		else if (StringUtils::StartWith(Line, "mtllib "))
		{
			ParseMtllibLine(Line, InRawData);
		}
		else if (StringUtils::StartWith(Line, "usemtl "))
		{
			ParseUseMtlLine(Line, CurrentMaterialName, InRawData);
		}
		else if (StringUtils::StartWith(Line, "f "))
		{
			if (!ParseFaceLine(Line, CurrentMaterialName, InRawData))
			{
				return false;
			}
		}
	}

	if (InRawData.Normals.empty())
	{
		ComputeNormals(InRawData);
	}

	return !InRawData.Positions.empty() && !InRawData.Faces.empty();
}

bool FObjStaticMeshImporter::BuildStaticMesh(const FString& Path, FStaticMesh* InStaticMesh, FObjRawData& RawData)
{
	// Mesh를 생성할 Raw Data 존재 확인
	if (RawData.Positions.empty() || RawData.Faces.empty())
	{
		return false;
	}

	InStaticMesh->PathFileName = Path;
	InStaticMesh->Vertices.clear();
	InStaticMesh->Indices.clear();
	InStaticMesh->Sections.clear();
	InStaticMesh->Slots.clear();
	BuiltMaterialSlotName.clear();

	// IndexBuffer를 위한 Map
	TMap<FObjVertexKey, uint32> VertexMap;
	TArray<TArray<uint32>> SlotIndices;

	for (const FObjRawFace& Face : RawData.Faces)
	{
		if (Face.Vertices.size() < 3)
		{
			continue;
		}

		const FString MaterialName = Face.MaterialName.empty() ? FString("DefaultWhite") : Face.MaterialName;
		const int32 SlotIdx = GetOrAddMaterialSlot(MaterialName);
		
		if (SlotIdx >= static_cast<int32>(SlotIndices.size()))
		{
			SlotIndices.resize(SlotIdx + 1);
		}

		TArray<uint32>& IndicesPerSlot = SlotIndices[SlotIdx];
		
		for (uint32 i = 0; i < Face.Vertices.size() - 2; i++)
		{
			const uint32 I0 = GetOrCreateVertexIndex(Face.Vertices[0], VertexMap, InStaticMesh, RawData);
			const uint32 I1 = GetOrCreateVertexIndex(Face.Vertices[i + 1], VertexMap, InStaticMesh, RawData);
			const uint32 I2 = GetOrCreateVertexIndex(Face.Vertices[i + 2], VertexMap, InStaticMesh, RawData);

			IndicesPerSlot.push_back(I0);
			IndicesPerSlot.push_back(I1);
			IndicesPerSlot.push_back(I2);
		}
	}

	for (const FString& SlotName : BuiltMaterialSlotName)
	{
		FStaticMeshMaterialSlot NewSlot;
		NewSlot.SlotName = SlotName;
		NewSlot.Material = nullptr;
		InStaticMesh->Slots.push_back(NewSlot);
	}

	for (int32 SlotIdx = 0; SlotIdx < static_cast<int32>(SlotIndices.size()); SlotIdx++)
	{
		TArray<uint32>& IndicesPerSlot = SlotIndices[SlotIdx];
		if (IndicesPerSlot.empty()) continue;

		FStaticMeshSection NewSection;
		NewSection.StartIndex = static_cast<int32>(InStaticMesh->Indices.size());
		NewSection.IndexCount = static_cast<uint32>(IndicesPerSlot.size());
		NewSection.MaterialSlotIndex = SlotIdx;

		InStaticMesh->Indices.insert(
			InStaticMesh->Indices.end(),
			IndicesPerSlot.begin(),
			IndicesPerSlot.end());

		InStaticMesh->Sections.push_back(NewSection);
	}

	BuildTangentsAndBitangents(InStaticMesh);
	InStaticMesh->LocalBounds = BuildLocalBounds(InStaticMesh);

	return !InStaticMesh->Vertices.empty() && !InStaticMesh->Indices.empty();
}

int32 FObjStaticMeshImporter::GetOrAddMaterialSlot(const FString& MaterialName)
{
	FString SlotName = MaterialName.empty() ? FString("DefaultWhite") : MaterialName;
	
	for (int32 i = 0; i < static_cast<int32>(BuiltMaterialSlotName.size()); i++)
	{
		if (BuiltMaterialSlotName[i] == SlotName)
		{
			return i;
		}
	}
	
	//	없다면 생성
	BuiltMaterialSlotName.push_back(SlotName);
	return static_cast<int32>(BuiltMaterialSlotName.size() - 1);
}

FAABB FObjStaticMeshImporter::BuildLocalBounds(FStaticMesh* InStaticMesh) const
{
	FAABB Bounds;
	Bounds.Reset();

	for (const FNormalVertex& Vertex : InStaticMesh->Vertices)
	{
		Bounds.Expand(Vertex.Position);
	}

	return Bounds;
}

#pragma region __HELPER__

//	v
bool FObjStaticMeshImporter::ParsePositionLine(const FString& Line, FObjRawData& InRawData)
{
	TArray<FString> Tokens = StringUtils::Split(Line);

	//	4개를 보장해야 함
	if (Tokens.size() < 4)
	{
		return false;
	}

	FVector Position;
	Position.X = std::stof(Tokens[1]);
	Position.Y = std::stof(Tokens[2]);
	Position.Z = std::stof(Tokens[3]);

	InRawData.Positions.push_back(Position);
	return true;
}

//	vt
bool FObjStaticMeshImporter::ParseTexCoordLine(const FString& Line, FObjRawData& InRawData)
{
	TArray<FString> Tokens = StringUtils::Split(Line);

	//	3개를 보장해야 함
	if (Tokens.size() < 3)
	{
		return false;
	}

	FVector2 TexCoord;
	TexCoord.X = std::stof(Tokens[1]);
	TexCoord.Y = 1.0f - std::stof(Tokens[2]); 

	InRawData.UVs.push_back(TexCoord);
	return true;
}

//	vn
bool FObjStaticMeshImporter::ParseNormalLine(const FString& Line, FObjRawData& InRawData)
{
	TArray<FString> Tokens = StringUtils::Split(Line);

	//	4개를 보장해야 함
	if (Tokens.size() < 4)
	{
		return false;
	}

	FVector Normal;
	Normal.X = std::stof(Tokens[1]);
	Normal.Y = std::stof(Tokens[2]);
	Normal.Z = std::stof(Tokens[3]);

	InRawData.Normals.push_back(Normal);
	return true;
}

//	mtllib
void FObjStaticMeshImporter::ParseMtllibLine(const FString& Line, FObjRawData& InRawData)
{
	TArray<FString> Tokens = StringUtils::Split(Line);

	//	파일명이 존재하는 지 여부만 확인
	if (Tokens.size() >= 2)
	{
		InRawData.ReferencedMtlPath = Tokens[1];
	}
}

//	usemtl
void FObjStaticMeshImporter::ParseUseMtlLine(const FString& Line, FString& CurrentMaterialName, FObjRawData& InRawData)
{
	TArray<FString> Tokens = StringUtils::Split(Line);

	if (Tokens.size() >= 2)
	{
		CurrentMaterialName = Tokens[1];
	}
}


bool FObjStaticMeshImporter::ParseFaceLine(const FString& Line, const FString& CurrentMaterialName, FObjRawData& InRawData)
{
	TArray<FString> Tokens = StringUtils::Split(Line);

	//	surface 정보는 최소한 4개를 보장 (face는 3개가 아닐 수도 있음)
	//	이후에 triangulation 진행해야 함

	if (Tokens.size() < 4)
	{
		return false;
	}

	FObjRawFace Face;
	Face.MaterialName = CurrentMaterialName;

	for (uint32 i = 1; i < Tokens.size(); i++)
	{
		FObjRawIndex Idx;
		if (!ParseFaceVertexToken(Tokens[i], Idx, InRawData))
		{
			return false;
		}

		Face.Vertices.push_back(Idx);
	}

	InRawData.Faces.push_back(Face);
	return true;
}

//	Obj index는 1-based이기에 0-based로 변경
//	NOTE : Obj는 종종 negative index를 사용할 때도 있음 (그러나 지원하지 않는게 편할 듯) - 필요하면 추가할 것
bool FObjStaticMeshImporter::ParseFaceVertexToken(const FString& Token, FObjRawIndex& OutIndex, FObjRawData& InRawData)
{
	TArray<FString> Parts;
	Parts.reserve(3);

	size_t Start = 0;
	while (true)
	{
		size_t SlashPos = Token.find('/', Start);
		if (SlashPos == FString::npos)
		{
			Parts.push_back(Token.substr(Start));
			break;
		}

		Parts.push_back(Token.substr(Start, SlashPos - Start));
		Start = SlashPos + 1;

		if (Start > Token.size())
		{
			Parts.emplace_back();
			break;
		}
	}

	if (Parts.size() >= 1 && !Parts[0].empty())
	{
		OutIndex.PositionIndex = std::stoi(Parts[0]) - 1;
	}
	if (Parts.size() >= 2 && !Parts[1].empty())
	{
		OutIndex.UVIndex = std::stoi(Parts[1]) - 1;
	}
	if (Parts.size() >= 3 && !Parts[2].empty())
	{
		OutIndex.NormalIndex = std::stoi(Parts[2]) - 1;
	}

	return OutIndex.PositionIndex >= 0;
}

// int32 FObjStaticMeshImporter::GetOrAddMaterialSlot(const FString& MaterialName)
// {
// 	FString SlotName = MaterialName;
// 	if (SlotName.empty())
// 	{
// 		SlotName = "Default";
// 	}
//
// 	//	이미 존재하는 MaterialSlot인지 확인
// 	for (int32 i = 0; i < static_cast<int32>(StaticMeshAsset.MaterialSlots.size()); i++)
// 	{
// 		if (StaticMeshAsset.MaterialSlots[i].SlotName == SlotName)
// 		{
// 			return i;
// 		}
// 	}
//
// 	FStaticMeshMaterialSlot NewSlot = {};
// 	NewSlot.SlotName = SlotName;
//
// 	StaticMeshAsset.MaterialSlots.push_back(NewSlot);
// 	return static_cast<int32>(StaticMeshAsset.MaterialSlots.size() - 1);
// }

//	Raw Index -> 최종 Vertex 생성
FNormalVertex FObjStaticMeshImporter::MakeVertex(const FObjRawIndex& RawIndex, FObjRawData& RawData) const
{
	FNormalVertex Vertex = {};

	if (RawIndex.PositionIndex >= 0 && RawIndex.PositionIndex < static_cast<int32>(RawData.Positions.size()))
	{
		Vertex.Position = RawData.Positions[RawIndex.PositionIndex];
	}
	if (RawIndex.NormalIndex >= 0 && RawIndex.NormalIndex < static_cast<int32>(RawData.Normals.size()))
	{
		Vertex.Normal = RawData.Normals[RawIndex.NormalIndex];
	}
	else
	{
		Vertex.Normal = FVector(0.0f, 0.0f, 1.0f);
	}

	//	White로 초기화
	Vertex.Color = FColor{ 1.f, 1.f, 1.f, 1.f };

	if (RawIndex.UVIndex >= 0 && RawIndex.UVIndex < static_cast<int32>(RawData.UVs.size()))
	{
		Vertex.UVs = RawData.UVs[RawIndex.UVIndex];
	}
	else
	{
		Vertex.UVs = FVector2{ 0.0f, 0.0f };
	}

	return Vertex;
}

uint32 FObjStaticMeshImporter::GetOrCreateVertexIndex(const FObjRawIndex& RawIndex, TMap<FObjVertexKey, uint32>& VertexMap, FStaticMesh* StaticMesh, FObjRawData& RawData)
{
	FObjVertexKey Key = {};
	Key.ObjRawIndex.PositionIndex = RawIndex.PositionIndex;
	Key.ObjRawIndex.NormalIndex = RawIndex.NormalIndex;
	Key.ObjRawIndex.UVIndex = RawIndex.UVIndex;

	auto It = VertexMap.find(Key);
	if (It != VertexMap.end())
	{
		return It->second;
	}

	FNormalVertex NewVertex = MakeVertex(RawIndex, RawData);
	uint32 NewIndex = static_cast<uint32>(StaticMesh->Vertices.size());

	StaticMesh->Vertices.push_back(NewVertex);
	VertexMap.emplace(Key, NewIndex);

	return NewIndex;
}

// Static Mesh Raw Data를 단위 큐브 크기로 정규화합니다. 
// - 정규화하지 않은 메시 파일과 서로 다른 binary cache를 갖습니다.
// - 개별 StaticMesh 컴포넌트에서 Import할 때 Normalize On Import 설정을 켜고 꺼서 조절할 수 있습니다.
void FObjStaticMeshImporter::NormalizeObjRawData(FObjRawData& RawData)
{
	if (RawData.Positions.empty())
	{
		return;
	}

	FVector Min(FLT_MAX, FLT_MAX, FLT_MAX);
	FVector Max(-FLT_MAX, -FLT_MAX, -FLT_MAX);

	for (const FVector& Position : RawData.Positions)
	{
		Min.X = std::min(Min.X, Position.X);
		Min.Y = std::min(Min.Y, Position.Y);
		Min.Z = std::min(Min.Z, Position.Z);

		Max.X = std::max(Max.X, Position.X);
		Max.Y = std::max(Max.Y, Position.Y);
		Max.Z = std::max(Max.Z, Position.Z);
	}

	const FVector Center = (Min + Max) * 0.5f;
	const FVector Size = Max - Min;
	const float MaxDim = std::max(Size.X, std::max(Size.Y, Size.Z));

	if (MaxDim <= MathUtil::Epsilon)
	{
		return;
	}

	const float Scale = 1.0f / MaxDim;

	for (FVector& Position : RawData.Positions)
	{
		Position = (Position - Center) * Scale;
	}
}

void FObjStaticMeshImporter::NormalizeRawSizeToUnitCube(FObjRawData& RawData)
{
	if (RawData.Positions.empty())
	{
		return;
	}

	FVector Min(FLT_MAX, FLT_MAX, FLT_MAX);
	FVector Max(-FLT_MAX, -FLT_MAX, -FLT_MAX);

	for (const FVector& Position : RawData.Positions)
	{
		Min.X = std::min(Min.X, Position.X);
		Min.Y = std::min(Min.Y, Position.Y);
		Min.Z = std::min(Min.Z, Position.Z);

		Max.X = std::max(Max.X, Position.X);
		Max.Y = std::max(Max.Y, Position.Y);
		Max.Z = std::max(Max.Z, Position.Z);
	}

	const FVector Center = (Min + Max) * 0.5f;
	
	for (FVector& Position : RawData.Positions)
	{
		Position = (Position - Center);
	}
}

// vn(normal 벡터 정보)이 없는 .obj 파일을 불러올 때 각 정점의 normal 값을 복원합니다.
// 각 삼각형의 벡터를 외적하여 법선벡터를 계산합니다. (큰 삼각형일수록 큰 가중치를 갖습니다.)
void FObjStaticMeshImporter::ComputeNormals(FObjRawData& RawData)
{
	const int32 PositionCount = static_cast<int32>(RawData.Positions.size());

	TArray<FVector> Accumulated(PositionCount, FVector(0.0f, 0.0f, 0.0f));

	// 각 Face를 삼각형으로 분해하며 면 법선을 누적
	for (FObjRawFace& Face : RawData.Faces)
	{
		if (Face.Vertices.size() < 3) continue;

		// 폴리곤을 fan triangulation 방식으로 분할하여 처리
		for (int32 i = 0; i < Face.Vertices.size() - 2; ++i)
		{
			int32 I0 = Face.Vertices[0].PositionIndex;
			int32 I1 = Face.Vertices[i + 1].PositionIndex;
			int32 I2 = Face.Vertices[i + 2].PositionIndex;

			if (I0 < 0 || I1 < 0 || I2 < 0) continue;

			const FVector& P0 = RawData.Positions[I0];
			const FVector& P1 = RawData.Positions[I1];
			const FVector& P2 = RawData.Positions[I2];

			FVector E01 = P1 - P0;
			FVector E02 = P2 - P0;

			// 정규화를 생략하여 면적 가중치 적용
			FVector FaceNormal = E01.CrossProduct(E02);

			Accumulated[I0] += FaceNormal;
			Accumulated[I1] += FaceNormal;
			Accumulated[I2] += FaceNormal;
		}
	}

	// 정규화한 뒤 RawData.Normals에 저장한다. (1:1로 Position과 대응된다.
	RawData.Normals.resize(PositionCount);
	for (int32 i = 0; i < PositionCount; ++i)
	{
		FVector Normal = Accumulated[i];
		float Length = Normal.Size();
		RawData.Normals[i] = (Length > 1e-6f) ? Normal / Length : FVector(0.0f, 0.0f, 1.0f);
	}

	// 각 face의 normal index를 position index와 동일하게 연결한다.
	for (FObjRawFace& Face : RawData.Faces)
	{
		for (FObjRawIndex& Idx : Face.Vertices)
		{
			Idx.NormalIndex = Idx.PositionIndex;
		}
	}
}

#pragma endregion
