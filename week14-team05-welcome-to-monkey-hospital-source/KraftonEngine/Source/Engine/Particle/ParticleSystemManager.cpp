#include "ParticleSystemManager.h"
#include "Object/GarbageCollection.h"
#include "Object/Object.h"

#include "Asset/AssetPackage.h"
#include "Platform/Paths.h"
#include "Particle/ParticleEmitter.h"
#include "Particle/ParticleEventManager.h"
#include "Particle/ParticleLODLevel.h"
#include "Particle/Modules/ParticleModuleCollision.h"
#include "Serialization/WindowsArchive.h"

#include <algorithm>
#include <filesystem>

#include "Core/Logging/Log.h"

namespace
{
	constexpr const char* Stage3LowerLODCollisionOverrideTargetPath =
		"Content/Particle System/NewParticleSystem_2.uasset";
	constexpr int32 Stage3LowerLODCollisionQueryBudget = 16;
	constexpr int32 Stage3FirstLowerCollisionOverrideLOD = 2;

	void ApplyStage3LowerLODCollisionOverrideToEmitter(UParticleEmitter* Emitter)
	{
		if (!Emitter)
		{
			return;
		}

		for (int32 LODIndex = Stage3FirstLowerCollisionOverrideLOD;
			LODIndex < Emitter->GetLODCount();
			++LODIndex)
		{
			UParticleLODLevel* LOD = Emitter->GetLODLevel(LODIndex);
			if (!LOD)
			{
				continue;
			}

			UParticleModuleCollision* CollisionModule =
				LOD->FindModuleByClass<UParticleModuleCollision>();
			if (!CollisionModule)
			{
				continue;
			}

			FParticleCollisionLODPolicyOverride& PolicyOverride =
				CollisionModule->LODPolicyOverride;

			// Stage 3 is a targeted rollout for the known falling-particle asset:
			// keep lower-LOD collision continuity with a small query budget, but do
			// not stomp on explicit authoring if an override is already enabled.
			if (PolicyOverride.bEnabled)
			{
				continue;
			}

			PolicyOverride.bEnabled = true;
			PolicyOverride.CollisionQueryBudget = Stage3LowerLODCollisionQueryBudget;
			PolicyOverride.bOverrideDisablePolicy = true;
			PolicyOverride.bDisableCollisionQueries = false;
		}
	}

	void ApplyStage3LowerLODCollisionOverrideIfNeeded(UParticleSystem* Asset)
	{
		if (!Asset)
		{
			return;
		}

		const FString NormalizedPath = FPaths::MakeProjectRelative(Asset->GetSourcePath());
		if (NormalizedPath != Stage3LowerLODCollisionOverrideTargetPath)
		{
			return;
		}

		for (int32 EmitterIndex = 0; EmitterIndex < Asset->GetEmitterCount(); ++EmitterIndex)
		{
			ApplyStage3LowerLODCollisionOverrideToEmitter(Asset->GetEmitter(EmitterIndex));
		}
	}
}

UParticleSystem* FParticleSystemManager::Load(const FString& Path)
{
	const FString NormalizedPath = FPaths::MakeProjectRelative(Path);

	auto It = LoadedParticleSystems.find(NormalizedPath);
	if (It != LoadedParticleSystems.end())
	{
		if (IsValid(It->second))
		{
			return It->second;
		}
		LoadedParticleSystems.erase(It);
	}

	if (!FAssetPackage::IsAssetPackagePath(NormalizedPath))
	{
		return nullptr;
	}

	FWindowsBinReader Ar(NormalizedPath);
	if (!Ar.IsValid())
	{
		return nullptr;
	}

	FAssetPackageHeader Header;
	FAssetImportMetadata Metadata;
	if (!FAssetPackage::ReadPackagePrelude(Ar, EAssetPackageType::ParticleSystem, Header, Metadata))
	{
		return nullptr;
	}

	UParticleSystem* NewAsset = UObjectManager::Get().CreateObject<UParticleSystem>();
	if (!NewAsset)
	{
		return nullptr;
	}

	NewAsset->Serialize(Ar);

	if (!Ar.IsValid())
	{
		UObjectManager::Get().DestroyObject(NewAsset);
		return nullptr;
	}

	NewAsset->SetSourcePath(NormalizedPath);
	NewAsset->BuildEmitters();
	ApplyStage3LowerLODCollisionOverrideIfNeeded(NewAsset);

	LoadedParticleSystems.emplace(NormalizedPath, NewAsset);
	return NewAsset;
}

UParticleSystem* FParticleSystemManager::Find(const FString& Path) const
{
	const FString NormalizedPath = FPaths::MakeProjectRelative(Path);

	auto It = LoadedParticleSystems.find(NormalizedPath);
	if (It == LoadedParticleSystems.end())
	{
		return nullptr;
	}
	if (IsValid(It->second))
	{
		return It->second;
	}
	return nullptr;
}

