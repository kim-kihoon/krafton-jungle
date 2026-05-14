#include "Core/ResourceManager.h"

#include "Core/Paths.h"
#include "Core/PlatformTime.h"
#include "SimpleJSON/json.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstring>
#include <cstdio>
#include <cstdint>
#include <cwctype>
#include <filesystem>
#include <fstream>
#include <ranges>
#include <string>
#include <unordered_set>
#include <vector>

#include "Asset/FileUtils.h"

#include "DDSTextureLoader.h"
#include "WICTextureLoader.h"
#include "Core/Logger.h"
#include "Core/StringUtils.h"
#include "Settings/EngineSettings.h"
#include "Asset/BinarySerializer.h"
#include "Asset/Skeleton.h"
#include "Asset/StaticMeshTypes.h"
#include "Asset\SkeletalMesh.h"
#include "Asset/StaticMeshSimplifier.h"
#include "Render/Scene/RenderCommand.h"
#if WITH_EDITOR
#include "Editor/Importer/Fbx/FbxAssetImporter.h"
#include "Editor/Importer/ObjStaticMeshImporter.h"
#endif
namespace
{
	constexpr const char* DefaultUberLitShaderPath = "Shaders/UberLit.hlsl";

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

		UE_LOG("Shader cache miss for material: %s. Attempting lazy load.", ShaderName.c_str());
		if (!TryLoadKnownMaterialShader(ResourceManager, ShaderName))
		{
			return nullptr;
		}

		return ResourceManager.GetShader(ShaderName);
	}

	std::wstring ToLowerPathToken(std::wstring Value)
	{
		std::transform(Value.begin(), Value.end(), Value.begin(), [](wchar_t Ch)
		{
			return static_cast<wchar_t>(std::towlower(Ch));
		});
		return Value;
	}

	FString ToLowerAscii(FString Value)
	{
		std::transform(Value.begin(), Value.end(), Value.begin(), [](unsigned char Ch)
		{
			return static_cast<char>(std::tolower(Ch));
		});
		return Value;
	}

	bool IsStaticMeshAssetPath(const FString& Path)
	{
		return ToLowerAscii(std::filesystem::path(FPaths::ToWide(Path)).extension().string()) == ".asset";
	}

	bool RegisterCompiledAssetPath(FResourceManager& ResourceManager, const FString& AssetPath)
	{
		FBinarySerializer Serializer;
		FStaticMeshBinaryHeader Header;
		if (!Serializer.ReadAssetHeader(AssetPath, Header))
		{
			return false;
		}

		switch (Header.AssetType)
		{
		case ECompiledAssetType::StaticMesh:
			return ResourceManager.RegisterStaticMeshAsset(AssetPath);
		case ECompiledAssetType::SkeletalMesh:
			return ResourceManager.RegisterSkeletalMeshAsset(AssetPath);
		case ECompiledAssetType::Skeleton:
			return ResourceManager.RegisterSkeletonAsset(AssetPath);
		default:
			return false;
		}
	}

	bool IsManagedCompiledAssetType(ECompiledAssetType AssetType)
	{
		return AssetType == ECompiledAssetType::StaticMesh ||
			AssetType == ECompiledAssetType::SkeletalMesh ||
			AssetType == ECompiledAssetType::Skeleton;
	}

	bool IsPathUnder(const std::filesystem::path& Path, const std::filesystem::path& RootPath)
	{
		const std::filesystem::path RelativePath = Path.lexically_relative(RootPath);
		return !RelativePath.empty() && *RelativePath.begin() != std::filesystem::path(L"..");
	}

	TArray<FShaderMacro> NormalizeShaderMacros(TArray<FShaderMacro> Macros)
	{
		std::sort(Macros.begin(), Macros.end(), [](const FShaderMacro& Left, const FShaderMacro& Right)
		{
			if (Left.Name != Right.Name)
			{
				return Left.Name < Right.Name;
			}

			return Left.Value < Right.Value;
		});

		Macros.erase(std::unique(Macros.begin(), Macros.end()), Macros.end());
		return Macros;
	}

	TArray<FShaderMacro> BuildShaderMacros(const D3D_SHADER_MACRO* Defines)
	{
		TArray<FShaderMacro> Macros;
		if (Defines == nullptr)
		{
			return Macros;
		}

		for (const D3D_SHADER_MACRO* Define = Defines; Define->Name != nullptr; ++Define)
		{
			FShaderMacro Macro;
			Macro.Name = Define->Name;
			Macro.Value = Define->Definition ? Define->Definition : "";
			Macros.push_back(Macro);
		}

		return NormalizeShaderMacros(std::move(Macros));
	}

	FString BuildShaderMacroLogString(const TArray<FShaderMacro>& Macros)
	{
		if (Macros.empty())
		{
			return "none";
		}

		FString Result;
		for (size_t Index = 0; Index < Macros.size(); ++Index)
		{
			if (Index > 0)
			{
				Result += ", ";
			}

			Result += Macros[Index].Name + "=" + Macros[Index].Value;;
		}

		return Result;
	}

	FShaderCompileKey NormalizeShaderCompileKey(FShaderCompileKey CompileKey)
	{
		CompileKey.Macros = NormalizeShaderMacros(std::move(CompileKey.Macros));
		return CompileKey;
	}

	FShaderCompileKey BuildShaderCompileKey(const FString& FilePath,
										   const FString& VSEntryPoint,
										   const FString& PSEntryPoint,
										   const D3D_SHADER_MACRO* Defines)
	{
		FShaderCompileKey CompileKey;
		CompileKey.FilePath = FilePath;
		CompileKey.VSEntryPoint = VSEntryPoint;
		CompileKey.PSEntryPoint = PSEntryPoint;
		CompileKey.Macros = BuildShaderMacros(Defines);
		return CompileKey;
	}

	struct FCompiledShaderMacroData
	{
		std::vector<std::string> Names;
		std::vector<std::string> Values;
		std::vector<D3D_SHADER_MACRO> Macros;
	};

	FCompiledShaderMacroData BuildCompiledShaderMacroData(const TArray<FShaderMacro>& Macros)
	{
		FCompiledShaderMacroData MacroData;
		MacroData.Names.reserve(Macros.size());
		MacroData.Values.reserve(Macros.size());
		for (const FShaderMacro& Macro : Macros)
		{
			MacroData.Names.emplace_back(Macro.Name);
			MacroData.Values.emplace_back(Macro.Value);
		}

		MacroData.Macros.reserve(Macros.size() + 1);
		for (size_t Index = 0; Index < Macros.size(); ++Index)
		{
			MacroData.Macros.push_back(
			{
				MacroData.Names[Index].c_str(),
				MacroData.Values[Index].c_str()
			});
		}

		MacroData.Macros.push_back({ nullptr, nullptr });
		return MacroData;
	}

    // ───────── Shader disk cache ─────────
	uint64_t ShaderCacheFNV1a(const void* Data, size_t Size, uint64_t Seed = 14695981039346656037ULL)
	{
		const uint8_t* Bytes = static_cast<const uint8_t*>(Data);
		uint64_t Hash = Seed;
		for (size_t i = 0; i < Size; ++i)
		{
			Hash ^= Bytes[i];
			Hash *= 1099511628211ULL;
		}
		return Hash;
	}

	uint64_t ShaderCacheHashFile(const std::filesystem::path& FilePath, uint64_t Seed)
	{
		std::ifstream File(FilePath, std::ios::binary);
		if (!File) return Seed;
		std::vector<char> Buffer((std::istreambuf_iterator<char>(File)), std::istreambuf_iterator<char>());
		return ShaderCacheFNV1a(Buffer.data(), Buffer.size(), Seed);
	}

	uint64_t ShaderCacheHashAllIncludes(uint64_t Seed)
	{
		const std::filesystem::path Dir(FPaths::ShaderDir());
		std::error_code EC;
		if (!std::filesystem::exists(Dir, EC)) return Seed;

		std::vector<std::filesystem::path> Includes;
		for (auto It = std::filesystem::recursive_directory_iterator(Dir, EC);
			!EC && It != std::filesystem::recursive_directory_iterator();
			It.increment(EC))
		{
			if (It->is_regular_file(EC) && It->path().extension() == L".hlsli")
			{
				Includes.push_back(It->path());
			}
		}
		std::sort(Includes.begin(), Includes.end());

		uint64_t Hash = Seed;
		for (const auto& IncPath : Includes)
		{
			const std::string Name = IncPath.filename().string();
			Hash = ShaderCacheFNV1a(Name.data(), Name.size(), Hash);
			Hash = ShaderCacheHashFile(IncPath, Hash);
		}
		return Hash;
	}

	std::filesystem::path ShaderCacheDir()
	{
		return std::filesystem::path(FPaths::RootDir()) / L"Asset" / L"ShaderCache";
	}

	std::string ComputeShaderCacheKey(const FShaderCompileKey& Key, const char* Entry, const char* Target)
	{
		uint64_t Hash = 14695981039346656037ULL;

		std::filesystem::path SourcePath(FPaths::RootDir());
		SourcePath /= FPaths::ToWide(Key.FilePath);
		Hash = ShaderCacheHashFile(SourcePath, Hash);

		Hash = ShaderCacheHashAllIncludes(Hash);

		for (const FShaderMacro& M : Key.Macros)
		{
			Hash = ShaderCacheFNV1a(M.Name.data(), M.Name.size(), Hash);
			Hash = ShaderCacheFNV1a("=", 1, Hash);
			Hash = ShaderCacheFNV1a(M.Value.data(), M.Value.size(), Hash);
			Hash = ShaderCacheFNV1a(";", 1, Hash);
		}

		const size_t EntryLen = std::strlen(Entry);
		Hash = ShaderCacheFNV1a(Entry, EntryLen, Hash);
		Hash = ShaderCacheFNV1a(":", 1, Hash);
		const size_t TargetLen = std::strlen(Target);
		Hash = ShaderCacheFNV1a(Target, TargetLen, Hash);

		char Buffer[24];
		std::snprintf(Buffer, sizeof(Buffer), "%016llx", static_cast<unsigned long long>(Hash));
		return std::string(Buffer);
	}

	bool TryLoadCachedShaderBlob(const std::string& CacheKey, ID3DBlob** OutBlob)
	{
		const std::filesystem::path Path = ShaderCacheDir() / (CacheKey + ".cso");
		std::error_code EC;
		if (!std::filesystem::exists(Path, EC)) return false;

		std::ifstream File(Path, std::ios::binary | std::ios::ate);
		if (!File) return false;
		const std::streamsize Size = File.tellg();
		if (Size <= 0) return false;
		File.seekg(0);

		ID3DBlob* Blob = nullptr;
		if (FAILED(D3DCreateBlob(static_cast<SIZE_T>(Size), &Blob)) || !Blob) return false;
		File.read(static_cast<char*>(Blob->GetBufferPointer()), Size);
		if (!File)
		{
			Blob->Release();
			return false;
		}
		*OutBlob = Blob;
		return true;
	}

	void SaveCachedShaderBlob(const std::string& CacheKey, ID3DBlob* Blob)
	{
		if (!Blob) return;
		std::error_code EC;
		std::filesystem::create_directories(ShaderCacheDir(), EC);

		const std::filesystem::path Path = ShaderCacheDir() / (CacheKey + ".cso");
		std::ofstream File(Path, std::ios::binary | std::ios::trunc);
		if (!File) return;
		File.write(static_cast<const char*>(Blob->GetBufferPointer()),
			static_cast<std::streamsize>(Blob->GetBufferSize()));
	}

	bool IsRemovedAmbientMaterialParam(const FString& ParamName)
	{
		return ParamName == "AmbientColor" || ParamName == "AmbientMap" || ParamName == "bHasAmbientMap";
	}

	FString NormalizeLegacyMaterialParamName(const FString& ParamName)
	{
		if (ParamName == "DiffuseColor")
		{
			return "BaseColor";
		}

		return ParamName;
	}

	bool IsRuntimeOnlyDecalMaterialParam(const FString& ParamName)
	{
		return ParamName == "InvDecalWorld" || ParamName == "DecalColorTint";
	}

	bool IsSupportedDecalMaterialParam(const FString& ParamName)
	{
		return ParamName == "BaseColor" ||
			ParamName == "Opacity" ||
			ParamName == "DiffuseMap" ||
			ParamName == "NormalMap" ||
			ParamName == "bHasNormalMap" ||
			ParamName == "EmissiveColor";
	}

	bool ShouldProcessMaterialParam(EMaterialDomain MaterialDomain, const FString& ParamName)
	{
		if (IsRemovedAmbientMaterialParam(ParamName))
		{
			return false;
		}

		if (MaterialDomain == EMaterialDomain::Decal)
		{
			return IsSupportedDecalMaterialParam(ParamName);
		}

		return !IsRuntimeOnlyDecalMaterialParam(ParamName);
	}

	void ApplyMaterialDefaults(UMaterial* Material)
	{
		if (Material == nullptr)
		{
			return;
		}

		FMaterialParamValue ExistingParamValue;
		if (Material->GetEffectiveMaterialDomain() == EMaterialDomain::Decal)
		{
			if (!Material->GetParam("BaseColor", ExistingParamValue))
			{
				Material->SetParam("BaseColor", FMaterialParamValue(FVector(1.0f, 1.0f, 1.0f)));
			}

			if (!Material->GetParam("Opacity", ExistingParamValue))
			{
				Material->SetParam("Opacity", FMaterialParamValue(1.0f));
			}

			if (!Material->GetParam("EmissiveColor", ExistingParamValue))
			{
				Material->SetParam("EmissiveColor", FMaterialParamValue(FVector::ZeroVector));
			}

			if (!Material->GetParam("DiffuseMap", ExistingParamValue))
			{
				if (UTexture* DefaultWhite = FResourceManager::Get().GetTexture("DefaultWhite"))
				{
					Material->SetParam("DiffuseMap", FMaterialParamValue(DefaultWhite));
				}
			}
		}

		if (!Material->GetParam("NormalMap", ExistingParamValue))
		{
			if (UTexture* DefaultNormal = FResourceManager::Get().GetTexture("DefaultNormal"))
			{
				Material->SetParam("NormalMap", FMaterialParamValue(DefaultNormal));
			}
		}

		if (!Material->GetParam("bHasNormalMap", ExistingParamValue))
		{
			Material->SetParam("bHasNormalMap", FMaterialParamValue(false));
		}
	}
}

