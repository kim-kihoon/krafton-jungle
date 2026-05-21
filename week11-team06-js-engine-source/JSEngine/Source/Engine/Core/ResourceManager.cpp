#include "Core/ResourceManager.h"

#include "Core/Paths.h"
#include "Core/AnimationSequenceLoadService.h"
#include "Core/AssetPathPolicy.h"
#include "Core/ExplicitFbxImportService.h"
#include "Core/FbxMaterialLoadService.h"
#include "Core/ImportedMaterialPolicy.h"
#include "Core/MaterialLoadService.h"
#include "Core/MaterialSerializationService.h"
#include "Core/ResourceMemoryReporter.h"
#include "Core/SkeletonLoadService.h"
#include "Core/SkeletalMeshLoadService.h"
#include "Core/StaticMeshLoadService.h"
#include "Animation/AnimStateMachineAsset.h"
#include "Animation/AnimStateMachineSerializer.h"

#include <algorithm>
#include <filesystem>
#include <chrono>
#include <cwctype>
#include "Asset/FileUtils.h"

#include "DDSTextureLoader.h"
#include "WICTextureLoader.h"
#include "Core/Logging/Log.h"

#if WITH_EDITOR
#include "Settings/EditorSettings.h"
#endif

#include "Asset/BinarySerializer.h"
#include "Asset/StaticMeshTypes.h"
#include "Asset/StaticMeshSimplifier.h"
#include "Render/Scene/RenderCommand.h"

namespace
{
	bool ShouldBuildStaticMeshLODs()
	{
#if WITH_EDITOR
		return FEditorSettings::Get().ShowFlags.bEnableLOD;
#else
		return true;
#endif
	}
}

#pragma region __BINARY__

namespace fs = std::filesystem;

namespace
{
	struct FCacheDeleteRule
	{
		fs::path Root;
		const wchar_t* Extensions[2];
		size_t ExtensionCount;
	};

	bool HasExtension(const std::wstring& Extension, const FCacheDeleteRule& Rule)
	{
		for (size_t Index = 0; Index < Rule.ExtensionCount; ++Index)
		{
			if (Extension == Rule.Extensions[Index])
			{
				return true;
			}
		}
		return false;
	}

	void DeleteEmptyDirectoriesUnder(const fs::path& Root)
	{
		if (!fs::exists(Root) || !fs::is_directory(Root))
		{
			return;
		}

		TArray<fs::path> Directories;
		std::error_code Ec;
		for (const fs::directory_entry& Entry : fs::recursive_directory_iterator(Root, fs::directory_options::skip_permission_denied, Ec))
		{
			if (!Ec && Entry.is_directory(Ec))
			{
				Directories.push_back(Entry.path());
			}
		}

		std::sort(
			Directories.begin(),
			Directories.end(),
			[](const fs::path& A, const fs::path& B)
			{
				return A.native().size() > B.native().size();
			});

		for (const fs::path& Directory : Directories)
		{
			Ec.clear();
			if (fs::is_empty(Directory, Ec) && !Ec)
			{
				fs::remove(Directory, Ec);
			}
		}
	}
}

uint64 FResourceManager::GetFileWriteTimeTicks(const FString& Path) const
{
	const FString NormalizedPath = FPaths::Normalize(Path);
	fs::path FilePath(FPaths::ToAbsolute(FPaths::ToWide(NormalizedPath)));
	if (!fs::exists(FilePath))
	{
		return 0;
	}

	auto WriteTime = fs::last_write_time(FilePath);
	auto Duration = WriteTime.time_since_epoch();

	return static_cast<uint64>(
		std::chrono::duration_cast<std::chrono::seconds>(Duration).count());
}

bool FResourceManager::IsStaticMeshBinaryValid(const FString& SourcePath, const FString& BinaryPath) const
{
	FStaticMeshBinaryHeader Header;
	const FString NormalizedBinaryPath = FPaths::Normalize(BinaryPath);
	if (!BinarySerializer.ReadStaticMeshHeader(NormalizedBinaryPath, Header))
	{
		return false;
	}

	const uint64 SourceWriteTime = GetFileWriteTimeTicks(FPaths::Normalize(SourcePath));
	if (SourceWriteTime == 0)
	{
		return false;
	}

	return Header.SourceFileWriteTime == SourceWriteTime;
}

bool FResourceManager::IsSkeletalMeshBinaryValid(const FString& SourcePath, const FString& BinaryPath) const
{
	FSkeletalMeshBinaryHeader Header;
	const FString NormalizedBinaryPath = FPaths::Normalize(BinaryPath);
	if (!BinarySerializer.ReadSkeletalMeshHeader(NormalizedBinaryPath, Header))
	{
		return false;
	}

	const uint64 SourceWriteTime = GetFileWriteTimeTicks(FPaths::Normalize(SourcePath));
	if (SourceWriteTime == 0)
	{
		return false;
	}

	return Header.SourceFileWriteTime == SourceWriteTime;
}

bool FResourceManager::IsAnimationSequenceBinaryValid(const FString& SourcePath, const FString& BinaryPath) const
{
	FAnimationSequenceBinaryHeader Header;
	const FString NormalizedBinaryPath = FPaths::Normalize(BinaryPath);
	if (!AnimationSequenceSerializer.ReadAnimationSequenceHeader(NormalizedBinaryPath, Header))
	{
		return false;
	}

	const uint64 SourceWriteTime = GetFileWriteTimeTicks(FPaths::Normalize(SourcePath));
	if (SourceWriteTime == 0)
	{
		return false;
	}

	return Header.SourceFileWriteTime == SourceWriteTime;
}