bool FParticleSystemManager::Save(UParticleSystem* Asset)
{
	if (!Asset)
	{
		UE_LOG("[ParticleSystemManager] Save failed: Asset is NULL");
		return false;
	}

	const FString& Path = Asset->GetSourcePath();
	if (Path.empty())
	{
		UE_LOG("[ParticleSystemManager] Save failed: SourcePath is empty. Asset=%p", Asset);
		return false;
	}

	const FString NormalizedPath = FPaths::MakeProjectRelative(Path);
	UE_LOG("[ParticleSystemManager] Save requested. SourcePath=%s NormalizedPath=%s Asset=%p EmitterCount=%d",
		Path.c_str(),
		NormalizedPath.c_str(),
		Asset,
		Asset->GetEmitterCount());

	{
		const FString TempPath = NormalizedPath + ".tmp";
		bool bWriteSucceeded = false;

		{
			FWindowsBinWriter Ar(TempPath);
			if (!Ar.IsValid())
			{
				UE_LOG("[ParticleSystemManager] Save failed: file open failed. Path=%s", TempPath.c_str());
				return false;
			}

			FAssetImportMetadata Metadata;
			if (!FAssetPackage::WritePackagePrelude(Ar, EAssetPackageType::ParticleSystem, Metadata))
			{
				UE_LOG("[ParticleSystemManager] Save failed: package prelude write failed. Path=%s", TempPath.c_str());
				return false;
			}

			Asset->BuildEmitters();
			Asset->Serialize(Ar);
			bWriteSucceeded = Ar.IsValid();
		}

		if (!bWriteSucceeded)
		{
			std::error_code RemoveError;
			std::filesystem::remove(FPaths::ToWide(TempPath), RemoveError);
			UE_LOG("[ParticleSystemManager] Save failed: archive invalid after asset serialize. Path=%s", TempPath.c_str());
			return false;
		}

		if (!MoveFileExW(
			FPaths::ToWide(TempPath).c_str(),
			FPaths::ToWide(NormalizedPath).c_str(),
			MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
		{
			UE_LOG("[ParticleSystemManager] Save failed: replace temp file failed. Path=%s TempPath=%s",
				NormalizedPath.c_str(),
				TempPath.c_str());
			return false;
		}
	}

	UE_LOG("[ParticleSystemManager] Save success. Path=%s", NormalizedPath.c_str());
	return true;
}

void FParticleSystemManager::SetDefaultEventManager(AParticleEventManager* InManager)
{
	DefaultEventManager = InManager;
}

AParticleEventManager* FParticleSystemManager::GetDefaultEventManager() const
{
	return DefaultEventManager.Get();
}

void FParticleSystemManager::RefreshAvailableParticleSystems()
{
	// Content 전체를 훑고 아래 ReadMetadata에서 ParticleSystem 타입만 거른다 — 저장 위치 무관.
	// (AnimGraphManager::RefreshAvailableGraphs 와 동일 패턴. 고정 하위 디렉토리로 제한하면
	//  다른 폴더에 저장된 자산이 목록에서 누락된다.)
	const std::filesystem::path ContentRoot =
		std::filesystem::path(FPaths::RootDir()) / L"Content";

	AvailableParticleSystemFiles.clear();

	if (!std::filesystem::exists(ContentRoot))
	{
		return;
	}

	const std::filesystem::path ProjectRoot(FPaths::RootDir());

	for (const auto& Entry : std::filesystem::recursive_directory_iterator(ContentRoot))
	{
		if (!Entry.is_regular_file())
		{
			continue;
		}

		std::wstring Ext = Entry.path().extension().wstring();
		std::transform(Ext.begin(), Ext.end(), Ext.begin(), ::towlower);

		if (Ext != L".uasset")
		{
			continue;
		}

		const FString RelPath =
			FPaths::ToUtf8(Entry.path().lexically_relative(ProjectRoot).generic_wstring());

		FAssetImportMetadata Metadata;
		if (!FAssetPackage::ReadMetadata(RelPath, EAssetPackageType::ParticleSystem, Metadata))
		{
			continue;
		}

		FAssetListItem Item;
		Item.DisplayName = FPaths::ToUtf8(Entry.path().stem().wstring());
		Item.FullPath = RelPath;

		AvailableParticleSystemFiles.push_back(std::move(Item));
	}
}

const TArray<FAssetListItem>& FParticleSystemManager::GetAvailableParticleSystemFiles() const
{
	return AvailableParticleSystemFiles;
}


void FParticleSystemManager::AddReferencedObjects(FReferenceCollector& Collector)
{
    for (auto& Pair : LoadedParticleSystems)
    {
        Collector.AddReferencedObject(Pair.second, "FParticleSystemManager.LoadedParticleSystems");
    }
}

void FParticleSystemManager::ClearCache()
{
    LoadedParticleSystems.clear();
    AvailableParticleSystemFiles.clear();
	DefaultEventManager.Reset();
}