#pragma region __BINARY__

FString FResourceManager::MakeStaticMeshAssetPath(const FString& SourcePath) const
{
	namespace fs = std::filesystem;

	fs::path SourceFsPath(FPaths::ToWide(FPaths::Normalize(SourcePath)));
	if (SourceFsPath.is_absolute())
	{
		const fs::path RootPath(FPaths::RootDir());
		const fs::path RelativeToRoot = SourceFsPath.lexically_relative(RootPath);
		if (!RelativeToRoot.empty() && *RelativeToRoot.begin() != fs::path(L".."))
		{
			SourceFsPath = RelativeToRoot;
		}
	}

	fs::path RelativeAssetPath = SourceFsPath;
	RelativeAssetPath.replace_extension(L".asset");

	fs::path AbsoluteAssetPath(FPaths::ToAbsolute(RelativeAssetPath.generic_wstring()));
	fs::create_directories(AbsoluteAssetPath.parent_path());

	return FPaths::ToUtf8(RelativeAssetPath.generic_wstring());
}

FString FResourceManager::MakeSkeletalMeshAssetPath(const FString& SourcePath) const
{
	return MakeStaticMeshAssetPath(SourcePath);
}

FString FResourceManager::MakeSkeletonAssetPath(const FString& SourcePath) const
{
	namespace fs = std::filesystem;

	fs::path SourceFsPath(FPaths::ToWide(FPaths::Normalize(SourcePath)));
	if (SourceFsPath.is_absolute())
	{
		const fs::path RootPath(FPaths::RootDir());
		const fs::path RelativeToRoot = SourceFsPath.lexically_relative(RootPath);
		if (!RelativeToRoot.empty() && *RelativeToRoot.begin() != fs::path(L".."))
		{
			SourceFsPath = RelativeToRoot;
		}
	}

	fs::path RelativeAssetPath = SourceFsPath.parent_path() / (SourceFsPath.stem().wstring() + L"_Skeleton.asset");
	fs::path AbsoluteAssetPath(FPaths::ToAbsolute(RelativeAssetPath.generic_wstring()));
	fs::create_directories(AbsoluteAssetPath.parent_path());

	return FPaths::ToUtf8(RelativeAssetPath.generic_wstring());
}

FString FResourceManager::MakeSkeletalMeshMaterialOverridePath(const FString& SourcePath) const
{
	return SourcePath + ".skelmat.json";
}

bool FResourceManager::RegisterStaticMeshAsset(const FString& AssetPath)
{
	const FString NormalizedPath = FPaths::Normalize(AssetPath);
	if (!IsStaticMeshAssetPath(NormalizedPath))
	{
		return false;
	}

	FStaticMeshBinaryHeader Header;
	if (!BinarySerializer.ReadStaticMeshHeader(NormalizedPath, Header))
	{
		return false;
	}

	FStaticMeshResource Resource;
	Resource.Name = NormalizedPath;
	Resource.Path = NormalizedPath;
	Resource.bPreload = false;
	Resource.bNormalizeToUnitCube = false;
	StaticMeshRegistry[Resource.Name] = Resource;

	if (std::find(ObjFilePaths.begin(), ObjFilePaths.end(), NormalizedPath) == ObjFilePaths.end())
	{
		ObjFilePaths.push_back(NormalizedPath);
	}

	return true;
}

bool FResourceManager::RegisterSkeletalMeshAsset(const FString& AssetPath)
{
	const FString NormalizedPath = FPaths::Normalize(AssetPath);
	if (!IsStaticMeshAssetPath(NormalizedPath))
	{
		return false;
	}

	FStaticMeshBinaryHeader Header;
	if (!BinarySerializer.ReadSkeletalMeshHeader(NormalizedPath, Header))
	{
		return false;
	}

	if (std::find(SkeletalMeshFilePaths.begin(), SkeletalMeshFilePaths.end(), NormalizedPath) == SkeletalMeshFilePaths.end())
	{
		SkeletalMeshFilePaths.push_back(NormalizedPath);
	}

	return true;
}

bool FResourceManager::RegisterSkeletonAsset(const FString& AssetPath)
{
	const FString NormalizedPath = FPaths::Normalize(AssetPath);
	if (!IsStaticMeshAssetPath(NormalizedPath))
	{
		return false;
	}

	FStaticMeshBinaryHeader Header;
	if (!BinarySerializer.ReadSkeletonHeader(NormalizedPath, Header))
	{
		return false;
	}

	if (std::find(SkeletonFilePaths.begin(), SkeletonFilePaths.end(), NormalizedPath) == SkeletonFilePaths.end())
	{
		SkeletonFilePaths.push_back(NormalizedPath);
	}

	return true;
}

void FResourceManager::PreloadStaticMeshes()
{
	for (const auto& [Key, Resource] : StaticMeshRegistry)
	{
		if (!Resource.bPreload)
		{
			continue;
		}

		if (LoadStaticMesh(Resource.Path) == nullptr)
		{
			UE_LOG("Failed to load static mesh from Resource.ini: %s", Resource.Path.c_str());
		}
	}
}

#pragma endregion


namespace ResourceKey
{
	constexpr const char* Font = "Font";
	constexpr const char* Particle = "Particle";
	constexpr const char* Material = "Material";
	constexpr const char* StaticMesh = "StaticMesh";
	constexpr const char* Path = "Path";
	constexpr const char* Columns = "Columns";
	constexpr const char* Rows = "Rows";
	constexpr const char* Preload = "Preload";
	constexpr const char* NormalizeToUnitCube = "NormalizeToUnitCube";
	constexpr const char* Type = "Type";
}

//	RootPath 하위에 있는 모든 사용 가능 Asset에 대하여 초기화 및 재추적하는 함수
void FResourceManager::LoadFromAssetDirectory(const FString& Path)
{
	//	초기화
	ObjFilePaths.clear();
	FontFilePaths.clear();
	TextureFilePaths.clear();
	MaterialFilePaths.clear();
	ParticleFilePaths.clear();
	SkeletalMeshFilePaths.clear();
	SkeletonFilePaths.clear();
	StaticMeshRegistry.clear();

	InitializeDefaultResources(CachedDevice.Get());

	namespace fs = std::filesystem;
	
	const fs::path RootPath = fs::path(FPaths::RootDir()) / FPaths::ToWide(Path);
	
	const fs::path ProjectRootPath = fs::path(FPaths::RootDir());

	if (!fs::exists(RootPath) || !fs::is_directory(RootPath))
	{
		UE_LOG("[ResourceManager] Fatal Error : Root Directory Error");
		return;
	}

	TArray<FString> MaterialFiles;
	TArray<FString> MaterialInstanceFiles;
#if WITH_EDITOR
	TArray<FString> ObjSourceFiles;
	TArray<FString> FbxSourceFiles;
#endif

	for (const auto& Entry : fs::recursive_directory_iterator(RootPath))
	{
		if (!Entry.is_regular_file())
		{
			continue;
		}

		const fs::path& FilePath = Entry.path();
		const std::wstring Extension = ToLowerPathToken(FilePath.extension().wstring());

		if (Extension == L".meta")
		{
			continue;
		}
		
		const FString RelativePath = FPaths::ToRelativeString(FilePath.generic_wstring());

		if (Extension == L".obj")
		{
#if WITH_EDITOR
			ObjSourceFiles.push_back(RelativePath);
#endif
		}
		else if (Extension == L".asset")
		{
			RegisterCompiledAssetPath(*this, RelativePath);
		}
		else if (Extension == L".mtl")
		{
			// OBJ MTL files are import sources. Runtime materials are .mat/.matinst assets.
		}
		else if (Extension == L".mat")
		{
			MaterialFilePaths.push_back(RelativePath);
			MaterialFiles.push_back(RelativePath);
		}
		else if (Extension == L".matinst")
		{
			MaterialInstanceFiles.push_back(RelativePath);
		}
		else if (	Extension == L".png" ||	Extension == L".dds" ||	Extension == L".jpg" ||	Extension == L".jpeg")
		{
			const FTextureAssetMeta Meta = LoadOrCreateTextureMeta(FilePath);

			if (Meta.Type == EAssetMetaType::Font)
			{
				FontFilePaths.push_back(RelativePath);
				RegisterFont(FName(RelativePath.c_str()), RelativePath, Meta.Columns, Meta.Rows);
			}
			else if (Meta.Type == EAssetMetaType::Particle)
			{
				ParticleFilePaths.push_back(RelativePath);
				RegisterParticle(FName(RelativePath.c_str()), RelativePath, Meta.Columns, Meta.Rows);
			}
			else if (Meta.Type == EAssetMetaType::Texture)
			{
				TextureFilePaths.push_back(RelativePath);
				LoadTexture(RelativePath, CachedDevice.Get());
			}
			//else
			//{
			//	TextureFilePaths.push_back(RelativePath);
			//}
		}
        else if (Extension == L".fbx")
        {
#if WITH_EDITOR
            FbxSourceFiles.push_back(RelativePath);
#endif
        }
	}

#if WITH_EDITOR
	FObjStaticMeshImporter ObjImporter;
	for (const FString& ObjSourcePath : ObjSourceFiles)
	{
		ObjImporter.ImportIfNeeded(*this, ObjSourcePath, &MaterialFiles);
	}

	FFbxAssetImporter FbxAssetImporter;
	for (const FString& FbxSourcePath : FbxSourceFiles)
	{
		const EFbxAssetImportResult ImportResult =
			FbxAssetImporter.ImportIfNeeded(*this, FbxSourcePath, &MaterialFiles);
		if (ImportResult == EFbxAssetImportResult::Failed)
		{
			UE_LOG("[ResourceManager] FBX import failed: %s", FbxSourcePath.c_str());
		}
	}
#endif

	for (const FString& MaterialPath : MaterialFiles)
	{
		if (std::find(MaterialFilePaths.begin(), MaterialFilePaths.end(), MaterialPath) == MaterialFilePaths.end())
		{
			MaterialFilePaths.push_back(MaterialPath);
		}
		DeserializeMaterial(MaterialPath);
	}

	for (const FString& MaterialInstancePath : MaterialInstanceFiles)
	{
		DeserializeMaterial(MaterialInstancePath);
	}

	//	TODO : Material, Texture Load
	PreloadStaticMeshes();

	if (LoadGPUResources(CachedDevice.Get()))
	{
		UE_LOG("Complete Load Resources!");
	}
	else
	{
		UE_LOG("Failed to Load Resources...");
	}
}