void FResourceManager::PreloadStaticMeshes()
{
	for (const auto& [Key, Resource] : StaticMeshCache.GetRegistry())
	{
		if (!Resource.bPreload)
		{
			continue;
		}

		if (LoadStaticMesh(Resource.Path) == nullptr)
		{
			UE_LOG_WARNING("Failed to load static mesh from Resource.ini: %s", Resource.Path.c_str());
		}
	}
}

UStaticMesh* FResourceManager::CreateStaticMeshFromLoadedData(FStaticMesh* LoadedMeshData, const FString& LogPath, bool bLogLodTiming, bool bLogLodSkipped) const
{
	UStaticMesh* LoadedMesh = UObjectManager::Get().CreateObject<UStaticMesh>();
	LoadedMesh->SetMeshData(LoadedMeshData);

	if (ShouldBuildStaticMeshLODs())
	{
		if (bLogLodTiming)
		{
			const auto LodStart = std::chrono::steady_clock::now();
			FStaticMeshSimplifier::BuildLODs(LoadedMesh);
			const auto LodEnd = std::chrono::steady_clock::now();
			double LodSec = std::chrono::duration<double>(LodEnd - LodStart).count();
			UE_LOG("[StaticMeshLoad] Generated %d LODs for %s in %.3f sec",
			       LoadedMesh->GetValidLODCount(), LogPath.c_str(), LodSec);
		}
		else
		{
			FStaticMeshSimplifier::BuildLODs(LoadedMesh);
		}
	}
	else if (bLogLodSkipped)
	{
		UE_LOG_WARNING("[StaticMeshLoad] LOD generation skipped for %s (Enable LOD is off)", LogPath.c_str());
	}

	return LoadedMesh;
}

#pragma endregion


void FResourceManager::ClearDiscoveredResourceLists(bool bClearAtlasCache)
{
	ObjFilePaths.clear();
	FontFilePaths.clear();
	TextureFilePaths.clear();
	MaterialFilePaths.clear();
	ParticleFilePaths.clear();
	CurveFilePaths.clear();
	AnimationSequenceFilePaths.clear();
	AnimStateMachineFilePaths.clear();
	SkeletonFilePaths.clear();
	SkeletalMeshFilePaths.clear();
	StaticMeshCache.ClearRegistry();

	if (bClearAtlasCache)
	{
		AtlasCache.Clear();
	}
}

void FResourceManager::RegisterDiscoveredAssetFile(const std::filesystem::path& FilePath, const std::filesystem::path& ProjectRootPath)
{
	std::wstring Extension = FilePath.extension().wstring();
	std::transform(Extension.begin(), Extension.end(), Extension.begin(), ::towlower);

	if (Extension == L".meta" || Extension == L".bin")
	{
		return;
	}

	const FString RelativePath = FPaths::Normalize(FPaths::ToString(std::filesystem::relative(FilePath, ProjectRootPath)));

	if (FAssetPathPolicy::IsCurveAssetPath(FPaths::ToUtf8(FilePath.generic_wstring())))
	{
		CurveFilePaths.push_back(RelativePath);
	}
	else if (FAssetPathPolicy::IsAnimationSequenceAssetPath(FPaths::ToUtf8(FilePath.generic_wstring())))
	{
		AnimationSequenceFilePaths.push_back(RelativePath);
	}
	else if (FAssetPathPolicy::IsAnimStateMachineAssetPath(FPaths::ToUtf8(FilePath.generic_wstring())))
	{
		AnimStateMachineFilePaths.push_back(RelativePath);
	}
	else if (Extension == L".obj")
	{
		ObjFilePaths.push_back(RelativePath);

		FStaticMeshResource Resource;
		Resource.Name = RelativePath;
		Resource.Path = RelativePath;
		Resource.bPreload = false;
		Resource.bNormalizeToUnitCube = false;
		StaticMeshCache.RegisterResource(Resource);
	}
	else if (Extension == L".mtl" || Extension == L".mat" || Extension == L".matinst")
	{
		MaterialFilePaths.push_back(RelativePath);
	}
	else if (Extension == L".png" || Extension == L".dds" || Extension == L".jpg" || Extension == L".jpeg")
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
		}
	}
}

void FResourceManager::InitializeDefaultWhiteTexture(ID3D11Device* Device)
{
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
	D3D11_SUBRESOURCE_DATA InitData = {&WhitePixel, 4, 0};

	if (!TextureCache.Contains("DefaultWhite"))  {
		Device->CreateTexture2D(&Desc, &InitData, DefaultWhiteTexture.ReleaseAndGetAddressOf());
		if (DefaultWhiteTexture)
		{
			UTexture* DefaultTexture = UObjectManager::Get().CreateObject<UTexture>();
			Device->CreateShaderResourceView(DefaultWhiteTexture.Get(), nullptr, DefaultTexture->GetAddressOfSRV());
			TextureCache.Register("DefaultWhite", DefaultTexture);
		}
	}
}

