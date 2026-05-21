#pragma once

#include "Asset/BinarySerializer.h"
#include "Asset/AnimationSequence.h"
#include "Asset/AnimationSequenceSerializer.h"
#include "Asset/CurveFloatAsset.h"
#include "Asset/FbxImporter.h"
#include "Asset/ObjLoader.h"
#include "Asset/SkeletonAsset.h"
#include "Asset/SkeletonSerializer.h"
#include "Asset/SkeletalMesh.h"
#include "Asset/StaticMesh.h"
#include "Core/AtlasResourceCache.h"
#include "Core/CurveResourceCache.h"
#include "Core/CoreTypes.h"
#include "Core/ImportedFbxAssetDiscovery.h"
#include "Core/MaterialResourceCache.h"
#include "Core/RenderStateResourceCache.h"
#include "Core/Singleton.h"
#include "Core/ResourceTypes.h"
#include "Core/ShaderResourceCache.h"
#include "Core/StaticMeshResourceCache.h"
#include "Core/TextureAssetMetaService.h"
#include "Core/TextureResourceCache.h"
#include "Object/FName.h"
#include "Render/Resource/ComputeShader.h"
#include "Render/Resource/ShaderTypes.h"
#include "Render/Resource/Material.h"
#include "Render/Resource/Texture.h"
#include "Render/Resource/RenderResources.h"
#include <d3d11.h>


class FMaterialLoadService;
class FMaterialSerializationService;
class FAnimationSequenceLoadService;
class FStaticMeshLoadService;
class FSkeletalMeshLoadService;
class FSkeletonLoadService;
class FFbxMaterialLoadService;
class UAnimStateMachineAsset;

//   .
class FResourceManager : public TSingleton<FResourceManager>
{
	friend class TSingleton<FResourceManager>;
	friend class FMaterialLoadService;
	friend class FMaterialSerializationService;
	friend class FAnimationSequenceLoadService;
	friend class FStaticMeshLoadService;
	friend class FSkeletalMeshLoadService;
	friend class FSkeletonLoadService;
	friend class FFbxMaterialLoadService;

public:
	void SetCachedDevice(ID3D11Device* Device) { CachedDevice = Device; }

	void LoadFromAssetDirectory(const FString& Path);
	void RefreshFromAssetDirectory(const FString& Path);

	void InitializeDefaultResources(ID3D11Device* Device);
	ID3D11ShaderResourceView* GetDefaultWhiteSRV() const
	{
		return TextureCache.GetDefaultWhiteSRV();
	}

	bool LoadGPUResources(ID3D11Device* Device);
	void ReleaseGPUResources();

	UTexture* GetTexture(const FString& Path) const;
	UTexture* LoadTexture(const FString& Path, ID3D11Device* Device = nullptr);
	const TArray<FString>& GetTextureFilePath() const;

	// Shader  VS / PS Stage  , Draw  Program(VS+PS ) .
	FVertexShader* GetOrCreateVertexShader(
		const FShaderStageKey& Key,
		const D3D_SHADER_MACRO* Defines = nullptr,
		const FVertexLayoutDesc* VertexLayout = nullptr);
	FPixelShader* GetOrCreatePixelShader(const FShaderStageKey& Key, const D3D_SHADER_MACRO* Defines = nullptr);
	FShaderProgram* GetOrCreateShaderProgram(
		const FShaderStageKey& VSKey,
		const FShaderStageKey& PSKey,
		const D3D_SHADER_MACRO* VSDefines = nullptr,
		const D3D_SHADER_MACRO* PSDefines = nullptr,
		const FVertexLayoutDesc* VertexLayout = nullptr);

	FComputeShader* GetComputeShader(const FString& Key) const;
    bool LoadComputeShader(const FString& FilePath, const FString& CSEntryPoint,
                           const D3D_SHADER_MACRO* Defines = nullptr, const FString& Key = "");

	// Shader     Stage/Program  .    Lazy Compile .
	void InvalidateShaderFile(const FString& Path);