void FResourceManager::RefreshFromAssetDirectory(const FString& Path)
{
	namespace fs = std::filesystem;

	ObjFilePaths.clear();
	FontFilePaths.clear();
	TextureFilePaths.clear();
	MaterialFilePaths.clear();
	ParticleFilePaths.clear();
	SkeletalMeshFilePaths.clear();
	SkeletonFilePaths.clear();
	StaticMeshRegistry.clear();
	FontResources.clear();
	ParticleResources.clear();

	const fs::path RootPath = fs::path(FPaths::RootDir()) / FPaths::ToWide(Path);
	const fs::path ProjectRootPath = fs::path(FPaths::RootDir());

	TArray<FString> MaterialFiles;
	TArray<FString> MaterialInstanceFiles;
#if WITH_EDITOR
	TArray<FString> ObjSourceFiles;
	TArray<FString> FbxSourceFiles;
#endif

	if (!fs::exists(RootPath) || !fs::is_directory(RootPath))
	{
		UE_LOG("[ResourceManager] Refresh Failed : Root Directory Error");
		return;
	}

	try
	{
		for (const auto& Entry : fs::recursive_directory_iterator(RootPath, fs::directory_options::skip_permission_denied))
		{
			if (!Entry.is_regular_file())
			{
				continue;
			}

			const fs::path& FilePath = Entry.path();
			const std::wstring Extension = ToLowerPathToken(FilePath.extension().wstring());

			if (Extension == L".meta")
			{
				continue;
			}

			const FString RelativePath = FPaths::ToUtf8(fs::relative(FilePath, ProjectRootPath).generic_wstring());

			if (Extension == L".obj")
			{
#if WITH_EDITOR
				ObjSourceFiles.push_back(RelativePath);
#endif
			}
			else if (Extension == L".asset")
			{
				RegisterCompiledAssetPath(*this, RelativePath);
			}
			else if (Extension == L".mtl")
			{
				// OBJ MTL files are import sources. Runtime materials are .mat/.matinst assets.
			}
			else if (Extension == L".mat")
			{
				MaterialFilePaths.push_back(RelativePath);
				MaterialFiles.push_back(RelativePath);
			}
			else if (Extension == L".matinst")
			{
				MaterialInstanceFiles.push_back(RelativePath);
			}
			else if (
				Extension == L".png" ||
				Extension == L".dds" ||
				Extension == L".jpg" ||
				Extension == L".jpeg")
			{
				const FTextureAssetMeta Meta = LoadOrCreateTextureMeta(FilePath);

				if (Meta.Type == EAssetMetaType::Font)
				{
					FontFilePaths.push_back(RelativePath);
					RegisterFont(FName(RelativePath.c_str()), RelativePath, Meta.Columns, Meta.Rows);
				}
				else if (Meta.Type == EAssetMetaType::Particle)
				{
					ParticleFilePaths.push_back(RelativePath);
					RegisterParticle(FName(RelativePath.c_str()), RelativePath, Meta.Columns, Meta.Rows);
				}
				else if (Meta.Type == EAssetMetaType::Texture)
				{
					TextureFilePaths.push_back(RelativePath);
					LoadTexture(RelativePath, CachedDevice.Get());
				}
				//else
				//{
				//	TextureFilePaths.push_back(RelativePath);
				//}
			}
			else if (Extension == L".fbx")
			{
#if WITH_EDITOR
				FbxSourceFiles.push_back(RelativePath);
#endif
			}
		}
	}
	catch (const std::exception& Ex)
	{
		UE_LOG("[ResourceManager] Refresh Exception: %s", Ex.what());
	}

#if WITH_EDITOR
	FObjStaticMeshImporter ObjImporter;
	for (const FString& ObjSourcePath : ObjSourceFiles)
	{
		ObjImporter.ImportIfNeeded(*this, ObjSourcePath, &MaterialFiles);
	}

	FFbxAssetImporter FbxAssetImporter;
	for (const FString& FbxSourcePath : FbxSourceFiles)
	{
		const EFbxAssetImportResult ImportResult =
			FbxAssetImporter.ImportIfNeeded(*this, FbxSourcePath, &MaterialFiles);
		if (ImportResult == EFbxAssetImportResult::Failed)
		{
			UE_LOG("[ResourceManager] FBX import failed: %s", FbxSourcePath.c_str());
		}
	}
#endif

	for (const FString& MaterialPath : MaterialFiles)
	{
		if (std::find(MaterialFilePaths.begin(), MaterialFilePaths.end(), MaterialPath) == MaterialFilePaths.end())
		{
			MaterialFilePaths.push_back(MaterialPath);
		}
		DeserializeMaterial(MaterialPath);
	}

	for (const FString& MaterialInstancePath : MaterialInstanceFiles)
	{
		DeserializeMaterial(MaterialInstancePath);
	}

	if (CachedDevice && !LoadGPUResources(CachedDevice.Get()))
	{
		UE_LOG("[ResourceManager] Refresh Failed : GPU Resource Reload Error");
	}

	UE_LOG("[ResourceManager] Asset Refresh Complete");
}

void FResourceManager::DeleteAllCacheFiles()
{
	namespace fs = std::filesystem;

	const fs::path ProjectRootPath(FPaths::RootDir());
	const TArray<fs::path> CacheRootPaths = {
		ProjectRootPath / "Asset" / "Mesh",
		ProjectRootPath / "Asset" / "Fbx",
	};

	TArray<fs::path> CandidateDirectories;
	for (const fs::path& CacheRootPath : CacheRootPaths)
	{
		if (!fs::exists(CacheRootPath) || !fs::is_directory(CacheRootPath))
		{
			continue;
		}

		for (const auto& Entry : fs::recursive_directory_iterator(CacheRootPath))
		{
			if (!Entry.is_regular_file())
			{
				continue;
			}

			const fs::path& FilePath = Entry.path();
			if (ToLowerPathToken(FilePath.extension().wstring()) != L".asset")
			{
				continue;
			}

			const FString RelativePath = FPaths::ToRelativeString(FilePath.generic_wstring());
			FStaticMeshBinaryHeader Header;
			if (!BinarySerializer.ReadAssetHeader(RelativePath, Header) ||
				!IsManagedCompiledAssetType(Header.AssetType))
			{
				continue;
			}

			std::error_code Ec;
			fs::remove(FilePath, Ec);
			fs::path DirectoryPath = FilePath.parent_path();
			while (!DirectoryPath.empty() && IsPathUnder(DirectoryPath, CacheRootPath))
			{
				CandidateDirectories.push_back(DirectoryPath);
				if (DirectoryPath == CacheRootPath)
				{
					break;
				}
				DirectoryPath = DirectoryPath.parent_path();
			}
		}
	}

	std::sort(CandidateDirectories.begin(), CandidateDirectories.end(), [](const fs::path& Left, const fs::path& Right)
	{
		const std::wstring LeftPath = Left.generic_wstring();
		const std::wstring RightPath = Right.generic_wstring();
		if (LeftPath.size() != RightPath.size())
		{
			return LeftPath.size() > RightPath.size();
		}
		return LeftPath > RightPath;
	});
	CandidateDirectories.erase(std::unique(CandidateDirectories.begin(), CandidateDirectories.end()), CandidateDirectories.end());

	for (const fs::path& DirectoryPath : CandidateDirectories)
	{
		std::error_code Ec;
		if (fs::exists(DirectoryPath, Ec) && fs::is_directory(DirectoryPath, Ec) && fs::is_empty(DirectoryPath, Ec))
		{
			fs::remove(DirectoryPath, Ec);
		}
	}

	UE_LOG("[ResourceManager] All mesh cache files removed");
}

FTextureAssetMeta FResourceManager::LoadOrCreateTextureMeta(const std::filesystem::path& FilePath) const
{
	namespace fs = std::filesystem;
	using namespace json;

	FTextureAssetMeta Meta;

	fs::path MetaPath = FilePath;
	MetaPath.replace_extension(L".meta");

	// 1. meta 파일 존재 → 로드
	if (fs::exists(MetaPath))
	{
		std::ifstream MetaFile(MetaPath);
		if (MetaFile.is_open())
		{
			FString Content((std::istreambuf_iterator<char>(MetaFile)),
							std::istreambuf_iterator<char>());

			JSON Root = JSON::Load(Content);
			if (Root.JSONType() == JSON::Class::Object)
			{
				if (Root.hasKey(ResourceKey::Type))
				{
					const FString TypeStr = Root[ResourceKey::Type].ToString();
					if (TypeStr == "Font")
					{
						Meta.Type = EAssetMetaType::Font;
					}
					else if (TypeStr == "Particle")
					{
						Meta.Type = EAssetMetaType::Particle;
					}
					else if (TypeStr == "Texture")
					{
						Meta.Type = EAssetMetaType::Texture;
					}
				}

				if (Root.hasKey(ResourceKey::Columns))
				{
					Meta.Columns = std::max(1, static_cast<int32>(Root[ResourceKey::Columns].ToInt()));
				}

				if (Root.hasKey(ResourceKey::Rows))
				{
					Meta.Rows = std::max(1, static_cast<int32>(Root[ResourceKey::Rows].ToInt()));
				}
			}
		}

		return Meta;
	}

	// 2. 없으면 기본 생성
	const std::wstring ParentDir = ToLowerPathToken(FilePath.parent_path().filename().wstring());

	if (ParentDir == L"font")
	{
		Meta.Type = EAssetMetaType::Font;
	}
	else if (ParentDir == L"particle")
	{
		Meta.Type = EAssetMetaType::Particle;
	}
	else if (ParentDir == L"texture" || ParentDir == L"textures")
	{
		Meta.Type = EAssetMetaType::Texture;
	}
	else
	{
		Meta.Type = EAssetMetaType::None;
	}

	Meta.Columns = 1;
	Meta.Rows = 1;

	// 3. meta 파일 생성
	JSON Root = JSON::Make(JSON::Class::Object);
	
	if (Meta.Type == EAssetMetaType::Font)
	{
		Root[ResourceKey::Type] = "Font";
	}
	else if (Meta.Type == EAssetMetaType::Particle)
	{
		Root[ResourceKey::Type] = "Particle";
	}
	else if (Meta.Type == EAssetMetaType::Texture)
	{
		Root[ResourceKey::Type] = "Texture";
	}
	else
	{
		Root[ResourceKey::Type] = "None";
	}
	
	Root[ResourceKey::Columns] = Meta.Columns;
	Root[ResourceKey::Rows] = Meta.Rows;

	std::ofstream OutFile(MetaPath);
	if (OutFile.is_open())
	{
		OutFile << Root.dump();
	}

	return Meta;
}

bool FResourceManager::LoadGPUResources(ID3D11Device* Device)
{
	if (!Device)
	{
		return false;
	}

	for (auto& [Key, Resource] : FontResources)
	{
		if (Resource.Texture != nullptr && Resource.Texture->GetSRV() != nullptr)
		{
			continue;
		}

		if (!FontLoader.Load(Resource.Name, Resource.Path, Resource.Columns, Resource.Rows, Device, Resource))
		{
			UE_LOG("Failed to load Font atlas: %s", Resource.Path.c_str());
			return false;
		}
	}

	for (auto& Resource : ParticleResources | std::views::values)
	{
		if (Resource.Texture != nullptr && Resource.Texture->GetSRV() != nullptr)
		{
			continue;
		}

		if (!ParticleLoader.Load(Resource.Name, Resource.Path, Resource.Columns, Resource.Rows, Device, Resource))
		{
			UE_LOG("Failed to load Particle atlas: %s", Resource.Path.c_str());
			return false;
		}
	}

	return true;
}