void FResourceManager::InitializeDefaultMaterial(ID3D11Device* Device)
{
	UMaterial* DefaultMat = GetOrCreateMaterial("DefaultWhite", EMaterialShaderType::SurfaceLit);
	DefaultMat->MaterialParams["AmbientColor"] = FMaterialParamValue(DefaultMat->MaterialData.AmbientColor);
	DefaultMat->MaterialParams["DiffuseColor"] = FMaterialParamValue(DefaultMat->MaterialData.DiffuseColor);
	DefaultMat->MaterialParams["SpecularColor"] = FMaterialParamValue(DefaultMat->MaterialData.SpecularColor);
	DefaultMat->MaterialParams["EmissiveColor"] = FMaterialParamValue(DefaultMat->MaterialData.EmissiveColor);
	DefaultMat->MaterialParams["Shininess"] = FMaterialParamValue(DefaultMat->MaterialData.Shininess);
	DefaultMat->MaterialParams["Opacity"] = FMaterialParamValue(DefaultMat->MaterialData.Opacity);

	UTexture* DefaultWhite = GetTexture("DefaultWhite");

	if (DefaultMat->MaterialData.bHasDiffuseTexture)
		DefaultMat->MaterialParams["DiffuseMap"] = FMaterialParamValue(LoadTexture(DefaultMat->MaterialData.DiffuseTexPath, Device));
	else
		DefaultMat->MaterialParams["DiffuseMap"] = FMaterialParamValue(DefaultWhite);

	if (DefaultMat->MaterialData.bHasAmbientTexture)
		DefaultMat->MaterialParams["AmbientMap"] = FMaterialParamValue(LoadTexture(DefaultMat->MaterialData.AmbientTexPath, Device));
	else
		DefaultMat->MaterialParams["AmbientMap"] = FMaterialParamValue(DefaultWhite);

	if (DefaultMat->MaterialData.bHasSpecularTexture)
		DefaultMat->MaterialParams["SpecularMap"] = FMaterialParamValue(LoadTexture(DefaultMat->MaterialData.SpecularTexPath, Device));
	else
		DefaultMat->MaterialParams["SpecularMap"] = FMaterialParamValue(DefaultWhite);

	if (DefaultMat->MaterialData.bHasEmissiveTexture)
		DefaultMat->MaterialParams["EmissiveMap"] = FMaterialParamValue(LoadTexture(DefaultMat->MaterialData.EmissiveTexPath, Device));
	else
		DefaultMat->MaterialParams["EmissiveMap"] = FMaterialParamValue(DefaultWhite);

	if (DefaultMat->MaterialData.bHasBumpTexture)
		DefaultMat->MaterialParams["BumpMap"] = FMaterialParamValue(LoadTexture(DefaultMat->MaterialData.BumpTexPath, Device));
	else
		DefaultMat->MaterialParams["BumpMap"] = FMaterialParamValue(DefaultWhite);

	DefaultMat->MaterialParams["bHasDiffuseMap"] = FMaterialParamValue(DefaultMat->MaterialData.bHasDiffuseTexture);
	DefaultMat->MaterialParams["bHasSpecularMap"] = FMaterialParamValue(DefaultMat->MaterialData.bHasSpecularTexture);
	DefaultMat->MaterialParams["bHasAmbientMap"] = FMaterialParamValue(DefaultMat->MaterialData.bHasAmbientTexture);
	DefaultMat->MaterialParams["bHasEmissiveMap"] = FMaterialParamValue(DefaultMat->MaterialData.bHasEmissiveTexture);
	DefaultMat->MaterialParams["bHasBumpMap"] = FMaterialParamValue(DefaultMat->MaterialData.bHasBumpTexture);
	DefaultMat->MaterialParams["ScrollUV"] = FMaterialParamValue(FVector2(0.0f, 0.0f));
}

void FResourceManager::InitializeOutlineMaterial()
{
	UMaterial* OutlineMat = GetOrCreateMaterial("OutlineMaterial", EMaterialShaderType::EditorOutline);
	OutlineMat->SetParam("OutlineColor", FMaterialParamValue(FVector4(1.0f, 0.5f, 0.0f, 1.0f)));
	OutlineMat->SetParam("OutlineThicknessPixels", FMaterialParamValue(5.0f));
	OutlineMat->SetParam("OutlineViewportSize", FMaterialParamValue(FVector2(800.0f, 600.0f)));
    OutlineMat->SetParam("OutlineViewportOrigin", FMaterialParamValue(FVector2(0.0f, 0.0f)));
}

//	RootPath ??륁맄?????�뮉 筌뤴뫀�??????�쎛???Asset??????뤿연 ?λ?�由?????????�밸�????λ??
void FResourceManager::LoadFromAssetDirectory(const FString& Path)
{
	//	?λ?�由??
	ClearDiscoveredResourceLists(false);

	InitializeDefaultResources(CachedDevice.Get());

	namespace fs = std::filesystem;
	
	const fs::path RootPath = fs::path(FPaths::RootDir()) / FPaths::ToWide(Path);
	
	const fs::path ProjectRootPath = fs::path(FPaths::RootDir());

	if (!fs::exists(RootPath) || !fs::is_directory(RootPath))
	{
		UE_LOG_ERROR("[ResourceManager] Fatal Error : Root Directory Error");
		return;
	}

	for (const auto& Entry : fs::recursive_directory_iterator(RootPath))
	{
		if (!Entry.is_regular_file())
		{
			continue;
		}

		RegisterDiscoveredAssetFile(Entry.path(), ProjectRootPath);
	}

	PreloadStaticMeshes();

	if (LoadGPUResources(CachedDevice.Get()))
	{
		UE_LOG("Complete Load Resources!");
	}
	else
	{
		UE_LOG_ERROR("Failed to Load Resources...");
	}
}

void FResourceManager::RefreshFromAssetDirectory(const FString& Path)
{
	namespace fs = std::filesystem;

	ClearDiscoveredResourceLists(true);

	const fs::path RootPath = fs::path(FPaths::RootDir()) / FPaths::ToWide(Path);
	const fs::path ProjectRootPath = fs::path(FPaths::RootDir());

	if (!fs::exists(RootPath) || !fs::is_directory(RootPath))
	{
		UE_LOG_ERROR("[ResourceManager] Refresh Failed : Root Directory Error");
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

			RegisterDiscoveredAssetFile(Entry.path(), ProjectRootPath);
		}
	}
	catch (const std::exception& Ex)
	{
		UE_LOG_ERROR("[ResourceManager] Refresh Exception: %s", Ex.what());
	}

	if (CachedDevice && !LoadGPUResources(CachedDevice.Get()))
	{
		UE_LOG_ERROR("[ResourceManager] Refresh Failed : GPU Resource Reload Error");
	}

	UE_LOG("[ResourceManager] Asset Refresh Complete");
}

