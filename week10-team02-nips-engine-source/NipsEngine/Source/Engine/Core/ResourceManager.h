#pragma once

#include "Asset/BinarySerializer.h"
#include "Asset/FontAtlasLoader.h"
#include "Asset/ParticleAtlasLoader.h"
#include "Asset/StaticMesh.h"
#include "Core/CoreTypes.h"
#include "Core/Singleton.h"
#include "Core/ResourceTypes.h"
#include "Object/FName.h"
#include "Render/Resource/Shader.h"
#include "Render/Resource/Material.h"
#include "Render/Resource/Texture.h"
#include "Render/Resource/RenderResources.h"
#include <d3d11.h>

class USkeleton;
class USkeletalMesh;

// 리소스를 관리하는 싱글턴.
// Resource.ini에서 리소스 경로/그리드 정보를 읽고, GPU 리소스를 로드/캐싱합니다.
// 컴포넌트는 소유하지 않고 포인터로 공유 데이터를 참조합니다.

#pragma region __ASSET_META__

constexpr const char* TextureMetaKey_Type = "Type";
constexpr const char* TextureMetaKey_Columns = "Columns";
constexpr const char* TextureMetaKey_Rows = "Rows";

enum class EAssetMetaType
{
	None,
	Font,
	Particle,
	Texture
};

struct FTextureAssetMeta
{
	EAssetMetaType Type = EAssetMetaType::None;
	int32 Columns = 1;
	int32 Rows = 1;
};

inline const char* ToString(EAssetMetaType Type)
{
	switch (Type)
	{
	case EAssetMetaType::Font: return "Font";
	case EAssetMetaType::Particle: return "Particle";
	default: return "None";
	}
}

inline EAssetMetaType ToAssetMetaType(const FString& Value)
{
	if (Value == "Font")
	{
		return EAssetMetaType::Font;
	}
	if (Value == "Particle")
	{
		return EAssetMetaType::Particle;
	}
	if (Value == "Texture") 
	{
		return EAssetMetaType::Texture;
	}
	return EAssetMetaType::None;
}

#pragma endregion

class FResourceManager : public TSingleton<FResourceManager>
{
	friend class TSingleton<FResourceManager>;

public:
	void SetCachedDevice(ID3D11Device* Device) { CachedDevice = Device; }
	ID3D11Device* GetCachedDevice() const { return CachedDevice.Get(); }

	void LoadFromAssetDirectory(const FString& Path);
	void RefreshFromAssetDirectory(const FString& Path);

	void InitializeDefaultResources(ID3D11Device* Device);
	ID3D11ShaderResourceView* GetDefaultWhiteSRV() const
	{
		auto it = Textures.find("DefaultWhite");
		if (it != Textures.end())
		{
			return it->second->GetSRV();
		}
		return nullptr;
	}

	bool LoadGPUResources(ID3D11Device* Device);
	void ReleaseGPUResources();

	UTexture* GetTexture(const FString& Path) const;
	UTexture* LoadTexture(const FString& Path, ID3D11Device* Device = nullptr);
	const TArray<FString>& GetTextureFilePath() const;

	UShader* GetShader(const FString& FilePath) const;
	UShader* GetShaderVariant(const FShaderCompileKey& CompileKey) const;
	bool LoadShader(const FString& FilePath, const FString& VSEntryPoint, const FString& PSEntryPoint,
	                const D3D_SHADER_MACRO* Defines = nullptr);
	bool LoadShader(const FString& FilePath, const FString& VSEntryPoint, const FString& PSEntryPoint,
                    const D3D11_INPUT_ELEMENT_DESC* InputElements, UINT InputElementCount, const D3D_SHADER_MACRO* Defines);
	bool LoadShader(const FShaderCompileKey& CompileKey);
	bool LoadShader(const FShaderCompileKey& CompileKey,
	                const D3D11_INPUT_ELEMENT_DESC* InputElements, UINT InputElementCount);
    //ID3DBlob* CompileShaderWithDefines(const WCHAR* filename, onst D3D_SHADER_MACRO* defines, const char* entryPoint, const char* shaderModel);

	UMaterial* GetMaterial(const FString& Path) const;
	UMaterial* GetOrCreateMaterial(const FString& Path, const FString& ShaderName);
	UMaterial* GetOrCreateMaterial(const FString& Name, const FString& Path, const FString& ShaderName);
	bool LoadMaterial(const FString& Path, const FString& ShaderName, ID3D11Device* Device = nullptr);

	bool SerializeMaterial(const FString& Path, const UMaterial* Material);
	bool SerializeMaterialInstance(const FString& Path, const UMaterialInstance* MaterialInstance);
	bool DeserializeMaterial(const FString& Path);
	TArray<FString> GetMaterialNames() const;
	TArray<FString> GetMaterialInterfaceNames() const;

	UMaterialInstance* CreateMaterialInstance(const FString& Path, UMaterial* Parent);
	UMaterialInstance* GetMaterialInstance(const FString& Path) const;
	UMaterialInterface* GetMaterialInterface(const FString& Path) const;

	FFontResource* FindFont(const FName& FontName);
	const FFontResource* FindFont(const FName& FontName) const;
	void RegisterFont(const FName& FontName, const FString& InPath, uint32 Columns = 16, uint32 Rows = 16);
	TArray<FString> GetFontNames() const;