void FResourceManager::InitializeDefaultResources(ID3D11Device* Device)
{
	if (!Device) return;

	D3D11_TEXTURE2D_DESC Desc = {};
	Desc.Width = 1;
	Desc.Height = 1;
	Desc.MipLevels = 1;
	Desc.ArraySize = 1;
	Desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	Desc.SampleDesc.Count = 1;
	Desc.Usage = D3D11_USAGE_IMMUTABLE;
	Desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

	constexpr uint32_t WhitePixel = 0xFFFFFFFF;
	constexpr uint32_t FlatNormalPixel = 0xFFFF8080;
	D3D11_SUBRESOURCE_DATA WhiteInitData = { &WhitePixel, 4, 0 };
	D3D11_SUBRESOURCE_DATA NormalInitData = { &FlatNormalPixel, 4, 0 };

	UTexture* DefaultWhiteTextureResource = UObjectManager::Get().CreateObject<UTexture>();
	UTexture* DefaultNormalTextureResource = UObjectManager::Get().CreateObject<UTexture>();

	Device->CreateTexture2D(&Desc, &WhiteInitData, DefaultWhiteTexture.ReleaseAndGetAddressOf());
	if (DefaultWhiteTexture)
	{
		Device->CreateShaderResourceView(DefaultWhiteTexture.Get(), nullptr, DefaultWhiteTextureResource->GetAddressOfSRV());
		Textures["DefaultWhite"] = DefaultWhiteTextureResource;
	}

	Device->CreateTexture2D(&Desc, &NormalInitData, DefaultNormalTexture.ReleaseAndGetAddressOf());
	if (DefaultNormalTexture)
	{
		Device->CreateShaderResourceView(DefaultNormalTexture.Get(), nullptr, DefaultNormalTextureResource->GetAddressOfSRV());
		Textures["DefaultNormal"] = DefaultNormalTextureResource;
	}

	UMaterial* DefaultMat = GetOrCreateMaterial("DefaultWhite", DefaultUberLitShaderPath);
	DefaultMat->MaterialParams["BaseColor"] = FMaterialParamValue(DefaultMat->MaterialData.BaseColor);
	DefaultMat->MaterialParams["SpecularColor"] = FMaterialParamValue(DefaultMat->MaterialData.SpecularColor);
	DefaultMat->MaterialParams["EmissiveColor"] = FMaterialParamValue(DefaultMat->MaterialData.EmissiveColor);
	DefaultMat->MaterialParams["Shininess"] = FMaterialParamValue(DefaultMat->MaterialData.Shininess);
	DefaultMat->MaterialParams["Opacity"] = FMaterialParamValue(DefaultMat->MaterialData.Opacity);

	UTexture* DefaultWhite = FResourceManager::Get().GetTexture("DefaultWhite");
	UTexture* DefaultNormal = FResourceManager::Get().GetTexture("DefaultNormal");

	if (DefaultMat->MaterialData.bHasDiffuseTexture)
		DefaultMat->MaterialParams["DiffuseMap"] = FMaterialParamValue(FResourceManager::Get().LoadTexture(DefaultMat->MaterialData.DiffuseTexPath, Device));
	else
		DefaultMat->MaterialParams["DiffuseMap"] = FMaterialParamValue(DefaultWhite);

	if (DefaultMat->MaterialData.bHasSpecularTexture)
		DefaultMat->MaterialParams["SpecularMap"] = FMaterialParamValue(FResourceManager::Get().LoadTexture(DefaultMat->MaterialData.SpecularTexPath, Device));
	else
		DefaultMat->MaterialParams["SpecularMap"] = FMaterialParamValue(DefaultWhite);

	if (DefaultMat->MaterialData.bHasNormalTexture)
		DefaultMat->MaterialParams["NormalMap"] = FMaterialParamValue(FResourceManager::Get().LoadTexture(DefaultMat->MaterialData.NormalTexPath, Device));
	else
		DefaultMat->MaterialParams["NormalMap"] = FMaterialParamValue(DefaultNormal);

	if (DefaultMat->MaterialData.bHasBumpTexture)
		DefaultMat->MaterialParams["BumpMap"] = FMaterialParamValue(FResourceManager::Get().LoadTexture(DefaultMat->MaterialData.BumpTexPath, Device));
	else
		DefaultMat->MaterialParams["BumpMap"] = FMaterialParamValue(DefaultWhite);

	DefaultMat->MaterialParams["bHasDiffuseMap"] = FMaterialParamValue(DefaultMat->MaterialData.bHasDiffuseTexture);
	DefaultMat->MaterialParams["bHasSpecularMap"] = FMaterialParamValue(DefaultMat->MaterialData.bHasSpecularTexture);
	DefaultMat->MaterialParams["bHasNormalMap"] = FMaterialParamValue(DefaultMat->MaterialData.bHasNormalTexture);
	DefaultMat->MaterialParams["bHasBumpMap"] = FMaterialParamValue(DefaultMat->MaterialData.bHasBumpTexture);

	DefaultMat->MaterialParams["ScrollUV"] = FMaterialParamValue(FVector2(0.0f, 0.0f));

	const FColor DefaultRedColor(uint32(255), uint32(50), uint32(150), uint32(255));
	UMaterial* DefaultRedMat = GetOrCreateMaterial("DefaultRed", DefaultUberLitShaderPath);
	DefaultRedMat->MaterialData.BaseColor = FVector(DefaultRedColor.R, DefaultRedColor.G, DefaultRedColor.B);
	DefaultRedMat->MaterialData.Opacity = DefaultRedColor.A;
	DefaultRedMat->MaterialParams["BaseColor"] = FMaterialParamValue(DefaultRedMat->MaterialData.BaseColor);
	DefaultRedMat->MaterialParams["SpecularColor"] = FMaterialParamValue(DefaultRedMat->MaterialData.SpecularColor);
	DefaultRedMat->MaterialParams["EmissiveColor"] = FMaterialParamValue(DefaultRedMat->MaterialData.EmissiveColor);
	DefaultRedMat->MaterialParams["Shininess"] = FMaterialParamValue(DefaultRedMat->MaterialData.Shininess);
	DefaultRedMat->MaterialParams["Opacity"] = FMaterialParamValue(DefaultRedMat->MaterialData.Opacity);
	DefaultRedMat->MaterialParams["DiffuseMap"] = FMaterialParamValue(DefaultWhite);
	DefaultRedMat->MaterialParams["SpecularMap"] = FMaterialParamValue(DefaultWhite);
	DefaultRedMat->MaterialParams["NormalMap"] = FMaterialParamValue(DefaultNormal);
	DefaultRedMat->MaterialParams["BumpMap"] = FMaterialParamValue(DefaultWhite);
	DefaultRedMat->MaterialParams["bHasDiffuseMap"] = FMaterialParamValue(false);
	DefaultRedMat->MaterialParams["bHasSpecularMap"] = FMaterialParamValue(false);
	DefaultRedMat->MaterialParams["bHasNormalMap"] = FMaterialParamValue(false);
	DefaultRedMat->MaterialParams["bHasBumpMap"] = FMaterialParamValue(false);
	DefaultRedMat->MaterialParams["ScrollUV"] = FMaterialParamValue(FVector2(0.0f, 0.0f));
	
	// Outline Material
	UMaterial* OutlineMat = GetOrCreateMaterial("OutlineMaterial", "Shaders/OutlinePostProcess.hlsl");
	OutlineMat->SetParam("OutlineColor", FMaterialParamValue(FVector4(1.0f, 0.5f, 0.0f, 1.0f)));
	OutlineMat->SetParam("OutlineThicknessPixels", FMaterialParamValue(5.0f));
	OutlineMat->SetParam("OutlineViewportSize", FMaterialParamValue(FVector2(800.0f, 600.0f)));
	OutlineMat->SetParam("OutlineViewportOrigin", FMaterialParamValue(FVector2(0.0f, 0.0f)));
}

void FResourceManager::ReleaseGPUResources()
{
	for (auto& [Key, Texture] : Textures)
	{
		if (Texture)
		{
			UObjectManager::Get().DestroyObject(Texture);
		}
	}
	Textures.clear();
	DefaultWhiteTexture.Reset();
	DefaultNormalTexture.Reset();

	std::unordered_set<UMaterial*> UniqueMaterials;
	for (auto& [Key, Material] : Materials)
	{
		if (Material && UniqueMaterials.insert(Material).second)
		{
			UObjectManager::Get().DestroyObject(Material);
		}
	}
	Materials.clear();

	std::unordered_set<UMaterialInstance*> UniqueMaterialInstances;
	for (auto& [Key, MaterialInst] : MaterialInstances)
	{
		if (MaterialInst && UniqueMaterialInstances.insert(MaterialInst).second)
		{
			UObjectManager::Get().DestroyObject(MaterialInst);
		}
	}
	MaterialInstances.clear();

	std::unordered_set<UShader*> UniqueShaders;
	for (auto& [Key, Shader] : Shaders)
	{
		if (Shader)
		{
			UniqueShaders.insert(Shader);
		}
	}
	Shaders.clear();

	for (auto& [Key, Shader] : ShaderVariants)
	{
		if (Shader)
		{
			UniqueShaders.insert(Shader);
		}
	}
	ShaderVariants.clear();

	for (UShader* Shader : UniqueShaders)
	{
		UObjectManager::Get().DestroyObject(Shader);
	}

	for (auto& [Key, Font] : FontResources)
	{
		if (Font.Texture)
		{
			UObjectManager::Get().DestroyObject(Font.Texture);
		}
	}
	FontResources.clear();

	for (auto& [Key, Particle] : ParticleResources)
	{
		if (Particle.Texture)
		{
			UObjectManager::Get().DestroyObject(Particle.Texture);
		}
	}
	ParticleResources.clear();

	for (auto& [Path, StaticMeshAsset] : StaticMeshes)
	{
		UObjectManager::Get().DestroyObject(StaticMeshAsset);
	}
	StaticMeshes.clear();
	StaticMeshRegistry.clear();

	for (auto& [Path, SkeletalMeshAsset] : SkeletalMeshes)
	{
		UObjectManager::Get().DestroyObject(SkeletalMeshAsset);
	}
	SkeletalMeshes.clear();

	for (auto& [Path, SkeletonAsset] : Skeletons)
	{
		UObjectManager::Get().DestroyObject(SkeletonAsset);
	}
	Skeletons.clear();

	// D3D state object caches
	SamplerStates.clear();
	DepthStencilStates.clear();
	BlendStates.clear();
	RasterizerStates.clear();

	DefaultWhiteTexture.Reset();
	CachedDevice.Reset();
}

bool FResourceManager::LoadShader(const FString& FilePath,
								  const FString& VSEntryPoint,
								  const FString& PSEntryPoint,
								  const D3D_SHADER_MACRO* Defines)
{
	return LoadShaderInternal(
		BuildShaderCompileKey(FilePath, VSEntryPoint, PSEntryPoint, Defines),
		nullptr,
		0,
		true);
}

bool FResourceManager::LoadShader(const FString& FilePath,
								  const FString& VSEntryPoint,
								  const FString& PSEntryPoint,
								  const D3D11_INPUT_ELEMENT_DESC* InputElements,
								  UINT InputElementCount,
								  const D3D_SHADER_MACRO* Defines)
{
	return LoadShaderInternal(
		BuildShaderCompileKey(FilePath, VSEntryPoint, PSEntryPoint, Defines),
		InputElements,
		InputElementCount,
		true);
}

bool FResourceManager::LoadShader(const FShaderCompileKey& CompileKey)
{
	return LoadShaderInternal(CompileKey, nullptr, 0, false);
}

bool FResourceManager::LoadShader(const FShaderCompileKey& CompileKey,
								  const D3D11_INPUT_ELEMENT_DESC* InputElements,
								  UINT InputElementCount)
{
	return LoadShaderInternal(CompileKey, InputElements, InputElementCount, false);
}

bool FResourceManager::LoadShaderInternal(const FShaderCompileKey& CompileKey,
										  const D3D11_INPUT_ELEMENT_DESC* InputElements,
										  UINT InputElementCount,
										  bool bRegisterPathAlias)
{
	if (!CachedDevice.Get())
	{
		return false;
	}

	const FShaderCompileKey NormalizedKey = NormalizeShaderCompileKey(CompileKey);
	if (UShader* ExistingShader = GetShaderVariant(NormalizedKey))
	{
		if (bRegisterPathAlias && NormalizedKey.Macros.empty())
		{
			Shaders[NormalizedKey.FilePath] = ExistingShader;
		}
		return true;
	}

	UShader* Shader = UObjectManager::Get().CreateObject<UShader>();
	Shader->FilePath = NormalizedKey.FilePath;

	TComPtr<ID3DBlob> VSBlob;
	TComPtr<ID3DBlob> PSBlob;
	TComPtr<ID3DBlob> ErrorBlob;
	const FCompiledShaderMacroData MacroData = BuildCompiledShaderMacroData(NormalizedKey.Macros);
	const D3D_SHADER_MACRO* RawMacros = MacroData.Macros.empty() ? nullptr : MacroData.Macros.data();
	const FString MacroLog = BuildShaderMacroLogString(NormalizedKey.Macros);

    const std::string VSCacheKey = ComputeShaderCacheKey(NormalizedKey, NormalizedKey.VSEntryPoint.c_str(), "vs_5_0");
    HRESULT hr = S_OK;
    bool bVSFromCache = false;
    {
	    ID3DBlob* CachedBlob = nullptr;
	    if (TryLoadCachedShaderBlob(VSCacheKey, &CachedBlob))
	    {
	        VSBlob.Attach(CachedBlob);
	        bVSFromCache = true;
	        UE_LOG("[ShaderCompile] VS cache-hit %s entry=%s key=%s",
                NormalizedKey.FilePath.c_str(),
                NormalizedKey.VSEntryPoint.c_str(),
                VSCacheKey.c_str());
	    }
    }
    if (!bVSFromCache)
    {
        const auto VSBegin = std::chrono::steady_clock::now();
        hr = D3DCompileFromFile(FPaths::ToWide(NormalizedKey.FilePath).c_str(), RawMacros, D3D_COMPILE_STANDARD_FILE_INCLUDE,
            NormalizedKey.VSEntryPoint.c_str(), "vs_5_0", 0, 0, &VSBlob, &ErrorBlob);
        const auto VSMs = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - VSBegin).count();
        {
            FString MacroDump;
            for (const FShaderMacro& M : NormalizedKey.Macros)
            {
                if (!MacroDump.empty()) MacroDump += ",";
                MacroDump += M.Name;
            }
            UE_LOG("[ShaderCompile] VS %lldms %s [%s] entry=%s",
                static_cast<long long>(VSMs),
                NormalizedKey.FilePath.c_str(),
                MacroDump.c_str(),
                NormalizedKey.VSEntryPoint.c_str());
        }
        if (SUCCEEDED(hr))
        {
            SaveCachedShaderBlob(VSCacheKey, VSBlob.Get());
        }
    }
	if (FAILED(hr))
	{
		if (ErrorBlob)
		{
			UE_LOG("Vertex Shader Compile Error (%s): %s", NormalizedKey.FilePath.c_str(), static_cast<const char*>(ErrorBlob->GetBufferPointer()));
		}
		else
		{
			UE_LOG("Failed to compile vertex shader: %s", NormalizedKey.FilePath.c_str());
		}
		UObjectManager::Get().DestroyObject(Shader);
		return false;
	}
	if (!Shader->ReflectShader(VSBlob.Get(), CachedDevice.Get(), EShaderStage::Vertex))
	{
		UE_LOG("Failed to reflect vertex shader: %s", NormalizedKey.FilePath.c_str());
		UObjectManager::Get().DestroyObject(Shader);
		return false;
	}
	ErrorBlob.Reset();

    const std::string PSCacheKey = ComputeShaderCacheKey(NormalizedKey, NormalizedKey.PSEntryPoint.c_str(), "ps_5_0");
    hr = S_OK;
    bool bPSFromCache = false;
    {
	    ID3DBlob* CachedBlob = nullptr;
	    if (TryLoadCachedShaderBlob(PSCacheKey, &CachedBlob))
	    {
	        PSBlob.Attach(CachedBlob);
	        bPSFromCache = true;
	        UE_LOG("[ShaderCompile] PS cache-hit %s entry=%s key=%s",
                NormalizedKey.FilePath.c_str(),
                NormalizedKey.PSEntryPoint.c_str(),
                PSCacheKey.c_str());
	    }
    }
    if (!bPSFromCache)
    {
        const auto PSBegin = std::chrono::steady_clock::now();
        hr = D3DCompileFromFile(FPaths::ToWide(NormalizedKey.FilePath).c_str(), RawMacros, D3D_COMPILE_STANDARD_FILE_INCLUDE,
            NormalizedKey.PSEntryPoint.c_str(), "ps_5_0", 0, 0, &PSBlob, &ErrorBlob);
        const auto PSMs = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - PSBegin).count();
        {
            FString MacroDump;
            for (const FShaderMacro& M : NormalizedKey.Macros)
            {
                if (!MacroDump.empty()) MacroDump += ",";
                MacroDump += M.Name;
            }
            UE_LOG("[ShaderCompile] PS %lldms %s [%s] entry=%s",
                static_cast<long long>(PSMs),
                NormalizedKey.FilePath.c_str(),
                MacroDump.c_str(),
                NormalizedKey.PSEntryPoint.c_str());
        }
        if (SUCCEEDED(hr))
        {
            SaveCachedShaderBlob(PSCacheKey, PSBlob.Get());
        }
    }
	if (FAILED(hr))
	{
		if (ErrorBlob)
		{
			UE_LOG("Pixel Shader Compile Error (%s): %s", NormalizedKey.FilePath.c_str(), static_cast<const char*>(ErrorBlob->GetBufferPointer()));
		}
		else
		{
			UE_LOG("Failed to compile pixel shader: %s", NormalizedKey.FilePath.c_str());
		}
		UObjectManager::Get().DestroyObject(Shader);
		return false;
	}
	if (!Shader->ReflectShader(PSBlob.Get(), CachedDevice.Get(), EShaderStage::Pixel))
	{
		UE_LOG("Failed to reflect pixel shader: %s", NormalizedKey.FilePath.c_str());
		UObjectManager::Get().DestroyObject(Shader);
		return false;
	}

	hr = CachedDevice->CreateVertexShader(VSBlob->GetBufferPointer(), VSBlob->GetBufferSize(), nullptr,
		&Shader->ShaderData.VS);
	if (FAILED(hr))
	{
		UE_LOG("Failed to create vertex shader: %s", NormalizedKey.FilePath.c_str());
		UObjectManager::Get().DestroyObject(Shader);
		return false;
	}

	hr = CachedDevice->CreatePixelShader(PSBlob->GetBufferPointer(), PSBlob->GetBufferSize(), nullptr,
		&Shader->ShaderData.PS);
	if (FAILED(hr))
	{
		UE_LOG("Failed to create pixel shader: %s", NormalizedKey.FilePath.c_str());
		UObjectManager::Get().DestroyObject(Shader);
		return false;
	}

	if (InputElements != nullptr && InputElementCount > 0)
	{
		if (Shader->ShaderData.InputLayout)
		{
			Shader->ShaderData.InputLayout->Release();
			Shader->ShaderData.InputLayout = nullptr;
		}

		hr = CachedDevice->CreateInputLayout(InputElements, InputElementCount, VSBlob->GetBufferPointer(),
			VSBlob->GetBufferSize(), &Shader->ShaderData.InputLayout);
		if (FAILED(hr))
		{
			UE_LOG("Failed to create input layout: %s", NormalizedKey.FilePath.c_str());
			UObjectManager::Get().DestroyObject(Shader);
			return false;
		}
	}

	ShaderVariants[NormalizedKey] = Shader;
	if (bRegisterPathAlias && NormalizedKey.Macros.empty())
	{
		Shaders[NormalizedKey.FilePath] = Shader;
	}

	return true;
}