void FResourceManager::DeleteAllCacheFiles()
{
	const fs::path RootPath = fs::path(FPaths::RootDir());
	const fs::path AssetRoot = RootPath / "Asset";
	const FCacheDeleteRule Rules[] =
	{
		{ RootPath / "Asset" / "Mesh" / "Bin", { L".bin", nullptr }, 1 },
		{ RootPath / "Asset" / "SkeletalMesh" / "Bin", { L".bin", nullptr }, 1 },
		{ RootPath / "Asset" / "Animation" / "Bin", { L".bin", nullptr }, 1 },
		{ RootPath / "Asset" / "Material" / "Auto", { L".mat", L".matinst" }, 2 },
	};

	uint32 DeletedFileCount = 0;
	for (const FCacheDeleteRule& Rule : Rules)
	{
		if (!fs::exists(Rule.Root) || !fs::is_directory(Rule.Root))
		{
			continue;
		}

		std::error_code Ec;
		for (const fs::directory_entry& Entry : fs::recursive_directory_iterator(Rule.Root, fs::directory_options::skip_permission_denied, Ec))
		{
			if (Ec || !Entry.is_regular_file(Ec))
			{
				continue;
			}

			const fs::path& FilePath = Entry.path();
			const std::wstring Extension = FilePath.extension().wstring();
			if (!HasExtension(Extension, Rule))
			{
				continue;
			}

			Ec.clear();
			if (fs::remove(FilePath, Ec) && !Ec)
			{
				++DeletedFileCount;
			}
		}

		DeleteEmptyDirectoriesUnder(Rule.Root);
	}

	if (fs::exists(AssetRoot) && fs::is_directory(AssetRoot))
	{
		FImportedFbxAssetDiscovery ImportedDiscovery;
		const TArray<FImportedFbxAssetRecord> ImportedRecords =
			ImportedDiscovery.DiscoverInDirectory(FPaths::ToUtf8(AssetRoot.generic_wstring()));

		for (const FImportedFbxAssetRecord& Record : ImportedRecords)
		{
			if (Record.Type == EImportedFbxAssetType::Unknown || Record.AssetPath.empty())
			{
				continue;
			}

			const fs::path ImportedBinaryPath = fs::path(FPaths::ToWide(Record.AssetPath));
			const fs::path AbsoluteImportedBinaryPath = ImportedBinaryPath.is_absolute()
				? ImportedBinaryPath.lexically_normal()
				: (RootPath / ImportedBinaryPath).lexically_normal();

			std::error_code Ec;
			if (fs::is_regular_file(AbsoluteImportedBinaryPath, Ec) && fs::remove(AbsoluteImportedBinaryPath, Ec) && !Ec)
			{
				++DeletedFileCount;
			}
		}
	}

	UE_LOG("[ResourceManager] Removed %u cache files from disk", DeletedFileCount);
}

FTextureAssetMeta FResourceManager::LoadOrCreateTextureMeta(const std::filesystem::path& FilePath) const
{
	return FTextureAssetMetaService::LoadOrCreate(FilePath);
}

bool FResourceManager::LoadGPUResources(ID3D11Device* Device)
{
	return AtlasCache.LoadGPUResources(Device);
}

void FResourceManager::InitializeDefaultResources(ID3D11Device* Device)
{
	if (!Device) return;

	InitializeDefaultWhiteTexture(Device);
	InitializeDefaultMaterial(Device);
	InitializeOutlineMaterial();
}

void FResourceManager::ReleaseGPUResources()
{
	TextureCache.Release();

	MaterialCache.Release();

	ShaderCache.Release();

	AtlasCache.Release();

	StaticMeshCache.Release();

	CurveCache.Release();

	RenderStateCache.Release();

	TSet<USkeletalMesh*> DestroyedSkeletalMeshes;
	for (auto& [Path, Mesh] : SkeletalMeshMap)
	{
		if (Mesh && DestroyedSkeletalMeshes.insert(Mesh).second)
		{
			UObjectManager::Get().DestroyObject(Mesh);
		}
	}
	SkeletalMeshMap.clear();

	TSet<UAnimationSequence*> DestroyedAnimationSequences;
	for (auto& [Path, Sequence] : AnimationSequenceMap)
	{
		if (Sequence && DestroyedAnimationSequences.insert(Sequence).second)
		{
			UObjectManager::Get().DestroyObject(Sequence);
		}
	}
	AnimationSequenceMap.clear();

	TSet<UAnimStateMachineAsset*> DestroyedStateMachines;
	for (auto& [Path, Asset] : AnimStateMachineMap)
	{
		if (Asset && DestroyedStateMachines.insert(Asset).second)
		{
			UObjectManager::Get().DestroyObject(Asset);
		}
	}
	AnimStateMachineMap.clear();

	TSet<USkeletonAsset*> DestroyedSkeletons;
	for (auto& [Path, Skeleton] : SkeletonMap)
	{
		if (Skeleton && DestroyedSkeletons.insert(Skeleton).second)
		{
			UObjectManager::Get().DestroyObject(Skeleton);
		}
	}
	SkeletonMap.clear();

	DefaultWhiteTexture.Reset();
	CachedDevice.Reset();
}

FVertexShader* FResourceManager::GetOrCreateVertexShader(
	const FShaderStageKey& Key,
	const D3D_SHADER_MACRO* Defines,
	const FVertexLayoutDesc* VertexLayout)
{
	return ShaderCache.GetOrCreateVertexShader(Key, Defines, CachedDevice.Get(), VertexLayout);
}

