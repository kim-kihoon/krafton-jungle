#include "AssetRegistry.h"
#include "Mesh/MeshManager.h"
#include "Mesh/Skeletal/SkeletalMesh.h"
#include "Animation/AnimationManager.h"
#include "Animation/Graph/AnimGraphManager.h"
#include "Animation/Sequence/AnimSequence.h"
#include "Animation/Montage/AnimMontage.h"
#include "Animation/Skeleton/Skeleton.h"
#include "Animation/Skeleton/SkeletonManager.h"
#include "Physics/PhysicsAsset.h"
#include "Physics/PhysicsAssetManager.h"
#include "Platform/Paths.h"
#include "Core/Logging/Log.h"
#include "Asset/AssetPackage.h"
#include "Materials/MaterialManager.h"

#include <algorithm>
#include <cstring>
#include <cwctype>
#include <exception>
#include <filesystem>

#include "Particle/ParticleSystemManager.h"
#include "Particle/VectorField/VectorFieldManager.h"
#include "LuaBlueprint/LuaBlueprintManager.h"

namespace FAssetRegistry
{
	const TArray<FAssetListItem>& ListByTypeName(const char* AssetTypeName)
	{
		static const TArray<FAssetListItem> Empty;
		if (!AssetTypeName) return Empty;

		try
		{
		if (std::strcmp(AssetTypeName, "UStaticMesh") == 0 || std::strcmp(AssetTypeName, "StaticMesh") == 0)
		{
			FMeshManager::ScanMeshAssets();
			return FMeshManager::GetAvailableStaticMeshFiles();
		}
		if (std::strcmp(AssetTypeName, "USkeletalMesh") == 0 || std::strcmp(AssetTypeName, "SkeletalMesh") == 0)
		{
			FMeshManager::ScanMeshAssets();
			return FMeshManager::GetAvailableSkeletalMeshFiles();
		}
        if (std::strcmp(AssetTypeName, "USkeleton") == 0)
        {
            return FSkeletonManager::Get().GetAvailableSkeletonFiles();
        }
        if (std::strcmp(AssetTypeName, "UPhysicsAsset") == 0 || std::strcmp(AssetTypeName, "PhysicsAsset") == 0)
        {
            return FPhysicsAssetManager::Get().GetAvailablePhysicsAssetFiles();
        }
		if (std::strcmp(AssetTypeName, "UAnimSequence") == 0)
		{
			// 콤보 열 때마다 재스캔 — 기존엔 MeshEditor 열 때만 Refresh 가 호출돼 PropertyWidget
			// 의 AnimSequence 콤보가 비어 있는 경우가 잦았음 (특히 새 PIE / 새 SkeletalMesh).
			FAnimationManager::Get().RefreshAvailableAnimations();
			return FAnimationManager::Get().GetAvailableAnimationFiles();
		}
		if (std::strcmp(AssetTypeName, "UAnimGraphAsset") == 0)
		{
			// 콤보 열 때마다 재스캔 — 방금 ContentBrowser 에서 만든 자산이 즉시 노출되도록.
			FAnimGraphManager::Get().RefreshAvailableGraphs();
			return FAnimGraphManager::Get().GetAvailableGraphFiles();
		}
        if (std::strcmp(AssetTypeName, "UParticleSystem") == 0
            || std::strcmp(AssetTypeName, "ParticleSystem") == 0
            || std::strcmp(AssetTypeName, "Particle") == 0)
        {
            FParticleSystemManager::Get().RefreshAvailableParticleSystems();
            return FParticleSystemManager::Get().GetAvailableParticleSystemFiles();
        }
		if (std::strcmp(AssetTypeName, "ULuaBlueprintAsset") == 0
			|| std::strcmp(AssetTypeName, "LuaBlueprint") == 0
			|| std::strcmp(AssetTypeName, "Lua Blueprint") == 0)
		{
			FLuaBlueprintManager::Get().RefreshAvailableBlueprints();
			return FLuaBlueprintManager::Get().GetAvailableBlueprintFiles();
		}
		if (std::strcmp(AssetTypeName, "UMaterial") == 0 || std::strcmp(AssetTypeName, "Material") == 0)
		{
			static TArray<FAssetListItem> MaterialCache;
			MaterialCache.clear();
			FMaterialManager::Get().ScanMaterialAssets();
			for (const FMaterialAssetListItem& MaterialItem : FMaterialManager::Get().GetAvailableMaterialFiles())
			{
				FAssetListItem Item;
				Item.DisplayName = MaterialItem.DisplayName;
				Item.FullPath = MaterialItem.FullPath;
				MaterialCache.push_back(std::move(Item));
			}
			return MaterialCache;
		}
		if (std::strcmp(AssetTypeName, "UTexture") == 0 || std::strcmp(AssetTypeName, "Texture") == 0)
		{
			static TArray<FAssetListItem> TextureCache;
			TextureCache.clear();
			FMaterialManager::Get().ScanTexturePaths();
			for (const FString& TexturePath : FMaterialManager::Get().GetAvailableTexturePaths())
			{
				std::filesystem::path Path(FPaths::ToWide(TexturePath));
				FAssetListItem Item;
				Item.DisplayName = FPaths::ToUtf8(Path.stem().wstring());
				Item.FullPath = TexturePath;
				TextureCache.push_back(std::move(Item));
			}
			return TextureCache;
		}
		if (std::strcmp(AssetTypeName, "Audio") == 0 || std::strcmp(AssetTypeName, "Sound") == 0)
		{
			static TArray<FAssetListItem> AudioCache;
			AudioCache.clear();

			namespace fs = std::filesystem;
			const fs::path AudioRoot = fs::path(FPaths::AudioDir());
			if (fs::exists(AudioRoot) && fs::is_directory(AudioRoot))
			{
				std::error_code IterError;
				for (fs::recursive_directory_iterator It(AudioRoot, fs::directory_options::skip_permission_denied, IterError), End;
				     !IterError && It != End; It.increment(IterError))
				{
					std::error_code EntryError;
					if (!It->is_regular_file(EntryError) || EntryError) continue;
					std::wstring Ext = It->path().extension().wstring();
					std::transform(Ext.begin(), Ext.end(), Ext.begin(), ::towlower);
					if (Ext != L".wav" && Ext != L".mp3" && Ext != L".ogg" && Ext != L".flac") continue;

					FAssetListItem Item;
					Item.DisplayName = FPaths::ToUtf8(It->path().stem().wstring());
					Item.FullPath = FPaths::ToUtf8(It->path().lexically_relative(AudioRoot).generic_wstring());
					AudioCache.push_back(std::move(Item));
				}
			}
			return AudioCache;
		}
		if (std::strcmp(AssetTypeName, "Script") == 0 || std::strcmp(AssetTypeName, "LuaScript") == 0)
		{
			static TArray<FAssetListItem> ScriptCache;
			ScriptCache.clear();

			namespace fs = std::filesystem;
			const fs::path ScriptRoot = fs::path(FPaths::ScriptDir());
			if (fs::exists(ScriptRoot) && fs::is_directory(ScriptRoot))
			{
				std::error_code IterError;
				for (fs::recursive_directory_iterator It(ScriptRoot, fs::directory_options::skip_permission_denied, IterError), End;
				     !IterError && It != End; It.increment(IterError))
				{
					std::error_code EntryError;
					if (!It->is_regular_file(EntryError) || EntryError) continue;
					if (It->path().extension() != L".lua") continue;

					FAssetListItem Item;
					Item.DisplayName = FPaths::ToUtf8(It->path().stem().wstring());
					Item.FullPath = FPaths::ToUtf8(It->path().lexically_relative(ScriptRoot).generic_wstring());
					ScriptCache.push_back(std::move(Item));
				}
			}
			return ScriptCache;
		}
		if (std::strcmp(AssetTypeName, "RmlDocument") == 0
			|| std::strcmp(AssetTypeName, "UIDocument") == 0
			|| std::strcmp(AssetTypeName, "UI") == 0)
		{
			static TArray<FAssetListItem> RmlDocumentCache;
			RmlDocumentCache.clear();

			namespace fs = std::filesystem;
			const fs::path ContentRoot = fs::path(FPaths::RootDir()) / L"Content";
			const fs::path ProjectRoot(FPaths::RootDir());
			if (fs::exists(ContentRoot) && fs::is_directory(ContentRoot))
			{
				std::error_code IterError;
				for (fs::recursive_directory_iterator It(ContentRoot, fs::directory_options::skip_permission_denied, IterError), End;
				     !IterError && It != End; It.increment(IterError))
				{
					std::error_code EntryError;
					if (!It->is_regular_file(EntryError) || EntryError) continue;

					std::wstring Ext = It->path().extension().wstring();
					std::transform(Ext.begin(), Ext.end(), Ext.begin(), ::towlower);
					if (Ext != L".rml" && Ext != L".html" && Ext != L".htm") continue;

					FAssetListItem Item;
					Item.DisplayName = FPaths::ToUtf8(It->path().stem().wstring());
					Item.FullPath = FPaths::ToUtf8(It->path().lexically_relative(ProjectRoot).generic_wstring());
					RmlDocumentCache.push_back(std::move(Item));
				}
			}
			return RmlDocumentCache;
		}
		if (std::strcmp(AssetTypeName, "UCameraShakeAsset") == 0 || std::strcmp(AssetTypeName, "CameraShake") == 0)
		{
			static TArray<FAssetListItem> CameraShakeCache;
			CameraShakeCache.clear();

			namespace fs = std::filesystem;
			const fs::path ContentRoot = fs::path(FPaths::RootDir()) / L"Content";
			const fs::path ProjectRoot(FPaths::RootDir());
			if (fs::exists(ContentRoot) && fs::is_directory(ContentRoot))
			{
				std::error_code IterError;
				for (fs::recursive_directory_iterator It(ContentRoot, fs::directory_options::skip_permission_denied, IterError), End;
				     !IterError && It != End; It.increment(IterError))
				{
					std::error_code EntryError;
					if (!It->is_regular_file(EntryError) || EntryError) continue;
					if (It->path().extension() != L".uasset") continue;
					const FString RelPath = FPaths::ToUtf8(It->path().lexically_relative(ProjectRoot).generic_wstring());
					FAssetImportMetadata Metadata;
					if (!FAssetPackage::ReadMetadata(RelPath, EAssetPackageType::CameraShake, Metadata)) continue;

					FAssetListItem Item;
					Item.DisplayName = FPaths::ToUtf8(It->path().stem().wstring());
					Item.FullPath = RelPath;
					CameraShakeCache.push_back(std::move(Item));
				}
			}
			return CameraShakeCache;
		}
		if (std::strcmp(AssetTypeName, "UVectorFieldAsset") == 0)
		{
			FVectorFieldManager::Get().RefreshAvailableVectorFields();
			return FVectorFieldManager::Get().GetAvailableVectorFieldFiles();
		}
		if (std::strcmp(AssetTypeName, "LuaAnimScript") == 0)
		{
			// Content/Script/Anim/ 하위 .lua 파일 즉석 스캔. 콤보 열 때만 호출되므로 비용 무시.
			// startup 캐싱 필요해지면 별도 매니저로 이관.
			static TArray<FAssetListItem> Cache;
			Cache.clear();

			namespace fs = std::filesystem;
			const fs::path Dir = fs::path(FPaths::RootDir()) / L"Content" / L"Script" / L"Anim";
			if (fs::exists(Dir) && fs::is_directory(Dir))
			{
				for (const auto& Entry : fs::directory_iterator(Dir))
				{
					if (!Entry.is_regular_file()) continue;
					if (Entry.path().extension() != L".lua") continue;

					FAssetListItem Item;
					Item.DisplayName = FPaths::ToUtf8(Entry.path().stem().wstring());
					// FullPath 는 ULuaScriptComponent 와 동일 컨벤션 — ResolveScriptPath 가 받는 형태.
					Item.FullPath    = FPaths::ToUtf8(
						(fs::path(L"Anim") / Entry.path().filename()).generic_wstring());
					Cache.push_back(Item);
				}
			}
			return Cache;
		}
		}
		catch (const std::exception& Ex)
		{
			UE_LOG("FAssetRegistry::ListByTypeName('%s') 디렉토리 스캔 예외: %s", AssetTypeName, Ex.what());
			return Empty;
		}

		return Empty;
	}