//ID3DBlob* CompileShaderWithDefines(const WCHAR* filename,
//                                   const D3D_SHADER_MACRO* defines,
//                                   const char* entryPoint,
//                                   const char* shaderModel)
//{
//    UINT flags = D3DCOMPILE_ENABLE_STRICTNESS;
//
//#if defined(DEBUG) || defined(_DEBUG)
//    flags |= D3DCOMPILE_DEBUG;
//#endif
//
//    ID3DBlob* byteCode = nullptr;
//    ID3DBlob* errors = nullptr;
//
//    HRESULT hr = D3DCompileFromFile(
//        filename,
//        defines,
//        D3D_COMPILE_STANDARD_FILE_INCLUDE,
//        entryPoint,
//        shaderModel,
//        flags,
//        0,
//        &byteCode,
//        &errors);
//
//    if (FAILED(hr))
//    {
//        if (errors)
//        {
//            MessageBoxA(nullptr, (char*)errors->GetBufferPointer(), "Shader Compile Error", MB_OK);
//            errors->Release();
//        }
//        return nullptr;
//    }
//
//    return byteCode;
//}

UShader* FResourceManager::GetShader(const FString& FilePath) const
{
	auto It = Shaders.find(FilePath);
	return (It != Shaders.end()) ? It->second : nullptr;
}

UShader* FResourceManager::GetShaderVariant(const FShaderCompileKey& CompileKey) const
{
	const FShaderCompileKey NormalizedKey = NormalizeShaderCompileKey(CompileKey);
	auto It = ShaderVariants.find(NormalizedKey);
	return (It != ShaderVariants.end()) ? It->second : nullptr;
}

void FResourceManager::RegisterMaterialAliases(UMaterial* Material, const FString& FilePath, const FString& LegacyName)
{
	if (Material == nullptr)
	{
		return;
	}

	const FString NormalizedFilePath = FPaths::Normalize(FilePath);
	if (!NormalizedFilePath.empty())
	{
		Materials[NormalizedFilePath] = Material;
	}

	if (!LegacyName.empty() && Materials.find(LegacyName) == Materials.end())
	{
		Materials[LegacyName] = Material;
	}
}

void FResourceManager::RegisterMaterialInstanceAliases(UMaterialInstance* MaterialInstance, const FString& FilePath, const FString& LegacyName)
{
	if (MaterialInstance == nullptr)
	{
		return;
	}

	const FString NormalizedFilePath = FPaths::Normalize(FilePath);
	if (!NormalizedFilePath.empty())
	{
		MaterialInstances[NormalizedFilePath] = MaterialInstance;
	}

	if (!LegacyName.empty() && MaterialInstances.find(LegacyName) == MaterialInstances.end())
	{
		MaterialInstances[LegacyName] = MaterialInstance;
	}
}

TArray<FString> FResourceManager::GetMaterialNames() const
{
	TArray<FString> Names;
	std::unordered_set<UMaterial*> SeenMaterials;
	Names.reserve(Materials.size());
	for (const auto& [Name, Mat] : Materials)
	{
		if (Mat && SeenMaterials.insert(Mat).second)
		{
			Names.push_back(Mat->GetFilePath().empty() ? Name : Mat->GetFilePath());
		}
	}
	return Names;
}

TArray<FString> FResourceManager::GetMaterialInterfaceNames() const
{
	TArray<FString> Names;
	Names.reserve(Materials.size() + MaterialInstances.size());
	std::unordered_set<UMaterialInterface*> SeenMaterials;

	for (const auto& [Name, Mat] : Materials)
	{
		if (Mat && SeenMaterials.insert(Mat).second)
		{
			Names.push_back(Mat->GetFilePath().empty() ? Name : Mat->GetFilePath());
		}
	}

	for (const auto& [Name, MatInst] : MaterialInstances)
	{
		if (MatInst && SeenMaterials.insert(MatInst).second)
		{
			Names.push_back(MatInst->GetFilePath().empty() ? Name : MatInst->GetFilePath());
		}
	}

	return Names;
}

UMaterial* FResourceManager::GetMaterial(const FString& MaterialName) const
{
	auto It = Materials.find(MaterialName);
	if (It != Materials.end())
	{
		return It->second;
	}

	const FString NormalizedPath = FPaths::Normalize(MaterialName);
	It = Materials.find(NormalizedPath);
	return (It != Materials.end()) ? It->second : nullptr;
}

// 매개변수 없이 가장 간단한 Material을 생성
UMaterial* FResourceManager::GetOrCreateMaterial(const FString& Path, const FString& ShaderName)
{
	UMaterial* Material = GetMaterial(Path);
	if (Material)
	{
		return Material;
	}

	Material = UObjectManager::Get().CreateObject<UMaterial>();
	Material->Name = Path;
	Material->FilePath = Path;

	UShader* Shader = GetOrTryLoadMaterialShader(*this, ShaderName);
	Material->SetShader(Shader);

	RegisterMaterialAliases(Material, Path, Path);

	return Material;
}

UMaterial* FResourceManager::GetOrCreateMaterial(const FString& Name, const FString& Path, const FString& ShaderName)
{
	UMaterial* Material = GetMaterial(Name);
	if (Material)
	{
		return Material;
	}

	Material = UObjectManager::Get().CreateObject<UMaterial>();
	Material->Name = Name;
	Material->FilePath = Path;

	UShader* Shader = GetOrTryLoadMaterialShader(*this, ShaderName);
	Material->SetShader(Shader);

	RegisterMaterialAliases(Material, Path, Name);

	return Material;
}

bool FResourceManager::LoadMaterial(const FString& MtlFilePath, const FString& ShaderName, ID3D11Device* Device)
{
	if (MtlFilePath.empty())
	{
		return false;
	}

	const FString Extension = ToLowerAscii(std::filesystem::path(FPaths::ToWide(MtlFilePath)).extension().string());
	if (Extension == ".mat" || Extension == ".matinst")
	{
		return DeserializeMaterial(MtlFilePath);
	}

	UE_LOG("[ResourceManager] Runtime LoadMaterial only accepts .mat/.matinst assets. MTL is editor import-only: %s", MtlFilePath.c_str());
	return false;
}

UMaterialInstance* FResourceManager::CreateMaterialInstance(const FString& Path, UMaterial* Parent)
{
	if (UMaterialInstance* ExistingInstance = GetMaterialInstance(Path))
	{
		ExistingInstance->Parent = Parent;
		ExistingInstance->OverridedParams.clear();
		ExistingInstance->ClearLightingModelOverride();
		ExistingInstance->SetOwnership(EMaterialInstanceOwnership::ResourceManaged);
		ExistingInstance->ShaderBinding.reset();
		return ExistingInstance;
	}

	UMaterialInstance* Instance = UObjectManager::Get().CreateObject<UMaterialInstance>();
	Instance->Parent = Parent;
	Instance->Name = Path;
	Instance->FilePath = Path;
	Instance->SetOwnership(EMaterialInstanceOwnership::ResourceManaged);
	RegisterMaterialInstanceAliases(Instance, Path, Path);
	return Instance;
}

UMaterialInstance* FResourceManager::GetMaterialInstance(const FString& Path) const
{
	auto It = MaterialInstances.find(Path);
	if (It != MaterialInstances.end())
	{
		return It->second;
	}

	const FString NormalizedPath = FPaths::Normalize(Path);
	It = MaterialInstances.find(NormalizedPath);
	return (It != MaterialInstances.end()) ? It->second : nullptr;
}

UMaterialInterface* FResourceManager::GetMaterialInterface(const FString& Name) const
{
	UMaterial* Mat = GetMaterial(Name);
	if (Mat)
	{
		return Mat;
	}
	return GetMaterialInstance(Name);
}

bool FResourceManager::SerializeMaterial(const FString& MatFilePath, const UMaterial* Material)
{
	using json::JSON;
	JSON Root = JSON::Make(JSON::Class::Object);
	const EMaterialDomain MaterialDomain = Material->GetEffectiveMaterialDomain();
	Root["Name"] = Material->Name;
	Root["Shader"] = Material->Shader ? Material->Shader->FilePath : "";
	Root["MaterialDomain"] = ToMaterialDomainString(MaterialDomain);
	if (MaterialDomain == EMaterialDomain::Surface)
	{
		Root["LightingModel"] = ToLightingModelString(Material->GetEffectiveLightingModel());
	}

	JSON Params = JSON::Make(JSON::Class::Array);
	for (const auto& [ParamName, ParamValue] : Material->MaterialParams)
	{
		const FString SerializedParamName = NormalizeLegacyMaterialParamName(ParamName);
		if (!ShouldProcessMaterialParam(MaterialDomain, SerializedParamName))
		{
			continue;
		}

		JSON Param = JSON::Make(JSON::Class::Object);
		Param["Name"] = SerializedParamName;
		if (std::holds_alternative<bool>(ParamValue.Value))
		{
			Param["Type"] = "Bool";
			Param["Value"] = std::get<bool>(ParamValue.Value);
		}
		else if (std::holds_alternative<int>(ParamValue.Value))
		{
			Param["Type"] = "Int";
			Param["Value"] = std::get<int>(ParamValue.Value);
		}
		else if (std::holds_alternative<uint32>(ParamValue.Value))
		{
			Param["Type"] = "UInt";
			Param["Value"] = std::get<uint32>(ParamValue.Value);
		}
		else if (std::holds_alternative<float>(ParamValue.Value))
		{
			Param["Type"] = "Float";
			Param["Value"] = std::get<float>(ParamValue.Value);
		}
		else if (std::holds_alternative<FVector2>(ParamValue.Value))
		{
			const FVector2& Vec = std::get<FVector2>(ParamValue.Value);
			Param["Type"] = "Vector2";
			JSON VecArray2 = JSON::Make(JSON::Class::Array);
			VecArray2.append(Vec.X);
			VecArray2.append(Vec.Y);
			Param["Value"] = VecArray2;
		}
		else if (std::holds_alternative<FVector>(ParamValue.Value))
		{
			const FVector& Vec = std::get<FVector>(ParamValue.Value);
			Param["Type"] = "Vector3";
			JSON VecArray3 = JSON::Make(JSON::Class::Array);
			VecArray3.append(Vec.X);
			VecArray3.append(Vec.Y);
			VecArray3.append(Vec.Z);
			Param["Value"] = VecArray3;
		}
		else if (std::holds_alternative<FVector4>(ParamValue.Value))
		{
			const FVector4& Vec = std::get<FVector4>(ParamValue.Value);
			Param["Type"] = "Vector4";
			JSON VecArray4 = JSON::Make(JSON::Class::Array);
			VecArray4.append(Vec.X);
			VecArray4.append(Vec.Y);
			VecArray4.append(Vec.Z);
			VecArray4.append(Vec.W);
			Param["Value"] = VecArray4;
		}
		else if (std::holds_alternative<FMatrix>(ParamValue.Value))
		{
			const FMatrix& Mat = std::get<FMatrix>(ParamValue.Value);
			Param["Type"] = "Matrix4";
			JSON MatArray = JSON::Make(JSON::Class::Array);
			for (int Row = 0; Row < 4; ++Row)
			{
				JSON RowArray = JSON::Make(JSON::Class::Array);
				for (int Col = 0; Col < 4; ++Col)
				{
					RowArray.append(Mat.M[Row][Col]);
				}
				MatArray.append(RowArray);
			}
			Param["Value"] = MatArray;
		}
		else if (std::holds_alternative<UTexture*>(ParamValue.Value))
		{
			UTexture* Texture = std::get<UTexture*>(ParamValue.Value);
			Param["Type"] = "Texture";
			Param["Value"] = Texture ? Texture->GetFilePath() : "";
		}
		Params.append(Param);
	}
	Root["Params"] = Params;
	const std::filesystem::path AbsoluteMatPath(FPaths::ToAbsolute(FPaths::ToWide(MatFilePath)));
	std::filesystem::create_directories(AbsoluteMatPath.parent_path());
	std::ofstream OutFile(AbsoluteMatPath);
	if (!OutFile.is_open())
	{
		UE_LOG("Failed to open material file for writing: %s", MatFilePath.c_str());
		return false;
	}
	OutFile << Root.dump(4);
	return true;
}