FPixelShader* FResourceManager::GetOrCreatePixelShader(const FShaderStageKey& Key, const D3D_SHADER_MACRO* Defines)
{
	return ShaderCache.GetOrCreatePixelShader(Key, Defines, CachedDevice.Get());
}

FShaderProgram* FResourceManager::GetOrCreateShaderProgram(
	const FShaderStageKey& VSKey,
	const FShaderStageKey& PSKey,
	const D3D_SHADER_MACRO* VSDefines,
	const D3D_SHADER_MACRO* PSDefines,
	const FVertexLayoutDesc* VertexLayout)
{
	return ShaderCache.GetOrCreateProgram(VSKey, PSKey, VSDefines, PSDefines, CachedDevice.Get(), VertexLayout);
}

bool FResourceManager::LoadComputeShader(const FString& FilePath, const FString& EntryPoint,
                                         const D3D_SHADER_MACRO* Defines, const FString& Key)
{
	return ShaderCache.LoadComputeShader(FilePath, EntryPoint, Defines, Key, CachedDevice.Get());
}

void FResourceManager::InvalidateShaderFile(const FString& FilePath)
{
	ShaderCache.InvalidateShaderFile(FilePath);
}

FComputeShader* FResourceManager::GetComputeShader(const FString& Key) const
{
	return ShaderCache.GetComputeShader(Key);
}

TArray<FString> FResourceManager::GetMaterialNames() const
{
	return MaterialCache.GetMaterialNames();
}

TArray<FString> FResourceManager::GetMaterialInterfaceNames() const
{
	return MaterialCache.GetMaterialInterfaceNames(MaterialFilePaths);
}

UMaterial* FResourceManager::GetMaterial(const FString& MaterialName) const
{
	return MaterialCache.GetMaterial(MaterialName);
}

UMaterial* FResourceManager::GetOrCreateMaterial(const FString& Path, EMaterialShaderType ShaderType)
{
	UMaterial* Material = GetMaterial(Path);
	if (Material)
	{
		return Material;
	}

	Material = UObjectManager::Get().CreateObject<UMaterial>();
	Material->Name = Path;
	Material->FilePath = Path;

	Material->SetShaderType(ShaderType);

	MaterialCache.RegisterMaterial(Path, Material);

	return Material;
}

UMaterial* FResourceManager::GetOrCreateMaterial(const FString& Name, const FString& Path, EMaterialShaderType ShaderType)
{
	UMaterial* Material = GetMaterial(Name);
	if (Material)
	{
		return Material;
	}

	Material = UObjectManager::Get().CreateObject<UMaterial>();
	Material->Name = Name;
	Material->FilePath = Path;

	Material->SetShaderType(ShaderType);

	MaterialCache.RegisterMaterial(Name, Material);

	return Material;
}

bool FResourceManager::LoadMaterial(const FString& MtlFilePath, EMaterialShaderType ShaderType, ID3D11Device* Device)
{
	return FMaterialLoadService(*this).Load(MtlFilePath, ShaderType, Device);
}

bool FResourceManager::ImportMaterialFromFbx(const FString& SourceFbxPath, EMaterialShaderType ShaderType, ID3D11Device* Device)
{
	if (!Device)
	{
		Device = CachedDevice.Get();
	}

	return FFbxMaterialLoadService(*this).ImportFromFbx(SourceFbxPath, ShaderType, Device);
}

void FResourceManager::RegisterObjMaterialSlotAliases(const FString& ObjPath, const FString& MtlPath)
{
	const FString NormalizedObjPath = FPaths::Normalize(ObjPath);
	const FString NormalizedMtlPath = FPaths::Normalize(MtlPath);
	const TArray<FString> SlotNames = FImportedMaterialPolicy::CollectObjMaterialSlotNames(NormalizedObjPath);

	for (const FString& SlotName : SlotNames)
	{
		const FString* MtlAlias = MaterialCache.FindMaterialSlotAlias(FImportedMaterialPolicy::MakeMaterialSlotAliasKey(NormalizedMtlPath, SlotName));
		if (MtlAlias)
		{
			MaterialCache.SetMaterialSlotAlias(FImportedMaterialPolicy::MakeMaterialSlotAliasKey(NormalizedObjPath, SlotName), *MtlAlias);
		}
	}
}

UMaterial* FResourceManager::GetMaterialForStaticMeshSlot(const FString& SourcePath, const FString& SlotName) const
{
	if (!SourcePath.empty())
	{
		const FString* Alias = MaterialCache.FindMaterialSlotAlias(FImportedMaterialPolicy::MakeMaterialSlotAliasKey(SourcePath, SlotName));
		if (Alias)
		{
			if (UMaterial* Material = GetMaterial(*Alias))
			{
				return Material;
			}
		}
	}

	return GetMaterial(SlotName);
}

void FResourceManager::ResolveStaticMeshMaterialSlots(const FString& SourcePath, FStaticMesh* StaticMesh) const
{
	if (!StaticMesh)
	{
		return;
	}

	for (FStaticMeshMaterialSlot& Slot : StaticMesh->Slots)
	{
		if (!SourcePath.empty())
		{
			const FString* Alias = MaterialCache.FindMaterialSlotAlias(FImportedMaterialPolicy::MakeMaterialSlotAliasKey(SourcePath, Slot.SlotName));
			if (Alias)
			{
				Slot.SlotName = *Alias;
			}
		}

		Slot.Material = GetMaterialForStaticMeshSlot(SourcePath, Slot.SlotName);
		if (Slot.Material == nullptr)
		{
			Slot.Material = GetMaterial("DefaultWhite");
		}
	}
}