    TArray<FAssetListItem> ListSkeletons()
    {
        return FSkeletonManager::Get().GetAvailableSkeletonFiles();
    }

    bool ValidateAssetBinding(
        const FSkeletonBinding& A,
        const FSkeletonBinding& B,
        bool bAllowSameStructure,
        FSkeletonCompatibilityReport* OutReport)
    {
        const FSkeletonCompatibilityReport Report = FSkeletonManager::CheckCompatibility(A, B);
        if (OutReport)
        {
            *OutReport = Report;
        }

        if (Report.Result == ESkeletonCompatibilityResult::ExactSkeleton)
        {
            return true;
        }

        return bAllowSameStructure && Report.Result == ESkeletonCompatibilityResult::SameStructure;
    }

    TArray<FAssetListItem> ListAnimationsForSkeleton(const FSkeletonBinding& Skeleton, bool bAllowSameStructure)
    {
        TArray<FAssetListItem> Result;

        const TArray<FAssetListItem>& AnimFiles = FAnimationManager::Get().GetAvailableAnimationFiles();
        for (const FAssetListItem& Item : AnimFiles)
        {
            UAnimSequence* Sequence = FAnimationManager::Get().LoadAnimation(Item.FullPath);
            if (!Sequence)
            {
                continue;
            }

            if (ValidateAssetBinding(Sequence->GetSkeletonBinding(), Skeleton, bAllowSameStructure))
            {
                Result.push_back(Item);
            }
        }

        return Result;
    }