bool FResourceManager::SerializeMaterialInstance(const FString& MatInstFilePath, const UMaterialInstance* MaterialInstance)
{
	using json::JSON;
	JSON Root = JSON::Make(JSON::Class::Object);
	const EMaterialDomain MaterialDomain = MaterialInstance->GetEffectiveMaterialDomain();
	Root["Name"] = MaterialInstance->GetFilePath().empty() ? MaterialInstance->GetName() : MaterialInstance->GetFilePath();
	Root["Parent"] = MaterialInstance->Parent->GetFilePath().empty()
		? MaterialInstance->Parent->GetName()
		: MaterialInstance->Parent->GetFilePath();
	if (MaterialDomain == EMaterialDomain::Surface && MaterialInstance->HasLightingModelOverride())
	{
		Root["LightingModel"] = ToLightingModelString(MaterialInstance->GetLightingModelOverride());
	}
	JSON Params = JSON::Make(JSON::Class::Array);
	for (const auto& [ParamName, ParamValue] : MaterialInstance->OverridedParams)
	{
		const FString SerializedParamName = NormalizeLegacyMaterialParamName(ParamName);
		if (!ShouldProcessMaterialParam(MaterialDomain, SerializedParamName))
		{
			continue;
		}

		JSON Param = JSON::Make(JSON::Class::Object);
		Param["Name"] = SerializedParamName;
		if (std::holds_alternative<bool>(ParamValue.Value))
		{
			Param["Type"] = "Bool";
			Param["Value"] = std::get<bool>(ParamValue.Value);
		}
		else if (std::holds_alternative<int>(ParamValue.Value))
		{
			Param["Type"] = "Int";
			Param["Value"] = std::get<int>(ParamValue.Value);
		}
		else if (std::holds_alternative<uint32>(ParamValue.Value))
		{
			Param["Type"] = "UInt";
			Param["Value"] = std::get<uint32>(ParamValue.Value);
		}
		else if (std::holds_alternative<float>(ParamValue.Value))
		{
			Param["Type"] = "Float";
			Param["Value"] = std::get<float>(ParamValue.Value);
		}
		else if (std::holds_alternative<FVector2>(ParamValue.Value))
		{
			const FVector2& Vec = std::get<FVector2>(ParamValue.Value);
			Param["Type"] = "Vector2";
			Param["Value"] = JSON::Make(JSON::Class::Array);
			Param["Value"].append(Vec.X);
			Param["Value"].append(Vec.Y);
		}
		else if (std::holds_alternative<FVector>(ParamValue.Value))
		{
			const FVector& Vec = std::get<FVector>(ParamValue.Value);
			Param["Type"] = "Vector3";
			Param["Value"] = JSON::Make(JSON::Class::Array);
			Param["Value"].append(Vec.X);
			Param["Value"].append(Vec.Y);
			Param["Value"].append(Vec.Z);
		}
		else if (std::holds_alternative<FVector4>(ParamValue.Value))
		{
			const FVector4& Vec = std::get<FVector4>(ParamValue.Value);
			Param["Type"] = "Vector4";
			Param["Value"] = JSON::Make(JSON::Class::Array);
			Param["Value"].append(Vec.X);
			Param["Value"].append(Vec.Y);
			Param["Value"].append(Vec.Z);
			Param["Value"].append(Vec.W);
		}
		else if (std::holds_alternative<FMatrix>(ParamValue.Value))
		{
			const FMatrix& Mat = std::get<FMatrix>(ParamValue.Value);
			Param["Type"] = "Matrix4";
			JSON MatArray = JSON::Make(JSON::Class::Array);
			for (int Row = 0; Row < 4; ++Row)
			{
				JSON RowArray = JSON::Make(JSON::Class::Array);
				for (int Col = 0; Col < 4; ++Col)
				{
					RowArray.append(Mat.M[Row][Col]);
				}
				MatArray.append(RowArray);
			}
			Param["Value"] = MatArray;
		}
		else if (std::holds_alternative<UTexture*>(ParamValue.Value))
		{
			UTexture* Texture = std::get<UTexture*>(ParamValue.Value);
			Param["Type"] = "Texture";
			Param["Value"] = Texture ? Texture->GetFilePath() : "";
		}
		Params.append(Param);
	}
	Root["OverridedParams"] = Params;
	const std::filesystem::path AbsoluteMatInstPath(FPaths::ToAbsolute(FPaths::ToWide(MatInstFilePath)));
	std::filesystem::create_directories(AbsoluteMatInstPath.parent_path());
	std::ofstream OutFile(AbsoluteMatInstPath);
	if (!OutFile.is_open())
	{
		UE_LOG("Failed to open material instance file for writing: %s", MatInstFilePath.c_str());
		return false;
	}
	OutFile << Root.dump(4);
	return true;
}

bool FResourceManager::DeserializeMaterial(const FString& MatFilePath)
{
	using json::JSON;

	const FString NormalizedMatFilePath = FPaths::Normalize(MatFilePath);
	std::ifstream MatFile(std::filesystem::path(FPaths::ToAbsolute(FPaths::ToWide(NormalizedMatFilePath))));
	if (!MatFile.is_open())
	{
		UE_LOG("Failed to open material file: %s", MatFilePath.c_str());
		return false;
	}

	FString FileContent((std::istreambuf_iterator<char>(MatFile)), std::istreambuf_iterator<char>());
	JSON Root = JSON::Load(FileContent);

	if (Root.hasKey("Parent"))
	{
		const FString MatName = Root["Name"].ToString();
		const FString ParentKey = Root["Parent"].ToString();
		UMaterial* ParentMat = GetMaterial(ParentKey);

		if (!ParentMat)
		{
			UE_LOG("Parent material not found: %s", Root["Parent"].ToString().c_str());
			return false;
		}

		UMaterialInstance* MatInstance = CreateMaterialInstance(NormalizedMatFilePath, ParentMat);
		MatInstance->Name = MatName.empty() ? NormalizedMatFilePath : MatName;
		MatInstance->FilePath = NormalizedMatFilePath;
		if (ParentMat->GetEffectiveMaterialDomain() == EMaterialDomain::Surface && Root.hasKey("LightingModel"))
		{
			ELightingModel LightingModel = ELightingModel::Phong;
			if (TryParseLightingModel(Root["LightingModel"].ToString(), LightingModel))
			{
				MatInstance->SetLightingModelOverride(LightingModel);
			}
		}
		else
		{
			MatInstance->ClearLightingModelOverride();
		}

		for (auto& Param : Root["OverridedParams"].ArrayRange())
		{
			FString ParamName = NormalizeLegacyMaterialParamName(Param["Name"].ToString());
			if (!ShouldProcessMaterialParam(ParentMat->GetEffectiveMaterialDomain(), ParamName))
			{
				continue;
			}

			FString Type = Param["Type"].ToString();
			if (Type == "Bool")
			{
				bool Value = Param["Value"].ToBool();
				MatInstance->SetParam(ParamName, FMaterialParamValue(Value));
			}
			else if (Type == "Int")
			{
				int Value = Param["Value"].ToInt();
				MatInstance->SetParam(ParamName, FMaterialParamValue(Value));
			}
			else if (Type == "UInt")
			{
				uint32 Value = static_cast<uint32>(Param["Value"].ToInt());
				MatInstance->SetParam(ParamName, FMaterialParamValue(Value));
			}
			else if (Type == "Float")
			{
				float Value = static_cast<float>(Param["Value"].ToFloat());
				MatInstance->SetParam(ParamName, FMaterialParamValue(Value));
			}
			else if (Type == "Vector2")
			{
				FVector2 Value(
					static_cast<float>(Param["Value"][0].ToFloat()),
					static_cast<float>(Param["Value"][1].ToFloat()));
				MatInstance->SetParam(ParamName, FMaterialParamValue(Value));
			}
			else if (Type == "Vector3")
			{
				FVector Value(
					static_cast<float>(Param["Value"][0].ToFloat()),
					static_cast<float>(Param["Value"][1].ToFloat()),
					static_cast<float>(Param["Value"][2].ToFloat()));
				MatInstance->SetParam(ParamName, FMaterialParamValue(Value));
			}
			else if (Type == "Vector4")
			{
				FVector4 Value(
					static_cast<float>(Param["Value"][0].ToFloat()),
					static_cast<float>(Param["Value"][1].ToFloat()),
					static_cast<float>(Param["Value"][2].ToFloat()),
					static_cast<float>(Param["Value"][3].ToFloat()));
				MatInstance->SetParam(ParamName, FMaterialParamValue(Value));
			}
			else if (Type == "Matrix4")
			{
				FMatrix Value;
				for (int Row = 0; Row < 4; ++Row)
				{
					for (int Col = 0; Col < 4; ++Col)
					{
						Value.M[Row][Col] = static_cast<float>(Param["Value"][Row][Col].ToFloat());
					}
				}
				MatInstance->SetParam(ParamName, FMaterialParamValue(Value));
			}
			else if (Type == "Texture")
			{
				FString TexPath = Param["Value"].ToString();
				UTexture* Texture = LoadTexture(TexPath, CachedDevice.Get());
				if (Texture)
				{
					MatInstance->SetParam(ParamName, FMaterialParamValue(Texture));
				}
			}
		}

		RegisterMaterialInstanceAliases(MatInstance, NormalizedMatFilePath, MatName);
		return true;
	}

	FString MatName = Root["Name"].ToString();
	FString ShaderPath = Root["Shader"].ToString();
	UMaterial* Material = GetMaterial(NormalizedMatFilePath);
	if (Material == nullptr && !MatName.empty())
	{
		Material = GetMaterial(MatName);
	}
	if (Material == nullptr)
	{
		Material = UObjectManager::Get().CreateObject<UMaterial>();
	}

	Material->Name = MatName.empty() ? NormalizedMatFilePath : MatName;
	Material->FilePath = NormalizedMatFilePath;
	Material->SetShader(GetOrTryLoadMaterialShader(*this, ShaderPath.empty() ? DefaultUberLitShaderPath : ShaderPath));
	Material->MaterialParams.clear();
	EMaterialDomain MaterialDomain = EMaterialDomain::Surface;
	if (Root.hasKey("MaterialDomain"))
	{
		TryParseMaterialDomain(Root["MaterialDomain"].ToString(), MaterialDomain);
	}
	Material->SetMaterialDomain(MaterialDomain);

	ELightingModel LightingModel = ELightingModel::Phong;
	if (MaterialDomain == EMaterialDomain::Surface && Root.hasKey("LightingModel"))
	{
		TryParseLightingModel(Root["LightingModel"].ToString(), LightingModel);
	}
	Material->SetLightingModel(LightingModel);

	for (auto& Param : Root["Params"].ArrayRange())
	{
		FString ParamName = NormalizeLegacyMaterialParamName(Param["Name"].ToString());
		if (!ShouldProcessMaterialParam(MaterialDomain, ParamName))
		{
			continue;
		}

		FString Type = Param["Type"].ToString();

		if (Type == "Bool")
		{
			bool Value = Param["Value"].ToBool();
			Material->SetParam(ParamName, FMaterialParamValue(Value));
		}
		else if (Type == "Int")
		{
			int Value = Param["Value"].ToInt();
			Material->SetParam(ParamName, FMaterialParamValue(Value));
		}
		else if (Type == "UInt")
		{
			uint32 Value = static_cast<uint32>(Param["Value"].ToInt());
			Material->SetParam(ParamName, FMaterialParamValue(Value));
		}
		else if (Type == "Float")
		{
			float Value = static_cast<float>(Param["Value"].ToFloat());
			Material->SetParam(ParamName, FMaterialParamValue(Value));
		}
		else if (Type == "Vector2")
		{
			FVector2 Value(
				static_cast<float>(Param["Value"][0].ToFloat()),
				static_cast<float>(Param["Value"][1].ToFloat()));
			Material->SetParam(ParamName, FMaterialParamValue(Value));
		}
		else if (Type == "Vector3")
		{
			FVector Value(
				static_cast<float>(Param["Value"][0].ToFloat()),
				static_cast<float>(Param["Value"][1].ToFloat()),
				static_cast<float>(Param["Value"][2].ToFloat()));
			Material->SetParam(ParamName, FMaterialParamValue(Value));
		}
		else if (Type == "Vector4")
		{
			FVector4 Value(
				static_cast<float>(Param["Value"][0].ToFloat()),
				static_cast<float>(Param["Value"][1].ToFloat()),
				static_cast<float>(Param["Value"][2].ToFloat()),
				static_cast<float>(Param["Value"][3].ToFloat()));
			Material->SetParam(ParamName, FMaterialParamValue(Value));
		}
		else if (Type == "Matrix4")
		{
			FMatrix Value;
			for (int Row = 0; Row < 4; ++Row)
			{
				for (int Col = 0; Col < 4; ++Col)
				{
					Value.M[Row][Col] = static_cast<float>(Param["Value"][Row][Col].ToFloat());
				}
			}
			Material->SetParam(ParamName, FMaterialParamValue(Value));
		}
		else if (Type == "Texture")
		{
			FString TexPath = Param["Value"].ToString();
			UTexture* Texture = LoadTexture(TexPath, CachedDevice.Get());
			if (Texture)
			{
				Material->SetParam(ParamName, FMaterialParamValue(Texture));
			}
		}
	}

	ApplyMaterialDefaults(Material);

	RegisterMaterialAliases(Material, NormalizedMatFilePath, MatName);

	return true;
}