void FResourceManager::ResolveSkeletalMeshMaterialSlots(const FString& SourcePath, FSkeletalMesh* SkeletalMesh) const
{
    if (!SkeletalMesh)
    {
        return;
    }

    for (FStaticMeshMaterialSlot& Slot : SkeletalMesh->MaterialSlots)
    {
        if (!SourcePath.empty())
        {
            const FString* Alias = MaterialCache.FindMaterialSlotAlias(FImportedMaterialPolicy::MakeMaterialSlotAliasKey(SourcePath, Slot.SlotName));
            if (Alias)
            {
                Slot.SlotName = *Alias;
            }
        }

        Slot.Material = GetMaterialForStaticMeshSlot(SourcePath, Slot.SlotName);
        if (Slot.Material == nullptr)
        {
            Slot.Material = GetMaterial("DefaultWhite");
        }
    }
}

UMaterialInstance* FResourceManager::CreateMaterialInstance(const FString& Path, UMaterial* Parent)
{
	return MaterialCache.CreateMaterialInstance(Path, Parent);
}

UMaterialInstance* FResourceManager::GetMaterialInstance(const FString& Path) const
{
	return MaterialCache.GetMaterialInstance(Path);
}

UMaterialInterface* FResourceManager::GetMaterialInterface(const FString& Name)
{
	UMaterial* Mat = GetMaterial(Name);
	if (Mat)
	{
		return Mat;
    }
	else if (Mat = GetMaterial(FPaths::Normalize(Name)))
	{
        return Mat;
	}
    else if (UMaterialInstance* MatInst = GetMaterialInstance(Name))
	{
		return MatInst;
    }
	if (UMaterialInstance* MatInst = GetMaterialInstance(FPaths::Normalize(Name)))
	{
		return MatInst;
	}

	const FString NormalizedName = FPaths::Normalize(Name);
	if (FAssetPathPolicy::IsSerializedMaterialAssetPath(NormalizedName) && FAssetPathPolicy::FileExists(NormalizedName))
	{
		if (DeserializeMaterial(NormalizedName))
		{
			if (UMaterial* LoadedMat = GetMaterial(NormalizedName))
			{
				return LoadedMat;
			}
			if (UMaterialInstance* LoadedMatInst = GetMaterialInstance(NormalizedName))
			{
				return LoadedMatInst;
			}
		}
	}

    return nullptr;
}

bool FResourceManager::SerializeMaterial(const FString& MatFilePath, const UMaterial* Material)
{
	return FMaterialSerializationService(*this).SerializeMaterial(MatFilePath, Material);
}

bool FResourceManager::SerializeMaterialInstance(const FString& MatInstFilePath, const UMaterialInstance* MaterialInstance)
{
	return FMaterialSerializationService(*this).SerializeMaterialInstance(MatInstFilePath, MaterialInstance);
}

bool FResourceManager::DeserializeMaterial(const FString& MatFilePath)
{
	return FMaterialSerializationService(*this).DeserializeMaterial(MatFilePath);
}

UTexture* FResourceManager::GetTexture(const FString& Path) const
{
	return TextureCache.Get(Path);
}

UTexture* FResourceManager::LoadTexture(const FString& Path, ID3D11Device* Device)
{
    if (Device == nullptr)
    {
        Device = CachedDevice.Get();
    }

	return TextureCache.Load(Path, Device);
}

// --- Font ---
FFontResource* FResourceManager::FindFont(const FName& FontName)
{
	return AtlasCache.FindFont(FontName);
}

const FFontResource* FResourceManager::FindFont(const FName& FontName) const
{
	return AtlasCache.FindFont(FontName);
}

void FResourceManager::RegisterFont(const FName& FontName, const FString& InPath, uint32 Columns, uint32 Rows)
{
	AtlasCache.RegisterFont(FontName, InPath, Columns, Rows);
}

// --- Particle ---
FParticleResource* FResourceManager::FindParticle(const FName& ParticleName)
{
	return AtlasCache.FindParticle(ParticleName);
}

const FParticleResource* FResourceManager::FindParticle(const FName& ParticleName) const
{
	return AtlasCache.FindParticle(ParticleName);
}

void FResourceManager::RegisterParticle(const FName& ParticleName, const FString& InPath, uint32 Columns, uint32 Rows)
{
	AtlasCache.RegisterParticle(ParticleName, InPath, Columns, Rows);
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
	return FStaticMeshLoadService(*this).Load(Path);
}

UStaticMesh* FResourceManager::ImportStaticMeshFromFbx(const FString& SourceFbxPath, bool bImportMaterials)
{
	return FStaticMeshLoadService(*this).ImportFbxSource(SourceFbxPath, bImportMaterials);
}

UStaticMesh* FResourceManager::FindStaticMesh(const FString& Path) const
{
	const FString NormalizedPath = FPaths::Normalize(Path);
	return StaticMeshCache.Find(NormalizedPath);
}

TArray<FString> FResourceManager::GetStaticMeshPaths() const
{
	return ObjFilePaths;
}

USkeletonAsset* FResourceManager::LoadSkeleton(const FString& Path)
{
	return FSkeletonLoadService(*this).Load(Path);
}

USkeletonAsset* FResourceManager::FindSkeleton(const FString& Path) const
{
	const FString NormalizedPath = FPaths::Normalize(Path);

	auto It = SkeletonMap.find(NormalizedPath);
	if (It != SkeletonMap.end())
	{
		return It->second;
	}

	return nullptr;
}

TArray<FString> FResourceManager::GetSkeletonPaths() const
{
	return SkeletonFilePaths;
}