	UMaterial* GetMaterial(const FString& Path) const;
	UMaterial* GetOrCreateMaterial(const FString& Path, EMaterialShaderType ShaderType = EMaterialShaderType::SurfaceLit);
	UMaterial* GetOrCreateMaterial(const FString& Name, const FString& Path, EMaterialShaderType ShaderType = EMaterialShaderType::SurfaceLit);
	bool LoadMaterial(const FString& Path, EMaterialShaderType ShaderType = EMaterialShaderType::SurfaceLit, ID3D11Device* Device = nullptr);
	bool ImportMaterialFromFbx(const FString& SourceFbxPath, EMaterialShaderType ShaderType = EMaterialShaderType::SurfaceLit, ID3D11Device* Device = nullptr);

	bool SerializeMaterial(const FString& Path, const UMaterial* Material);
	bool SerializeMaterialInstance(const FString& Path, const UMaterialInstance* MaterialInstance);
	bool DeserializeMaterial(const FString& Path);
	TArray<FString> GetMaterialNames() const;
	TArray<FString> GetMaterialInterfaceNames() const;

	UMaterialInstance* CreateMaterialInstance(const FString& Path, UMaterial* Parent);
	UMaterialInstance* GetMaterialInstance(const FString& Path) const;
	UMaterialInterface* GetMaterialInterface(const FString& Path);

	FFontResource* FindFont(const FName& FontName);
	const FFontResource* FindFont(const FName& FontName) const;
	void RegisterFont(const FName& FontName, const FString& InPath, uint32 Columns = 16, uint32 Rows = 16);
	TArray<FString> GetFontNames() const;

	FParticleResource* FindParticle(const FName& ParticleName);
	const FParticleResource* FindParticle(const FName& ParticleName) const;
	void RegisterParticle(const FName& ParticleName, const FString& InPath, uint32 Columns = 1, uint32 Rows = 1);
	TArray<FString> GetParticleNames() const;

	UStaticMesh* LoadStaticMesh(const FString& Path);
	UStaticMesh* ImportStaticMeshFromFbx(const FString& SourceFbxPath, bool bImportMaterials = true);
	UStaticMesh* FindStaticMesh(const FString& Path) const;
	TArray<FString> GetStaticMeshPaths() const;

	USkeletonAsset* LoadSkeleton(const FString& Path);
	USkeletonAsset* FindSkeleton(const FString& Path) const;
	TArray<FString> GetSkeletonPaths() const;
	TArray<USkeletonAsset*> ImportSkeletonsFromFbx(const FString& SourceFbxPath);
	bool SaveSkeleton(USkeletonAsset* Skeleton);

	/*
	 * note:         
	 */
	USkeletalMesh* LoadSkeletalMesh(const FString& Path);
	USkeletalMesh* LoadSkeletalMesh(const FString& Path, const FString& SkeletonName);
	USkeletalMesh* ImportSkeletalMeshFromFbx(
		const FString& SourceFbxPath,
		const FString& SkeletonName = FString(),
		bool bImportMaterials = true,
		const TArray<USkeletonAsset*>* ImportedSkeletonsOverride = nullptr);
    USkeletalMesh* FindSkeletalMesh(const FString& Path) const;
	TArray<FString> GetSkeletalMeshPaths() const;
	FFbxMeshContentInfo InspectFbxMeshContent(const FString& Path);
	TArray<FImportedFbxAssetRecord> ImportFbxAssets(const FString& SourceFbxPath);
	TArray<FImportedFbxAssetRecord> DiscoverImportedFbxAssets(const FString& SourceFbxPath) const;

	//  socket  mesh data   writable cache(.bin) .
	bool SaveSkeletalMesh(USkeletalMesh* Mesh);

	UCurveFloatAsset* LoadCurve(const FString& Path);
	UCurveFloatAsset* FindCurve(const FString& Path) const;
	bool SaveCurve(const FString& Path, const UCurveFloatAsset* Curve);
	TArray<FString> GetCurvePaths() const;

	UAnimationSequence* LoadAnimationSequence(const FString& Path);
	UAnimationSequence* ImportAnimationSequencesFromFbx(
		const FString& SourceFbxPath,
		const TArray<USkeletonAsset*>* ImportedSkeletonsOverride = nullptr);
	UAnimationSequence* FindAnimationSequence(const FString& Path) const;
	bool SaveAnimationSequence(UAnimationSequence* Sequence);
	TArray<FString> GetAnimationSequencePaths() const;