UTexture* FResourceManager::GetTexture(const FString& Path) const
{
	auto It = Textures.find(Path);
	return (It != Textures.end()) ? It->second : nullptr;
}

UTexture* FResourceManager::LoadTexture(const FString& Path, ID3D11Device* Device)
{
	if (Device == nullptr)
	{
		Device = CachedDevice.Get();
	}

	if (UTexture* Cached = GetTexture(Path))
	{
		return Cached;
	}

	UTexture* Texture = UObjectManager::Get().CreateObject<UTexture>();
	if (!Texture->LoadFromFile(Path, Device))
	{
		return nullptr;
	}

	Textures[Path] = Texture;
	return Texture;
}

// --- Font ---
FFontResource* FResourceManager::FindFont(const FName& FontName)
{
	if (FontResources.empty())
	{
		return nullptr;
	}
	
	auto It = FontResources.find(FontName.ToString());
	return (It != FontResources.end()) ? &It->second : &FontResources.begin()->second;
}

const FFontResource* FResourceManager::FindFont(const FName& FontName) const
{
	if (FontResources.empty())
	{
		return nullptr;
	}
	
	//	Default인 경우 첫 Font Resource로 Fallback (반드시 하나 이상의 Font는 Upload
	auto It = FontResources.find(FontName.ToString());
	return (It != FontResources.end()) ? &It->second : &FontResources.begin()->second;
}

void FResourceManager::RegisterFont(const FName& FontName, const FString& InPath, uint32 Columns, uint32 Rows)
{
	FFontResource Resource;
	Resource.Name = FontName;
	Resource.Path = InPath;
	Resource.Columns = Columns;
	Resource.Rows = Rows;
	Resource.Texture = UObjectManager::Get().CreateObject<UTexture>();
	FontResources[FontName.ToString()] = Resource;
}

// --- Particle ---
FParticleResource* FResourceManager::FindParticle(const FName& ParticleName)
{
	//	마찬가지로 하나 이상의 Particle을 무조건 가지고 있어야 함.
	if (ParticleResources.empty())
	{
		return nullptr;
	}
	
	auto It = ParticleResources.find(ParticleName.ToString());
	return (It != ParticleResources.end()) ? &It->second : &ParticleResources.begin()->second;
}

const FParticleResource* FResourceManager::FindParticle(const FName& ParticleName) const
{
	if (ParticleResources.empty())
	{
		return nullptr;
	}
	
	auto It = ParticleResources.find(ParticleName.ToString());
	return (It != ParticleResources.end()) ? &It->second : &ParticleResources.begin()->second;
}

void FResourceManager::RegisterParticle(const FName& ParticleName, const FString& InPath, uint32 Columns, uint32 Rows)
{
	FParticleResource Resource;
	Resource.Name = ParticleName;
	Resource.Path = InPath;
	Resource.Columns = Columns;
	Resource.Rows = Rows;
	Resource.Texture = UObjectManager::Get().CreateObject<UTexture>();
	ParticleResources[ParticleName.ToString()] = Resource;
}

TArray<FString> FResourceManager::GetFontNames() const
{
	return FontFilePaths;
}

TArray<FString> FResourceManager::GetParticleNames() const
{
	return ParticleFilePaths;
}

UStaticMesh* FResourceManager::LoadStaticMesh(const FString& Path)
{
	FStaticMeshLoadOptions LoadOptions;
	return LoadStaticMeshWithOptions(Path, LoadOptions);
}

UStaticMesh* FResourceManager::LoadStaticMesh(const FString& Path, bool bNormalizeToUnitCube)
{
	(void)bNormalizeToUnitCube;
	FStaticMeshLoadOptions LoadOptions;
	LoadOptions.bNormalizeToUnitCube = false;
	return LoadStaticMeshWithOptions(Path, LoadOptions);
}

UStaticMesh* FResourceManager::LoadStaticMeshWithOptions(const FString& Path, const FStaticMeshLoadOptions& LoadOptions)
{
	(void)LoadOptions;
	FString MeshPath = FPaths::Normalize(Path);

	// 메모리 캐시 확인
	if (UStaticMesh* FoundMesh = FindStaticMesh(MeshPath))
	{
		return FoundMesh;
	}

	if (!IsStaticMeshAssetPath(MeshPath))
	{
		UE_LOG("[StaticMeshLoad] Runtime static mesh path must be .asset: %s", MeshPath.c_str());
		return nullptr;
	}

	double BinaryLoadSec = 0.0;

	const auto BinaryStart = std::chrono::steady_clock::now();
	FStaticMesh* LoadedMeshData = new FStaticMesh();
	if (!BinarySerializer.LoadStaticMesh(MeshPath, *LoadedMeshData))
	{
		delete LoadedMeshData;
		const auto BinaryEnd = std::chrono::steady_clock::now();
		BinaryLoadSec = std::chrono::duration<double>(BinaryEnd - BinaryStart).count();
		UE_LOG("[StaticMeshLoad] Failed | Path=%s | BinarySec=%.6f", MeshPath.c_str(), BinaryLoadSec);
		return nullptr;
	}

	const auto BinaryEnd = std::chrono::steady_clock::now();
	BinaryLoadSec = std::chrono::duration<double>(BinaryEnd - BinaryStart).count();
	LoadedMeshData->PathFileName = MeshPath;

	UE_LOG(
		"[StaticMeshLoad] Source=Binary | Path=%s | BinarySec=%.6f",
		MeshPath.c_str(),
		BinaryLoadSec);

	// Material 연결
	for (FStaticMeshMaterialSlot& Slot : LoadedMeshData->Slots)
	{
		Slot.Material = !Slot.MaterialAssetPath.empty()
			? GetMaterialInterface(Slot.MaterialAssetPath)
			: nullptr;

		if (Slot.Material == nullptr)
		{
			Slot.Material = GetMaterialInterface(Slot.SlotName);
		}

		if (Slot.Material == nullptr)
		{
			Slot.Material = GetMaterial("DefaultWhite");
		}
	}

	UStaticMesh* LoadedMesh = UObjectManager::Get().CreateObject<UStaticMesh>();
	LoadedMesh->SetMeshData(LoadedMeshData);

	if (FEngineSettings::Get().bEnableStaticMeshLOD)
	{
		const auto LodStart = std::chrono::steady_clock::now();
		FStaticMeshSimplifier::BuildLODs(LoadedMesh);
		const auto LodEnd = std::chrono::steady_clock::now();
		double LodSec = std::chrono::duration<double>(LodEnd - LodStart).count();
		UE_LOG("[StaticMeshLoad] Generated %d LODs for %s in %.3f sec",
			   LoadedMesh->GetValidLODCount(), MeshPath.c_str(), LodSec);
	}
	else
	{
		UE_LOG("[StaticMeshLoad] LOD generation skipped for %s (LOD is off)", MeshPath.c_str());
	}

	StaticMeshes.insert({ MeshPath, LoadedMesh });

	return LoadedMesh;
}

UStaticMesh* FResourceManager::FindStaticMesh(const FString& Path) const
{
	auto It = StaticMeshes.find(Path);
	if (It == StaticMeshes.end())
	{
		return nullptr;
	}

	return It->second;
}

TArray<FString> FResourceManager::GetStaticMeshPaths() const
{
	return ObjFilePaths;
}

bool FResourceManager::LoadSkeletalMeshMaterialOverrides(const FString& Path, USkeletalMesh* Mesh)
{
	using json::JSON;

	if (Mesh == nullptr || Mesh->GetMeshData() == nullptr)
	{
		return false;
	}

	const FString OverridePath = MakeSkeletalMeshMaterialOverridePath(Path);
	std::ifstream OverrideFile(FPaths::ToWide(OverridePath));
	if (!OverrideFile.is_open())
	{
		return false;
	}

	FString FileContent((std::istreambuf_iterator<char>(OverrideFile)), std::istreambuf_iterator<char>());
	JSON Root = JSON::Load(FileContent);
	if (Root.JSONType() != JSON::Class::Object || !Root.hasKey("Slots"))
	{
		return false;
	}

	JSON& SlotsNode = Root["Slots"];
	if (SlotsNode.JSONType() != JSON::Class::Array)
	{
		return false;
	}

	TMap<FString, FString> MaterialBySlotName;
	for (JSON& SlotNode : SlotsNode.ArrayRange())
	{
		if (SlotNode.JSONType() != JSON::Class::Object ||
			!SlotNode.hasKey("SlotName") ||
			!SlotNode.hasKey("Material"))
		{
			continue;
		}

		MaterialBySlotName[SlotNode["SlotName"].ToString()] = SlotNode["Material"].ToString();
	}

	bool bAppliedAny = false;
	for (FSkeletalMeshMaterialSlot& Slot : Mesh->GetMeshData()->MaterialSlots)
	{
		auto It = MaterialBySlotName.find(Slot.SlotName);
		if (It == MaterialBySlotName.end() || It->second.empty())
		{
			continue;
		}

		if (UMaterialInterface* Material = GetMaterialInterface(It->second))
		{
			Slot.Material = Material;
			bAppliedAny = true;
		}
	}

	return bAppliedAny;
}

bool FResourceManager::SaveSkeletalMeshMaterialOverrides(const FString& Path, const USkeletalMesh* Mesh) const
{
	using json::JSON;

	if (Mesh == nullptr || Mesh->GetMeshData() == nullptr)
	{
		return false;
	}

	JSON Root = JSON::Make(JSON::Class::Object);
	Root["Source"] = Path;

	JSON Slots = JSON::Make(JSON::Class::Array);
	for (const FSkeletalMeshMaterialSlot& Slot : Mesh->GetMeshData()->MaterialSlots)
	{
		JSON SlotNode = JSON::Make(JSON::Class::Object);
		SlotNode["SlotName"] = Slot.SlotName;
		SlotNode["Material"] = Slot.Material
			? (Slot.Material->GetFilePath().empty() ? Slot.Material->GetName() : Slot.Material->GetFilePath())
			: "";
		Slots.append(SlotNode);
	}
	Root["Slots"] = Slots;

	const FString OverridePath = MakeSkeletalMeshMaterialOverridePath(Path);
	std::ofstream OverrideFile(FPaths::ToWide(OverridePath));
	if (!OverrideFile.is_open())
	{
		UE_LOG("[SkeletalMeshMaterial] Failed to write override file: %s", OverridePath.c_str());
		return false;
	}

	OverrideFile << Root.dump(4);
	return true;
}

