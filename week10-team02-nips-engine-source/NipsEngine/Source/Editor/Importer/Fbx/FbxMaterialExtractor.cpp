#include "Editor/Importer/Fbx/FbxMaterialExtractor.h"

#include "Core/Logger.h"
#include "Core/Paths.h"
#include "Core/ResourceManager.h"
#include "Editor/Importer/Fbx/FbxSceneUtils.h"
#include "Object/Object.h"

#include <fbxsdk.h>

#include <algorithm>
#include <filesystem>

namespace
{
	constexpr const char* DefaultUberLitShaderPath = "Shaders/UberLit.hlsl";

	FString MakeFbxMaterialAssetPath(const FString& SourcePath, const FString& AssetStem, const FString& SlotName)
	{
		namespace fs = std::filesystem;

		const fs::path SourceFsPath(FPaths::ToWide(FPaths::Normalize(SourcePath)));
		const std::wstring MaterialFileName =
			FPaths::ToWide(FbxSceneUtils::SanitizeAssetFileToken(AssetStem)) + L"_" +
			FPaths::ToWide(FbxSceneUtils::SanitizeAssetFileToken(SlotName)) + L".mat";
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

		UE_LOG("[FbxMaterialExtractor] Shader cache miss for imported material: %s. Attempting lazy load.", ShaderName.c_str());
		if (!TryLoadKnownMaterialShader(ResourceManager, ShaderName))
		{
			return nullptr;
		}

		return ResourceManager.GetShader(ShaderName);
	}

	bool TextureFileExists(const FString& TexturePath)
	{
		if (TexturePath.empty())
		{
			return false;
		}

		return std::filesystem::exists(std::filesystem::path(FPaths::ToAbsolute(FPaths::ToWide(TexturePath))));
	}

	FString FindExistingTexturePath(const TArray<FString>& Candidates)
	{
		for (const FString& Candidate : Candidates)
		{
			if (TextureFileExists(Candidate))
			{
				return Candidate;
			}
		}

		return {};
	}

	FString ResolveFbxTexturePath(const FString& FbxFilePath, const char* TextureFilePath)
	{
		if (TextureFilePath == nullptr || TextureFilePath[0] == '\0')
		{
			return {};
		}

		std::filesystem::path TexturePath(FPaths::ToWide(TextureFilePath));
		if (TexturePath.is_absolute())
		{
			return FPaths::ToUtf8(TexturePath.lexically_normal().generic_wstring());
		}

		const std::filesystem::path FbxDir = std::filesystem::path(FPaths::ToWide(FbxFilePath)).parent_path();
		const FString FbxRelativePath = FPaths::ToUtf8((FbxDir / TexturePath).lexically_normal().generic_wstring());
		if (TextureFileExists(FbxRelativePath))
		{
			return FbxRelativePath;
		}

		const FString FileName = FPaths::ToUtf8(TexturePath.filename().generic_wstring());
		const FString AssetTexturePath = FindExistingTexturePath({
			"Asset/Texture/" + FileName,
			"Asset/Fbx/Textures/" + FileName,
		});

		return AssetTexturePath.empty() ? FbxRelativePath : AssetTexturePath;
	}

	FString GetTexturePathFromProperty(const FString& FbxFilePath, FbxSurfaceMaterial* Material, const char* PropertyName)
	{
		if (Material == nullptr || PropertyName == nullptr)
		{
			return {};
		}

		FbxProperty TextureProperty = Material->FindProperty(PropertyName);
		if (!TextureProperty.IsValid())
		{
			return {};
		}

		const int32 TextureCount = TextureProperty.GetSrcObjectCount<FbxFileTexture>();
		for (int32 TextureIndex = 0; TextureIndex < TextureCount; ++TextureIndex)
		{
			FbxFileTexture* Texture = TextureProperty.GetSrcObject<FbxFileTexture>(TextureIndex);
			if (Texture == nullptr)
			{
				continue;
			}

			const char* RelativeFileName = Texture->GetRelativeFileName();
			if (RelativeFileName != nullptr && RelativeFileName[0] != '\0')
			{
				return ResolveFbxTexturePath(FbxFilePath, RelativeFileName);
			}

			const char* FileName = Texture->GetFileName();
			if (FileName != nullptr && FileName[0] != '\0')
			{
				return ResolveFbxTexturePath(FbxFilePath, FileName);
			}
		}

		return {};
	}