	FParticleResource* FindParticle(const FName& ParticleName);
	const FParticleResource* FindParticle(const FName& ParticleName) const;
	void RegisterParticle(const FName& ParticleName, const FString& InPath, uint32 Columns = 1, uint32 Rows = 1);
	TArray<FString> GetParticleNames() const;

	UStaticMesh* LoadStaticMesh(const FString& Path);
	UStaticMesh* LoadStaticMesh(const FString& Path, bool bNormalizeToUnitCube);
	UStaticMesh* FindStaticMesh(const FString& Path) const;
	TArray<FString> GetStaticMeshPaths() const;
	FString MakeStaticMeshAssetPath(const FString& SourcePath) const;
	bool RegisterStaticMeshAsset(const FString& AssetPath);

	USkeletalMesh* LoadSkeletalMesh(const FString& Path);
    USkeletalMesh* FindSkeletalMesh(const FString& Path) const;
    TArray<FString> GetSkeletalMeshPaths() const;
	FString MakeSkeletalMeshAssetPath(const FString& SourcePath) const;
	FString MakeSkeletonAssetPath(const FString& SourcePath) const;
	bool RegisterSkeletalMeshAsset(const FString& AssetPath);
	bool RegisterSkeletonAsset(const FString& AssetPath);
	USkeleton* LoadSkeleton(const FString& Path);
	USkeleton* FindSkeleton(const FString& Path) const;
	TArray<FString> GetSkeletonPaths() const;
	bool LoadSkeletalMeshMaterialOverrides(const FString& Path, USkeletalMesh* Mesh);
	bool SaveSkeletalMeshMaterialOverrides(const FString& Path, const USkeletalMesh* Mesh) const;

	ID3D11SamplerState* GetOrCreateSamplerState(ESamplerType Type, ID3D11Device* Device = nullptr);
	ID3D11DepthStencilState* GetOrCreateDepthStencilState(EDepthStencilType Type, ID3D11Device* Device = nullptr);
	ID3D11BlendState* GetOrCreateBlendState(EBlendType Type, ID3D11Device* Device = nullptr);
	ID3D11RasterizerState* GetOrCreateRasterizerState(ERasterizerType Type, ID3D11Device* Device = nullptr);

	size_t GetMaterialMemorySize() const;
	
	//	Binary 전체 삭제
	void DeleteAllCacheFiles();

private:
	FString MakeSkeletalMeshMaterialOverridePath(const FString& SourcePath) const;
	void PreloadStaticMeshes();
	UStaticMesh* LoadStaticMeshWithOptions(const FString& Path, const FStaticMeshLoadOptions& LoadOptions);
	bool LoadShaderInternal(const FShaderCompileKey& CompileKey,
	                        const D3D11_INPUT_ELEMENT_DESC* InputElements,
	                        UINT InputElementCount,
	                        bool bRegisterPathAlias);
	void RegisterMaterialAliases(UMaterial* Material, const FString& FilePath, const FString& LegacyName);
	void RegisterMaterialInstanceAliases(UMaterialInstance* MaterialInstance, const FString& FilePath, const FString& LegacyName);
	
	FTextureAssetMeta LoadOrCreateTextureMeta(const std::filesystem::path& FilePath) const;

	FResourceManager() = default;
	~FResourceManager() { ReleaseGPUResources(); }

	TComPtr<ID3D11Device> CachedDevice;

	FFontAtlasLoader FontLoader;
	FParticleAtlasLoader ParticleLoader;
	
	FBinarySerializer BinarySerializer;

	TMap<FString, FFontResource>     FontResources;
	TMap<FString, FParticleResource> ParticleResources;
	
	TMap<FString, FStaticMeshResource> StaticMeshRegistry;

	TComPtr<ID3D11Texture2D>          DefaultWhiteTexture;
	TComPtr<ID3D11Texture2D>          DefaultNormalTexture;

	TMap<FString, UStaticMesh*> StaticMeshes;
	TMap<FString, UShader*> Shaders;
	TMap<FShaderCompileKey, UShader*> ShaderVariants;
	TMap<FString, UTexture*> Textures;
	TMap<FString, UMaterial*> Materials;
	TMap<FString, UMaterialInstance*> MaterialInstances;
	TMap<ESamplerType, TComPtr<ID3D11SamplerState>> SamplerStates;
	TMap<EDepthStencilType, TComPtr<ID3D11DepthStencilState>> DepthStencilStates;
	TMap<EBlendType, TComPtr<ID3D11BlendState>> BlendStates;
	TMap<ERasterizerType, TComPtr<ID3D11RasterizerState>> RasterizerStates;

	/* Paths */
	TArray<FString> ObjFilePaths;
	TArray<FString> MaterialFilePaths;
	TArray<FString> ParticleFilePaths;
	TArray<FString> FontFilePaths;
	TArray<FString> TextureFilePaths;

    TMap<FString, USkeletalMesh*> SkeletalMeshes;
	TMap<FString, USkeleton*> Skeletons;
    TArray<FString> SkeletalMeshFilePaths;
	TArray<FString> SkeletonFilePaths;
};