USkeletalMesh* FResourceManager::LoadSkeletalMesh(const FString& Path)
{
	const FString MeshPath = FPaths::Normalize(Path);
    if (USkeletalMesh* FoundMesh = FindSkeletalMesh(MeshPath))
    {
        return FoundMesh;
    }

	if (!IsStaticMeshAssetPath(MeshPath))
	{
		UE_LOG("[SkeletalMeshLoad] Runtime skeletal mesh path must be .asset: %s", MeshPath.c_str());
		return nullptr;
	}

	FSkeletalMesh* LoadedMeshData = new FSkeletalMesh();
	if (!BinarySerializer.LoadSkeletalMesh(MeshPath, *LoadedMeshData))
	{
		delete LoadedMeshData;
		UE_LOG("[SkeletalMeshLoad] Failed to load asset: %s", MeshPath.c_str());
		return nullptr;
	}

	LoadedMeshData->PathFileName = MeshPath;
	if (LoadedMeshData->SkeletonAssetPath.empty())
	{
		delete LoadedMeshData;
		UE_LOG("[SkeletalMeshLoad] Missing skeleton asset path: %s", MeshPath.c_str());
		return nullptr;
	}

	USkeleton* LoadedSkeleton = LoadSkeleton(LoadedMeshData->SkeletonAssetPath);
	if (LoadedSkeleton == nullptr)
	{
		delete LoadedMeshData;
		UE_LOG("[SkeletalMeshLoad] Failed to load skeleton %s for %s",
			LoadedMeshData->SkeletonAssetPath.c_str(),
			MeshPath.c_str());
		return nullptr;
	}

	USkeletalMesh* LoadedMesh = UObjectManager::Get().CreateObject<USkeletalMesh>();
	LoadedMesh->SetMeshData(LoadedMeshData);
	LoadedMesh->SetSkeleton(LoadedSkeleton);

    LoadSkeletalMeshMaterialOverrides(MeshPath, LoadedMesh);

    for (FSkeletalMeshMaterialSlot& Slot : LoadedMesh->GetMeshData()->MaterialSlots)
    {
		if (Slot.Material == nullptr && !Slot.MaterialAssetPath.empty())
		{
			Slot.Material = GetMaterialInterface(Slot.MaterialAssetPath);
		}

        if (Slot.Material == nullptr)
        {
            Slot.Material = GetMaterialInterface(Slot.SlotName);
        }

        if (Slot.Material == nullptr)
        {
            Slot.Material = GetMaterial("DefaultWhite");
        }
    }

    SkeletalMeshes.insert({ MeshPath, LoadedMesh });

    UE_LOG("[SkeletalMeshLoad] Loaded | Path=%s | Skeleton=%s",
		MeshPath.c_str(),
		LoadedMeshData->SkeletonAssetPath.c_str());

    return LoadedMesh;
}

USkeletalMesh* FResourceManager::FindSkeletalMesh(const FString& Path) const
{
    auto It = SkeletalMeshes.find(Path);
	if (It == SkeletalMeshes.end())
	{
		const FString NormalizedPath = FPaths::Normalize(Path);
		It = SkeletalMeshes.find(NormalizedPath);
	}

    if (It == SkeletalMeshes.end())
    {
        return nullptr;
    }

    return It->second;
}

TArray<FString> FResourceManager::GetSkeletalMeshPaths() const
{
    return SkeletalMeshFilePaths;
}

USkeleton* FResourceManager::LoadSkeleton(const FString& Path)
{
	const FString SkeletonPath = FPaths::Normalize(Path);
	if (USkeleton* FoundSkeleton = FindSkeleton(SkeletonPath))
	{
		return FoundSkeleton;
	}

	if (!IsStaticMeshAssetPath(SkeletonPath))
	{
		UE_LOG("[SkeletonLoad] Runtime skeleton path must be .asset: %s", SkeletonPath.c_str());
		return nullptr;
	}

	FSkeleton* LoadedSkeletonData = new FSkeleton();
	if (!BinarySerializer.LoadSkeleton(SkeletonPath, *LoadedSkeletonData))
	{
		delete LoadedSkeletonData;
		UE_LOG("[SkeletonLoad] Failed to load asset: %s", SkeletonPath.c_str());
		return nullptr;
	}

	LoadedSkeletonData->PathFileName = SkeletonPath;

	USkeleton* LoadedSkeleton = UObjectManager::Get().CreateObject<USkeleton>();
	LoadedSkeleton->SetSkeletonData(LoadedSkeletonData);
	Skeletons.insert({ SkeletonPath, LoadedSkeleton });

	UE_LOG("[SkeletonLoad] Loaded | Path=%s | Bones=%d",
		SkeletonPath.c_str(),
		static_cast<int32>(LoadedSkeletonData->Bones.size()));

	return LoadedSkeleton;
}

USkeleton* FResourceManager::FindSkeleton(const FString& Path) const
{
	auto It = Skeletons.find(Path);
	if (It == Skeletons.end())
	{
		const FString NormalizedPath = FPaths::Normalize(Path);
		It = Skeletons.find(NormalizedPath);
	}

	return It != Skeletons.end() ? It->second : nullptr;
}

TArray<FString> FResourceManager::GetSkeletonPaths() const
{
	return SkeletonFilePaths;
}

const TArray<FString>& FResourceManager::GetTextureFilePath() const
{
	return TextureFilePaths;
}

ID3D11SamplerState* FResourceManager::GetOrCreateSamplerState(ESamplerType Type, ID3D11Device* Device)
{
	if (Device == nullptr)
	{
		Device = CachedDevice.Get();
	}

	auto It = SamplerStates.find(Type);
	if (It != SamplerStates.end())
	{
		return It->second.Get();
	}

	D3D11_SAMPLER_DESC Desc = {};
	Desc.AddressU = D3D11_TEXTURE_ADDRESS_WRAP;
	Desc.AddressV = D3D11_TEXTURE_ADDRESS_WRAP;
	Desc.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
	Desc.MinLOD = 0;
	Desc.MaxLOD = D3D11_FLOAT32_MAX;
	switch (Type)
	{
	case ESamplerType::EST_Point:
		Desc.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
		break;
	case ESamplerType::EST_Linear:
		Desc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
		break;
	case ESamplerType::EST_Anisotropic:
		Desc.Filter = D3D11_FILTER_ANISOTROPIC;
		Desc.MaxAnisotropy = 16;
		break;
	default:
		Desc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
		break;
	}

	TComPtr<ID3D11SamplerState> SamplerState;
	HRESULT hr = CachedDevice->CreateSamplerState(&Desc, &SamplerState);
	if (FAILED(hr))
	{
		UE_LOG("Failed to create sampler state");
		return nullptr;
	}

	SamplerStates[Type] = SamplerState;
	return SamplerState.Get();
}

ID3D11DepthStencilState* FResourceManager::GetOrCreateDepthStencilState(EDepthStencilType Type, ID3D11Device* Device)
{
	if (Device == nullptr)
	{
		Device = CachedDevice.Get();
	}
	auto It = DepthStencilStates.find(Type);
	if (It != DepthStencilStates.end())
	{
		return It->second.Get();
	}

	D3D11_DEPTH_STENCIL_DESC Desc = {};
	switch (Type)
	{
	case EDepthStencilType::Default:
		Desc.DepthEnable = TRUE;
		Desc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
		Desc.DepthFunc = D3D11_COMPARISON_LESS;
		Desc.StencilEnable = FALSE;
		break;
	case EDepthStencilType::DepthReadOnly:
		Desc.DepthEnable = TRUE;
		Desc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
		Desc.DepthFunc = D3D11_COMPARISON_LESS_EQUAL;
		Desc.StencilEnable = FALSE;
		break;
	case EDepthStencilType::DepthAlways:
		Desc.DepthEnable = TRUE;
		Desc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
		Desc.DepthFunc = D3D11_COMPARISON_ALWAYS;
		Desc.StencilEnable = FALSE;
		break;
	case EDepthStencilType::StencilWrite:
		Desc.DepthEnable = TRUE;
		Desc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
		Desc.DepthFunc = D3D11_COMPARISON_ALWAYS;
		Desc.StencilEnable = TRUE;
		Desc.StencilWriteMask = 0xFF;
		Desc.StencilWriteMask = 0xFF;
		Desc.FrontFace.StencilFunc = D3D11_COMPARISON_ALWAYS;
		Desc.FrontFace.StencilPassOp = D3D11_STENCIL_OP_REPLACE;
		Desc.FrontFace.StencilFailOp = D3D11_STENCIL_OP_KEEP;
		Desc.FrontFace.StencilDepthFailOp = D3D11_STENCIL_OP_KEEP;
		Desc.BackFace = Desc.FrontFace;
		break;
	case EDepthStencilType::GizmoInside:
		Desc.DepthEnable = TRUE;
		Desc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
		Desc.DepthFunc = D3D11_COMPARISON_ALWAYS;
		Desc.StencilEnable = TRUE;
		Desc.StencilReadMask = 0xFF;
		Desc.StencilWriteMask = 0x00;
		Desc.FrontFace.StencilFunc = D3D11_COMPARISON_EQUAL;
		Desc.FrontFace.StencilPassOp = D3D11_STENCIL_OP_KEEP;
		Desc.FrontFace.StencilFailOp = D3D11_STENCIL_OP_KEEP;
		Desc.FrontFace.StencilDepthFailOp = D3D11_STENCIL_OP_KEEP;
		Desc.BackFace = Desc.FrontFace;
		break;
	case EDepthStencilType::GizmoOutside:
		Desc.DepthEnable = TRUE;
		Desc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
		Desc.DepthFunc = D3D11_COMPARISON_ALWAYS;
		Desc.StencilEnable = TRUE;
		Desc.StencilReadMask = 0xFF;
		Desc.StencilWriteMask = 0x00;
		Desc.FrontFace.StencilFunc = D3D11_COMPARISON_NOT_EQUAL;
		Desc.FrontFace.StencilPassOp = D3D11_STENCIL_OP_KEEP;
		Desc.FrontFace.StencilFailOp = D3D11_STENCIL_OP_KEEP;
		Desc.FrontFace.StencilDepthFailOp = D3D11_STENCIL_OP_KEEP;
		Desc.BackFace = Desc.FrontFace;
		break;
	}

	TComPtr<ID3D11DepthStencilState> DepthStencilState;
	HRESULT hr = CachedDevice->CreateDepthStencilState(&Desc, &DepthStencilState);
	if (FAILED(hr))
	{
		UE_LOG("Failed to create depth stencil state");
		return nullptr;
	}

	DepthStencilStates[Type] = DepthStencilState;
	return DepthStencilState.Get();
}

ID3D11BlendState* FResourceManager::GetOrCreateBlendState(EBlendType Type, ID3D11Device* Device)
{
	if (Device == nullptr)
	{
		Device = CachedDevice.Get();
	}
	auto It = BlendStates.find(Type);
	if (It != BlendStates.end())
	{
		return It->second.Get();
	}

	D3D11_BLEND_DESC Desc = {};
	switch (Type)
	{
	case EBlendType::Opaque:
		Desc.RenderTarget[0].BlendEnable = FALSE;
		Desc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
		break;
	case EBlendType::AlphaBlend:
		Desc.RenderTarget[0].BlendEnable = TRUE;
		Desc.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
		Desc.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
		Desc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
		Desc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
		Desc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ZERO;
		Desc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
		Desc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
		break;
	case EBlendType::NoColor:
		Desc.RenderTarget[0].BlendEnable = FALSE;
		Desc.RenderTarget[0].RenderTargetWriteMask = 0;
		break;
	}

	TComPtr<ID3D11BlendState> BlendState;
	HRESULT hr = CachedDevice->CreateBlendState(&Desc, &BlendState);
	if (FAILED(hr))
	{
		UE_LOG("Failed to create blend state");
		return nullptr;
	}

	BlendStates[Type] = BlendState;
	return BlendState.Get();
}

ID3D11RasterizerState* FResourceManager::GetOrCreateRasterizerState(ERasterizerType Type, ID3D11Device* Device)
{
	if (Device == nullptr)
	{
		Device = CachedDevice.Get();
	}
	auto It = RasterizerStates.find(Type);
	if (It != RasterizerStates.end())
	{
		return It->second.Get();
	}

	D3D11_RASTERIZER_DESC Desc = {};
	switch (Type)
	{
	case ERasterizerType::SolidBackCull:
		Desc.FillMode = D3D11_FILL_SOLID;
		Desc.CullMode = D3D11_CULL_BACK;
		break;
	case ERasterizerType::SolidFrontCull:
		Desc.FillMode = D3D11_FILL_SOLID;
		Desc.CullMode = D3D11_CULL_FRONT;
		break;
	case ERasterizerType::SolidNoCull:
		Desc.FillMode = D3D11_FILL_SOLID;
		Desc.CullMode = D3D11_CULL_NONE;
		break;
	case ERasterizerType::WireFrame:
		Desc.FillMode = D3D11_FILL_WIREFRAME;
		Desc.CullMode = D3D11_CULL_BACK;
		break;
	}

	TComPtr<ID3D11RasterizerState> RasterizerState;
	HRESULT hr = CachedDevice->CreateRasterizerState(&Desc, &RasterizerState);
	if (FAILED(hr))
	{
		UE_LOG("Failed to create rasterizer state");
		return nullptr;
	}
	RasterizerStates[Type] = RasterizerState;
	return RasterizerState.Get();
}

// TODO: 변경된 구조에 맞춰서 수정하기
size_t FResourceManager::GetMaterialMemorySize() const
{
	size_t TotalSize = 0;

	TotalSize += Materials.size() * sizeof(UMaterial);

	for (const auto& Pair : Materials)
	{
		const FMaterial& Mat = Pair.second->MaterialData;
		TotalSize += Mat.Name.capacity();
		TotalSize += Mat.DiffuseTexPath.capacity();
		TotalSize += Mat.SpecularTexPath.capacity();
		TotalSize += Mat.NormalTexPath.capacity();
		TotalSize += Mat.BumpTexPath.capacity();
	}

	return TotalSize;
}