	FString BuildTextureStemFromMaterialName(const FString& MaterialName, const char* TextureSuffix)
	{
		FString BaseName = MaterialName;
		if (BaseName.starts_with("MI_"))
		{
			BaseName = BaseName.substr(3);
		}
		else if (BaseName.starts_with("M_"))
		{
			BaseName = BaseName.substr(2);
		}

		if (!BaseName.starts_with("T_"))
		{
			BaseName = "T_" + BaseName;
		}

		return BaseName + "_" + FString(TextureSuffix ? TextureSuffix : "");
	}

	FString FindAssetTextureByMaterialName(const FString& MaterialName, const char* TextureSuffix)
	{
		const FString Stem = BuildTextureStemFromMaterialName(MaterialName, TextureSuffix);
		const TArray<FString> Extensions = { ".PNG", ".png", ".DDS", ".dds", ".JPG", ".jpg", ".JPEG", ".jpeg" };

		TArray<FString> Candidates;
		for (const FString& Extension : Extensions)
		{
			Candidates.push_back("Asset/Texture/" + Stem + Extension);
			Candidates.push_back("Asset/Fbx/Textures/" + Stem + Extension);
		}

		return FindExistingTexturePath(Candidates);
	}

	FString GetDiffuseTexturePath(const FString& FbxFilePath, FbxSurfaceMaterial* Material)
	{
		FString TexturePath = GetTexturePathFromProperty(FbxFilePath, Material, FbxSurfaceMaterial::sDiffuse);
		if (!TexturePath.empty())
		{
			return TexturePath;
		}

		return Material ? FindAssetTextureByMaterialName(Material->GetName(), "D") : FString();
	}

	FString GetNormalTexturePath(const FString& FbxFilePath, FbxSurfaceMaterial* Material)
	{
		FString TexturePath = GetTexturePathFromProperty(FbxFilePath, Material, FbxSurfaceMaterial::sNormalMap);
		if (!TexturePath.empty())
		{
			return TexturePath;
		}

		TexturePath = GetTexturePathFromProperty(FbxFilePath, Material, FbxSurfaceMaterial::sBump);
		if (!TexturePath.empty())
		{
			return TexturePath;
		}

		return Material ? FindAssetTextureByMaterialName(Material->GetName(), "N") : FString();
	}

	FString GetSpecularTexturePath(const FString& FbxFilePath, FbxSurfaceMaterial* Material)
	{
		FString TexturePath = GetTexturePathFromProperty(FbxFilePath, Material, FbxSurfaceMaterial::sSpecular);
		if (!TexturePath.empty())
		{
			return TexturePath;
		}

		return Material ? FindAssetTextureByMaterialName(Material->GetName(), "S") : FString();
	}

	FVector ReadFbxColorProperty(FbxSurfaceMaterial* Material, const char* PropertyName, const FVector& DefaultValue)
	{
		if (Material == nullptr || PropertyName == nullptr)
		{
			return DefaultValue;
		}

		FbxProperty Property = Material->FindProperty(PropertyName);
		if (!Property.IsValid())
		{
			return DefaultValue;
		}

		const FbxDouble3 Value = Property.Get<FbxDouble3>();
		return FVector(static_cast<float>(Value[0]), static_cast<float>(Value[1]), static_cast<float>(Value[2]));
	}

	float ReadFbxFloatProperty(FbxSurfaceMaterial* Material, const char* PropertyName, float DefaultValue)
	{
		if (Material == nullptr || PropertyName == nullptr)
		{
			return DefaultValue;
		}

		FbxProperty Property = Material->FindProperty(PropertyName);
		if (!Property.IsValid())
		{
			return DefaultValue;
		}

		return static_cast<float>(Property.Get<FbxDouble>());
	}