    TArray<FAssetListItem> ListMontagesForSkeleton(const FSkeletonBinding& Skeleton, bool bAllowSameStructure)
    {
        TArray<FAssetListItem> Result;

        const TArray<FAssetListItem>& MontageFiles = FAnimationManager::Get().GetAvailableMontageFiles();
        for (const FAssetListItem& Item : MontageFiles)
        {
            UAnimMontage* Montage = FAnimationManager::Get().LoadMontage(Item.FullPath);
            if (!Montage)
            {
                continue;
            }

            // Montage 자체는 skeleton 메타데이터가 없음 — source sequence 의 binding 으로 판정.
            // Source 가 비어 있으면 어떤 skeleton 에도 매칭할 수 없으니 스킵.
            const UAnimSequence* Src = Montage->GetSourceSequence();
            if (!Src)
            {
                continue;
            }

            if (ValidateAssetBinding(Src->GetSkeletonBinding(), Skeleton, bAllowSameStructure))
            {
                Result.push_back(Item);
            }
        }

        return Result;
    }

    TArray<FAssetListItem> ListMeshesForSkeleton(const FSkeletonBinding& Skeleton, bool bAllowSameStructure)
    {
        TArray<FAssetListItem> Result;

        const TArray<FAssetListItem>& MeshFiles = FMeshManager::GetAvailableSkeletalMeshFiles();
        for (const FAssetListItem& Item : MeshFiles)
        {
            FSkeletonBinding MeshBinding;
            if (!FMeshManager::ReadSkeletalMeshBinding(Item.FullPath, MeshBinding))
            {
                continue;
            }

            if (ValidateAssetBinding(MeshBinding, Skeleton, bAllowSameStructure))
            {
                Result.push_back(Item);
            }
        }

        return Result;
    }

    TArray<FAssetListItem> ListPhysicsAssetsForSkeleton(const FSkeletonBinding& Skeleton, bool bAllowSameStructure)
    {
        return FPhysicsAssetManager::Get().FindPhysicsAssetsForSkeleton(Skeleton, bAllowSameStructure);
    }

    USkeleton* FindSkeletonByGuid(const FString& SkeletonAssetGuid)
    {
        return FSkeletonManager::Get().FindSkeletonByAssetGuid(SkeletonAssetGuid);
    }

    bool CheckAnimationForMesh(const UAnimSequence* Sequence, const USkeletalMesh* Mesh, FSkeletonCompatibilityReport* OutReport)
    {
        if (!Sequence || !Mesh)
        {
            if (OutReport)
            {
                OutReport->Result = ESkeletonCompatibilityResult::Incompatible;
                OutReport->Reason = "null animation or mesh";
            }
            return false;
        }

        const FSkeletonCompatibilityReport Report = FSkeletonManager::CheckCompatibility(
            Sequence->GetSkeletonBinding(),
            Mesh->GetSkeletonBinding(),
            nullptr,
            Mesh->GetSkeleton());

        if (OutReport)
        {
            *OutReport = Report;
        }

        return Report.IsCompatible(false);
    }
}