	UAnimStateMachineAsset* LoadAnimStateMachineAsset(const FString& Path);
	UAnimStateMachineAsset* FindAnimStateMachineAsset(const FString& Path) const;
	bool SaveAnimStateMachineAsset(const FString& Path, const UAnimStateMachineAsset* Asset);
	TArray<FString> GetAnimStateMachineAssetPaths() const;

	ID3D11SamplerState* GetOrCreateSamplerState(ESamplerType Type, ID3D11Device* Device = nullptr);
	ID3D11DepthStencilState* GetOrCreateDepthStencilState(EDepthStencilType Type, ID3D11Device* Device = nullptr);
	ID3D11BlendState* GetOrCreateBlendState(EBlendType Type, ID3D11Device* Device = nullptr);
	ID3D11RasterizerState* GetOrCreateRasterizerState(ERasterizerType Type, ID3D11Device* Device = nullptr);

	size_t GetMaterialMemorySize() const;
	
	//  Binary   (      )
	void DeleteAllCacheFiles();

private:
	void ClearDiscoveredResourceLists(bool bClearAtlasCache);
	void RegisterDiscoveredAssetFile(const std::filesystem::path& FilePath, const std::filesystem::path& ProjectRootPath);
	void InitializeDefaultWhiteTexture(ID3D11Device* Device);
	void InitializeDefaultMaterial(ID3D11Device* Device);
	void InitializeOutlineMaterial();
	uint64 GetFileWriteTimeTicks(const FString& Path) const;
	bool IsStaticMeshBinaryValid(const FString& SourcePath, const FString& BinaryPath) const;
	bool IsSkeletalMeshBinaryValid(const FString& SourcePath, const FString& BinaryPath) const;
	bool IsAnimationSequenceBinaryValid(const FString& SourcePath, const FString& BinaryPath) const;
	void PreloadStaticMeshes();
	UStaticMesh* CreateStaticMeshFromLoadedData(FStaticMesh* LoadedMeshData, const FString& LogPath, bool bLogLodTiming, bool bLogLodSkipped) const;
	
	FTextureAssetMeta LoadOrCreateTextureMeta(const std::filesystem::path& FilePath) const;
	void RegisterObjMaterialSlotAliases(const FString& ObjPath, const FString& MtlPath);
	UMaterial* GetMaterialForStaticMeshSlot(const FString& SourcePath, const FString& SlotName) const;
	void ResolveStaticMeshMaterialSlots(const FString& SourcePath, FStaticMesh* StaticMesh) const;

	void ResolveSkeletalMeshMaterialSlots(const FString& SourcePath, FSkeletalMesh* SkeletalMesh) const;

	FResourceManager() = default;
	~FResourceManager() { ReleaseGPUResources(); }

	TComPtr<ID3D11Device> CachedDevice;

	FObjLoader ObjLoader;
	FFbxImporter FbxImporter;
	FBinarySerializer BinarySerializer;
	FAnimationSequenceSerializer AnimationSequenceSerializer;
	FSkeletonSerializer SkeletonSerializer;

	TComPtr<ID3D11Texture2D>          DefaultWhiteTexture;

	FCurveResourceCache CurveCache;
	FShaderResourceCache ShaderCache;
	FTextureResourceCache TextureCache;
	FMaterialResourceCache MaterialCache;
	FRenderStateResourceCache RenderStateCache;
	FStaticMeshResourceCache StaticMeshCache;
	FAtlasResourceCache AtlasCache;

	//  skeletal mesh source FBX path  cache.bin path  key       (alias)
	TMap<FString, USkeletalMesh*> SkeletalMeshMap;
	TMap<FString, USkeletonAsset*> SkeletonMap;
	TMap<FString, UAnimationSequence*> AnimationSequenceMap;
	TMap<FString, UAnimStateMachineAsset*> AnimStateMachineMap;

	/* Paths */
	TArray<FString> ObjFilePaths;
	TArray<FString> MaterialFilePaths;
	TArray<FString> ParticleFilePaths;
	TArray<FString> FontFilePaths;
	TArray<FString> TextureFilePaths;
	TArray<FString> SkeletonFilePaths;
	TArray<FString> SkeletalMeshFilePaths;
	TArray<FString> CurveFilePaths;
	TArray<FString> AnimationSequenceFilePaths;
	TArray<FString> AnimStateMachineFilePaths;
};