TArray<USkeletonAsset*> FResourceManager::ImportSkeletonsFromFbx(const FString& SourceFbxPath)
{
	const FString NormalizedSourcePath = FPaths::Normalize(SourceFbxPath);
	TArray<USkeletonAsset*> Result;
	TArray<FSkeleton*> ImportedSkeletons = FbxImporter.LoadSkeletons(NormalizedSourcePath);

	for (FSkeleton* Data : ImportedSkeletons)
	{
		if (!Data)
		{
			continue;
		}

		const FString BinaryPath = FAssetPathPolicy::MakeSiblingSkeletonBinaryPath(NormalizedSourcePath, Data->RootNodeName);
		Data->PathFileName = BinaryPath;

		if (!SkeletonSerializer.SaveSkeleton(BinaryPath, *Data))
		{
			UE_LOG_WARNING("[SkeletonImport] Failed to save skeleton binary | Source=%s | Binary=%s | Root=%s",
			               NormalizedSourcePath.c_str(),
			               BinaryPath.c_str(),
			               Data->RootNodeName.c_str());
			delete Data;
			continue;
		}

		USkeletonAsset* Skeleton = FindSkeleton(BinaryPath);
		if (!Skeleton)
		{
			Skeleton = UObjectManager::Get().CreateObject<USkeletonAsset>();
			SkeletonMap[BinaryPath] = Skeleton;
		}

		Skeleton->SetSkeletonData(Data);

		if (std::find(SkeletonFilePaths.begin(), SkeletonFilePaths.end(), BinaryPath) == SkeletonFilePaths.end())
		{
			SkeletonFilePaths.push_back(BinaryPath);
		}

		Result.push_back(Skeleton);

		UE_LOG("[SkeletonImport] Saved | Source=%s | Binary=%s | Root=%s | Bones=%zu | Sockets=%zu",
		       NormalizedSourcePath.c_str(),
		       BinaryPath.c_str(),
		       Data->RootNodeName.c_str(),
		       Data->Bones.size(),
		       Data->Sockets.size());
	}

	return Result;
}

bool FResourceManager::SaveSkeleton(USkeletonAsset* Skeleton)
{
	if (!Skeleton)
	{
		return false;
	}

	FSkeleton* Data = Skeleton->GetSkeletonData();
	if (!Data || Data->PathFileName.empty())
	{
		return false;
	}

	return SkeletonSerializer.SaveSkeleton(Data->PathFileName, *Data);
}

USkeletalMesh* FResourceManager::LoadSkeletalMesh(const FString& Path)
{
    return FSkeletalMeshLoadService(*this).Load(Path);
}

USkeletalMesh* FResourceManager::LoadSkeletalMesh(const FString& Path, const FString& SkeletonName)
{
	return FSkeletalMeshLoadService(*this).Load(Path, SkeletonName);
}

USkeletalMesh* FResourceManager::ImportSkeletalMeshFromFbx(
	const FString& SourceFbxPath,
	const FString& SkeletonName,
	bool bImportMaterials,
	const TArray<USkeletonAsset*>* ImportedSkeletonsOverride)
{
	return FSkeletalMeshLoadService(*this).ImportFbxSource(
		SourceFbxPath,
		SkeletonName,
		bImportMaterials,
		ImportedSkeletonsOverride);
}

USkeletalMesh* FResourceManager::FindSkeletalMesh(const FString& Path) const
{
    const FString NormalizedPath = FPaths::Normalize(Path);

    auto It = SkeletalMeshMap.find(NormalizedPath);
    if (It != SkeletalMeshMap.end())
    {
        return It->second;
    }

    return nullptr;
}

TArray<FString> FResourceManager::GetSkeletalMeshPaths() const
{
    return SkeletalMeshFilePaths;
}

FFbxMeshContentInfo FResourceManager::InspectFbxMeshContent(const FString& Path)
{
    return FbxImporter.InspectMeshContent(Path);
}

TArray<FImportedFbxAssetRecord> FResourceManager::ImportFbxAssets(const FString& SourceFbxPath)
{
	return FExplicitFbxImportService(*this).Import(SourceFbxPath);
}

TArray<FImportedFbxAssetRecord> FResourceManager::DiscoverImportedFbxAssets(const FString& SourceFbxPath) const
{
	FImportedFbxAssetDiscovery Discovery;
	return Discovery.DiscoverForSourceFbx(SourceFbxPath);
}

bool FResourceManager::SaveSkeletalMesh(USkeletalMesh* Mesh)
{
    if (!Mesh) return false;
    FSkeletalMesh* Data = Mesh->GetMeshData();
    if (!Data) return false;

    const FString FbxPath = Mesh->GetAssetPathFileName();
    if (FbxPath.empty()) return false;

    const FString BinPath = FAssetPathPolicy::MakeWritableSkeletalMeshCacheBinaryPath(FbxPath);
    return BinarySerializer.SaveSkeletalMesh(BinPath, FbxPath, *Data);
}

UCurveFloatAsset* FResourceManager::LoadCurve(const FString& Path)
{
	const FString NormalizedPath = FPaths::Normalize(Path);
	UCurveFloatAsset* Curve = CurveCache.Load(NormalizedPath);
	if (!Curve)
	{
		return nullptr;
	}

	if (std::find(CurveFilePaths.begin(), CurveFilePaths.end(), NormalizedPath) == CurveFilePaths.end())
	{
		CurveFilePaths.push_back(NormalizedPath);
	}

	return Curve;
}

UCurveFloatAsset* FResourceManager::FindCurve(const FString& Path) const
{
	return CurveCache.Find(Path);
}