	void SetDefaultUberLitParams(UMaterial* Material, UTexture* DiffuseTexture, UTexture* NormalTexture, UTexture* SpecularTexture)
	{
		if (Material == nullptr)
		{
			return;
		}

		FResourceManager& ResourceManager = FResourceManager::Get();
		UTexture* DefaultWhite = ResourceManager.GetTexture("DefaultWhite");
		UTexture* DefaultNormal = ResourceManager.GetTexture("DefaultNormal");

		Material->MaterialParams["BaseColor"] = FMaterialParamValue(Material->MaterialData.BaseColor);
		Material->MaterialParams["SpecularColor"] = FMaterialParamValue(Material->MaterialData.SpecularColor);
		Material->MaterialParams["EmissiveColor"] = FMaterialParamValue(Material->MaterialData.EmissiveColor);
		Material->MaterialParams["Shininess"] = FMaterialParamValue(Material->MaterialData.Shininess);
		Material->MaterialParams["Opacity"] = FMaterialParamValue(Material->MaterialData.Opacity);
		Material->MaterialParams["DiffuseMap"] = FMaterialParamValue(DiffuseTexture ? DiffuseTexture : DefaultWhite);
		Material->MaterialParams["SpecularMap"] = FMaterialParamValue(SpecularTexture ? SpecularTexture : DefaultWhite);
		Material->MaterialParams["NormalMap"] = FMaterialParamValue(NormalTexture ? NormalTexture : DefaultNormal);
		Material->MaterialParams["BumpMap"] = FMaterialParamValue(DefaultWhite);
		Material->MaterialParams["bHasDiffuseMap"] = FMaterialParamValue(Material->MaterialData.bHasDiffuseTexture);
		Material->MaterialParams["bHasSpecularMap"] = FMaterialParamValue(Material->MaterialData.bHasSpecularTexture);
		Material->MaterialParams["bHasNormalMap"] = FMaterialParamValue(Material->MaterialData.bHasNormalTexture);
		Material->MaterialParams["bHasBumpMap"] = FMaterialParamValue(false);
		Material->MaterialParams["ScrollUV"] = FMaterialParamValue(FVector2(0.0f, 0.0f));
	}

	UMaterial* CreateTransientMaterial(
		FResourceManager& ResourceManager,
		const FString& MaterialPath,
		const FString& SlotName,
		const FString& SourcePath,
		FbxSurfaceMaterial* FbxMaterial)
	{
		UShader* Shader = GetOrTryLoadMaterialShader(ResourceManager, DefaultUberLitShaderPath);
		if (Shader == nullptr)
		{
			UE_LOG("[FbxMaterialExtractor] Failed to load material shader for %s", MaterialPath.c_str());
			return nullptr;
		}

		const FString DiffuseTexturePath = GetDiffuseTexturePath(SourcePath, FbxMaterial);
		const FString NormalTexturePath = GetNormalTexturePath(SourcePath, FbxMaterial);
		const FString SpecularTexturePath = GetSpecularTexturePath(SourcePath, FbxMaterial);

		UTexture* DiffuseTexture = DiffuseTexturePath.empty() ? nullptr : ResourceManager.LoadTexture(DiffuseTexturePath);
		UTexture* NormalTexture = NormalTexturePath.empty() ? nullptr : ResourceManager.LoadTexture(NormalTexturePath);
		UTexture* SpecularTexture = SpecularTexturePath.empty() ? nullptr : ResourceManager.LoadTexture(SpecularTexturePath);

		UMaterial* Material = UObjectManager::Get().CreateObject<UMaterial>();
		Material->Name = SlotName;
		Material->FilePath = MaterialPath;
		Material->MaterialData.Name = SlotName;
		Material->MaterialData.BaseColor = ReadFbxColorProperty(FbxMaterial, FbxSurfaceMaterial::sDiffuse, FVector(0.8f, 0.8f, 0.8f));
		Material->MaterialData.SpecularColor = ReadFbxColorProperty(FbxMaterial, FbxSurfaceMaterial::sSpecular, FVector(0.0f, 0.0f, 0.0f));
		Material->MaterialData.EmissiveColor = ReadFbxColorProperty(FbxMaterial, FbxSurfaceMaterial::sEmissive, FVector(0.0f, 0.0f, 0.0f));
		Material->MaterialData.Shininess = ReadFbxFloatProperty(FbxMaterial, FbxSurfaceMaterial::sShininess, 30.0f);
		Material->MaterialData.Opacity = std::clamp(1.0f - ReadFbxFloatProperty(FbxMaterial, FbxSurfaceMaterial::sTransparencyFactor, 0.0f), 0.0f, 1.0f);
		Material->MaterialData.DiffuseTexPath = DiffuseTexturePath;
		Material->MaterialData.bHasDiffuseTexture = DiffuseTexture != nullptr && !DiffuseTexturePath.empty();
		Material->MaterialData.NormalTexPath = NormalTexturePath;
		Material->MaterialData.bHasNormalTexture = NormalTexture != nullptr && !NormalTexturePath.empty();
		Material->MaterialData.SpecularTexPath = SpecularTexturePath;
		Material->MaterialData.bHasSpecularTexture = SpecularTexture != nullptr && !SpecularTexturePath.empty();
		Material->SetShader(Shader);
		SetDefaultUberLitParams(Material, DiffuseTexture, NormalTexture, SpecularTexture);

		return Material;
	}