bool FResourceManager::SaveCurve(const FString& Path, const UCurveFloatAsset* Curve)
{
	const FString NormalizedPath = FPaths::Normalize(Path);
	if (!CurveCache.Save(NormalizedPath, Curve))
	{
		return false;
	}

	if (std::find(CurveFilePaths.begin(), CurveFilePaths.end(), NormalizedPath) == CurveFilePaths.end())
	{
		CurveFilePaths.push_back(NormalizedPath);
	}

	return true;
}

TArray<FString> FResourceManager::GetCurvePaths() const
{
	return CurveFilePaths;
}

UAnimationSequence* FResourceManager::LoadAnimationSequence(const FString& Path)
{
	return FAnimationSequenceLoadService(*this).Load(Path);
}

UAnimationSequence* FResourceManager::ImportAnimationSequencesFromFbx(
	const FString& SourceFbxPath,
	const TArray<USkeletonAsset*>* ImportedSkeletonsOverride)
{
	return FAnimationSequenceLoadService(*this).ImportFbxSource(SourceFbxPath, ImportedSkeletonsOverride);
}

UAnimationSequence* FResourceManager::FindAnimationSequence(const FString& Path) const
{
	const FString NormalizedPath = FPaths::Normalize(Path);
	auto It = AnimationSequenceMap.find(NormalizedPath);
	return It != AnimationSequenceMap.end() ? It->second : nullptr;
}

bool FResourceManager::SaveAnimationSequence(UAnimationSequence* Sequence)
{
	if (!Sequence)
	{
		return false;
	}

	if (!Sequence->GetAssetPath().empty())
	{
		return AnimationSequenceSerializer.SaveAnimationSequenceAsset(Sequence->GetAssetPath(), *Sequence);
	}

	FAnimationSequence* Data = Sequence->GetSequenceData();
	if (!Data || Data->SourcePath.empty())
	{
		return false;
	}

	const FString SequenceToken = Data->Name.empty() ? FString("anim") : Data->Name;
	const FString AssetPath = FAssetPathPolicy::MakeSiblingAnimationSequenceAssetPath(Data->SourcePath, SequenceToken);
	Sequence->SetAssetPath(AssetPath);
	if (Sequence->GetSourceImportPath().empty())
	{
		Sequence->SetSourceImportPath(Data->SourcePath);
	}

	return AnimationSequenceSerializer.SaveAnimationSequenceAsset(AssetPath, *Sequence);
}

TArray<FString> FResourceManager::GetAnimationSequencePaths() const
{
	return AnimationSequenceFilePaths;
}

UAnimStateMachineAsset* FResourceManager::LoadAnimStateMachineAsset(const FString& Path)
{
	const FString NormalizedPath = FPaths::Normalize(Path);
	if (UAnimStateMachineAsset* FoundAsset = FindAnimStateMachineAsset(NormalizedPath))
	{
		return FoundAsset;
	}

	if (!FAssetPathPolicy::IsAnimStateMachineAssetPath(NormalizedPath))
	{
		UE_LOG_WARNING("[AnimSM] Unsupported state machine asset path: %s", NormalizedPath.c_str());
		return nullptr;
	}

	UAnimStateMachineAsset* LoadedAsset = UAnimStateMachineAsset::LoadFromFile(NormalizedPath);
	if (!LoadedAsset)
	{
		return nullptr;
	}

	AnimStateMachineMap[NormalizedPath] = LoadedAsset;
	if (std::find(AnimStateMachineFilePaths.begin(), AnimStateMachineFilePaths.end(), NormalizedPath) == AnimStateMachineFilePaths.end())
	{
		AnimStateMachineFilePaths.push_back(NormalizedPath);
	}

	return LoadedAsset;
}

UAnimStateMachineAsset* FResourceManager::FindAnimStateMachineAsset(const FString& Path) const
{
	const FString NormalizedPath = FPaths::Normalize(Path);
	auto It = AnimStateMachineMap.find(NormalizedPath);
	return It != AnimStateMachineMap.end() ? It->second : nullptr;
}

bool FResourceManager::SaveAnimStateMachineAsset(const FString& Path, const UAnimStateMachineAsset* Asset)
{
	if (!Asset)
	{
		return false;
	}

	const FString NormalizedPath = FPaths::Normalize(Path);
	if (!Asset->SaveToFile(NormalizedPath))
	{
		return false;
	}

	if (std::find(AnimStateMachineFilePaths.begin(), AnimStateMachineFilePaths.end(), NormalizedPath) == AnimStateMachineFilePaths.end())
	{
		AnimStateMachineFilePaths.push_back(NormalizedPath);
	}
	return true;
}

TArray<FString> FResourceManager::GetAnimStateMachineAssetPaths() const
{
	return AnimStateMachineFilePaths;
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

	return RenderStateCache.GetOrCreateSamplerState(Type, Device);
}

ID3D11DepthStencilState* FResourceManager::GetOrCreateDepthStencilState(EDepthStencilType Type, ID3D11Device* Device)
{
	if (Device == nullptr)
	{
		Device = CachedDevice.Get();
	}
	return RenderStateCache.GetOrCreateDepthStencilState(Type, Device);
}

ID3D11BlendState* FResourceManager::GetOrCreateBlendState(EBlendType Type, ID3D11Device* Device)
{
	if (Device == nullptr)
	{
		Device = CachedDevice.Get();
	}
	return RenderStateCache.GetOrCreateBlendState(Type, Device);
}

ID3D11RasterizerState* FResourceManager::GetOrCreateRasterizerState(ERasterizerType Type, ID3D11Device* Device)
{
	if (Device == nullptr)
	{
		Device = CachedDevice.Get();
	}
	return RenderStateCache.GetOrCreateRasterizerState(Type, Device);
}

size_t FResourceManager::GetMaterialMemorySize() const
{
	return FResourceMemoryReporter::GetMaterialMemorySize(MaterialCache);
}