	bool CreateMaterialAsset(
		FResourceManager& ResourceManager,
		const FString& MaterialPath,
		const FString& SlotName,
		const FString& SourcePath,
		FbxSurfaceMaterial* FbxMaterial)
	{
		UMaterial* Material = CreateTransientMaterial(ResourceManager, MaterialPath, SlotName, SourcePath, FbxMaterial);
		if (Material == nullptr)
		{
			return false;
		}

		const bool bSerialized = ResourceManager.SerializeMaterial(MaterialPath, Material);
		UObjectManager::Get().DestroyObject(Material);

		if (!bSerialized)
		{
			UE_LOG("[FbxMaterialExtractor] Failed to serialize material asset: %s", MaterialPath.c_str());
		}

		return bSerialized;
	}
}

FFbxExtractedMaterial FFbxMaterialExtractor::ExtractMaterial(
	const FString& SourcePath,
	FbxSurfaceMaterial* FbxMaterial,
	const char* FallbackName) const
{
	FFbxExtractedMaterial Result;
	Result.SlotName = FbxSceneUtils::GetFbxObjectName(FbxMaterial, FallbackName ? FallbackName : "Material");
	Result.DiffuseTexturePath = GetDiffuseTexturePath(SourcePath, FbxMaterial);
	Result.NormalTexturePath = GetNormalTexturePath(SourcePath, FbxMaterial);
	Result.SpecularTexturePath = GetSpecularTexturePath(SourcePath, FbxMaterial);
	Result.SourceMaterial = FbxMaterial;

	return Result;
}

bool FFbxMaterialExtractor::ResolveMaterialAssetForSlot(
	FResourceManager& ResourceManager,
	const FString& SourcePath,
	const FString& AssetStem,
	const FFbxMaterialSlotSource& SlotSource,
	FString& OutMaterialPath,
	TArray<FString>* OutMaterialPaths) const
{
	namespace fs = std::filesystem;

	const FString SlotName = SlotSource.SlotName.empty() ? FString("DefaultWhite") : SlotSource.SlotName;
	const FString MaterialPath = MakeFbxMaterialAssetPath(SourcePath, AssetStem, SlotName);
	fs::path AbsoluteMaterialPath(FPaths::ToAbsolute(FPaths::ToWide(MaterialPath)));
	fs::path AbsoluteMaterialInstancePath = AbsoluteMaterialPath;
	AbsoluteMaterialInstancePath.replace_extension(L".matinst");

	const bool bMaterialExists = fs::exists(AbsoluteMaterialPath);
	const bool bMaterialInstanceExists = fs::exists(AbsoluteMaterialInstancePath);

	if (!bMaterialExists && bMaterialInstanceExists)
	{
		OutMaterialPath = FPaths::ToUtf8(
			fs::path(FPaths::ToWide(MaterialPath)).replace_extension(L".matinst").generic_wstring());
		return true;
	}

	OutMaterialPath = MaterialPath;
	if (!bMaterialExists)
	{
		if (!CreateMaterialAsset(ResourceManager, MaterialPath, SlotName, SourcePath, SlotSource.Material))
		{
			OutMaterialPath = "DefaultWhite";
			return false;
		}

		UE_LOG("[FbxMaterialExtractor] Created material asset: %s", MaterialPath.c_str());
	}

	if (OutMaterialPaths &&
		std::find(OutMaterialPaths->begin(), OutMaterialPaths->end(), OutMaterialPath) == OutMaterialPaths->end())
	{
		OutMaterialPaths->push_back(OutMaterialPath);
	}

	return true;
}
