#include "Editor/Subsystem/AssetFactory.h"

#include "Animation/Graph/AnimGraphAsset.h"
#include "Animation/Graph/AnimGraphManager.h"
#include "CameraShake/CameraShakeAsset.h"
#include "CameraShake/CameraShakeManager.h"
#include "FloatCurve/FloatCurveManager.h"
#include "FloatCurve/FloatCurveAsset.h"
#include "LuaBlueprint/LuaBlueprintAsset.h"
#include "LuaBlueprint/LuaBlueprintManager.h"
#include "Object/Reflection/ObjectFactory.h"
#include "Platform/Paths.h"
#include "Materials/MaterialManager.h"
#include "Materials/Material.h"
#include "Mesh/Skeletal/SkeletalMesh.h"
#include "Physics/PhysicsAsset.h"
#include "Physics/PhysicsAssetManager.h"

#include <filesystem>

#include "Core/Logging/Log.h"
#include "Particle/ParticleSystemManager.h"
#include "Particle/ParticleEmitter.h"
#include "Particle/ParticleLODLevel.h"
#include "Particle/VectorField/VectorFieldAsset.h"
#include "Particle/VectorField/VectorFieldManager.h"

namespace
{
	FString SanitizeAssetStem(const FString& AssetName)
	{
		return AssetName.empty() ? FString("NewFloatCurve") : AssetName;
	}

	std::filesystem::path BuildUniqueAssetPath(const std::filesystem::path& Directory, const FString& AssetName, const wchar_t* Extension)
	{
		const FString BaseStem = SanitizeAssetStem(AssetName);

		int32 Suffix = 0;
		for (;;)
		{
			FString CandidateStem = BaseStem;
			if (Suffix > 0)
			{
				CandidateStem += "_";
				CandidateStem += std::to_string(Suffix);
			}

			std::filesystem::path CandidatePath = Directory / (FPaths::ToWide(CandidateStem) + Extension);
			if (!std::filesystem::exists(CandidatePath))
			{
				return CandidatePath;
			}

			++Suffix;
		}
	}

}

bool FAssetFactory::CreateFloatCurve(const FString& DirectoryPath, const FString& AssetName, FString& OutCreatedPath)
{
	const std::filesystem::path Directory(FPaths::ToWide(DirectoryPath));
	if (!std::filesystem::exists(Directory) || !std::filesystem::is_directory(Directory))
	{
		return false;
	}

	const std::filesystem::path AssetPath = BuildUniqueAssetPath(Directory, AssetName, L".uasset");

	UFloatCurveAsset* NewAsset = UObjectManager::Get().CreateObject<UFloatCurveAsset>();
	NewAsset->SetSourcePath(FPaths::ToUtf8(AssetPath.wstring()));

	FFloatCurve& Curve = NewAsset->GetCurve();
	Curve.Reset();
	Curve.AddKey(0.0f, 0.0f);
	Curve.AddKey(1.0f, 1.0f);
	Curve.SortKeys();

	bool bSaved = FFloatCurveManager::Get().Save(NewAsset);
	UObjectManager::Get().DestroyObject(NewAsset);

	if (!bSaved)
	{
		return false;
	}

	OutCreatedPath = FPaths::ToUtf8(AssetPath.wstring());
	return true;
}

bool FAssetFactory::CreateCameraShake(const FString& DirectoryPath, const FString& AssetName, FString& OutCreatedPath)
{
	const std::filesystem::path Directory(FPaths::ToWide(DirectoryPath));
	if (!std::filesystem::exists(Directory) || !std::filesystem::is_directory(Directory))
	{
		return false;
	}

	const std::filesystem::path AssetPath = BuildUniqueAssetPath(Directory, AssetName, L".uasset");

	UCameraShakeAsset* NewAsset = UObjectManager::Get().CreateObject<UCameraShakeAsset>();
	NewAsset->SetSourcePath(FPaths::ToUtf8(AssetPath.wstring()));
	NewAsset->Version = 1;
	NewAsset->ShakeType = ECameraShakeType::Sequence;

	bool bSaved = FCameraShakeManager::Get().Save(NewAsset);
	UObjectManager::Get().DestroyObject(NewAsset);

	if (!bSaved)
	{
		return false;
	}

	OutCreatedPath = FPaths::ToUtf8(AssetPath.wstring());
	return true;
}

bool FAssetFactory::CreateAnimGraph(const FString& DirectoryPath, const FString& AssetName, FString& OutCreatedPath)
{
	const std::filesystem::path Directory(FPaths::ToWide(DirectoryPath));
	if (!std::filesystem::exists(Directory) || !std::filesystem::is_directory(Directory))
	{
		return false;
	}

	const std::filesystem::path AssetPath = BuildUniqueAssetPath(Directory, AssetName, L".uasset");

	UAnimGraphAsset* NewAsset = UObjectManager::Get().CreateObject<UAnimGraphAsset>();
	NewAsset->SetSourcePath(FPaths::ToUtf8(AssetPath.wstring()));
	NewAsset->InitializeDefault(); // SequencePlayer → OutputPose 기본 그래프.

	bool bSaved = FAnimGraphManager::Get().Save(NewAsset);
	UObjectManager::Get().DestroyObject(NewAsset);

	if (!bSaved)
	{
		return false;
	}

	OutCreatedPath = FPaths::ToUtf8(AssetPath.wstring());
	return true;
}

bool FAssetFactory::CreateParticleSystem(
	const FString& DirectoryPath,
	const FString& AssetName,
	FString& OutCreatedPath)
{
	const std::filesystem::path Directory(FPaths::ToWide(DirectoryPath));
	if (!std::filesystem::exists(Directory) || !std::filesystem::is_directory(Directory))
	{
		UE_LOG("[ParticleSystemFactory] Create failed: invalid directory. DirectoryPath=%s", DirectoryPath.c_str());
		return false;
	}

	const std::filesystem::path AssetPath = BuildUniqueAssetPath(
		Directory,
		AssetName.empty() ? "NewParticleSystem" : AssetName,
		L".uasset"
	);
	const FString CreatedPath = FPaths::ToUtf8(AssetPath.wstring());

	UE_LOG("[ParticleSystemFactory] Create start. Directory=%s AssetName=%s CreatedPath=%s",
		DirectoryPath.c_str(),
		AssetName.c_str(),
		CreatedPath.c_str());

	UParticleSystem* NewAsset = UObjectManager::Get().CreateObject<UParticleSystem>();
	if (!NewAsset)
	{
		UE_LOG("[ParticleSystemFactory] CreateObject<UParticleSystem> returned NULL. Path=%s", CreatedPath.c_str());
		return false;
	}

	NewAsset->SetSourcePath(CreatedPath);
	NewAsset->AddEmitter();
	NewAsset->BuildEmitters();

	const bool bSaved = FParticleSystemManager::Get().Save(NewAsset);
	UObjectManager::Get().DestroyObject(NewAsset);

	if (!bSaved)
	{
		UE_LOG("[ParticleSystemFactory] Save failed. Path=%s", CreatedPath.c_str());
		return false;
	}

	UParticleSystem* Loaded = FParticleSystemManager::Get().Load(CreatedPath);
	if (!Loaded)
	{
		UE_LOG("[ParticleSystemFactory] Load FAILED after Save. Path=%s", CreatedPath.c_str());
		return false;
	}

	OutCreatedPath = CreatedPath;
	UE_LOG("[ParticleSystemFactory] Create success. Path=%s", OutCreatedPath.c_str());
	return true;
}


bool FAssetFactory::CreateVectorField(const FString& DirectoryPath, const FString& AssetName, FString& OutCreatedPath, int32 SizeX, int32 SizeY, int32 SizeZ)
{
	const std::filesystem::path Directory(FPaths::ToWide(DirectoryPath));
	if (!std::filesystem::exists(Directory) || !std::filesystem::is_directory(Directory))
	{
		return false;
	}

	if (SizeX <= 0) SizeX = 16;
	if (SizeY <= 0) SizeY = 16;
	if (SizeZ <= 0) SizeZ = 16;

	const FString EffectiveName = AssetName.empty() ? FString("NewVectorField") : AssetName;
	const std::filesystem::path AssetPath = BuildUniqueAssetPath(Directory, EffectiveName, L".uasset");

	TArray<FVector> ZeroVectors;
	ZeroVectors.resize(static_cast<size_t>(SizeX) * static_cast<size_t>(SizeY) * static_cast<size_t>(SizeZ), FVector::ZeroVector);

	UVectorFieldAsset* NewAsset = UObjectManager::Get().CreateObject<UVectorFieldAsset>();
	if (!NewAsset)
	{
		return false;
	}

	NewAsset->SetSourcePath(FPaths::ToUtf8(AssetPath.wstring()));
	NewAsset->SetGridData(SizeX, SizeY, SizeZ, FVector(-1.0f, -1.0f, -1.0f), FVector(1.0f, 1.0f, 1.0f), ZeroVectors);

	bool bSaved = FVectorFieldManager::Get().Save(NewAsset);
	UObjectManager::Get().DestroyObject(NewAsset);

	if (!bSaved)
	{
		return false;
	}

	OutCreatedPath = FPaths::ToUtf8(AssetPath.wstring());
	return true;
}

bool FAssetFactory::CreatePhysicsAssetForSkeletalMesh(
	const FString& DirectoryPath,
	const FString& AssetName,
	const USkeletalMesh* SkeletalMesh,
	FString& OutCreatedPath)
{
	if (!SkeletalMesh)
	{
		return false;
	}

	const std::filesystem::path Directory(FPaths::ToWide(DirectoryPath));
	if (!std::filesystem::exists(Directory) || !std::filesystem::is_directory(Directory))
	{
		return false;
	}

	const std::filesystem::path AssetPath = BuildUniqueAssetPath(
		Directory,
		AssetName.empty() ? FString("NewPhysicsAsset") : AssetName,
		L".uasset"
	);
	const FString CreatedPath = FPaths::ToUtf8(AssetPath.wstring());

	UPhysicsAsset* NewAsset = FPhysicsAssetManager::Get().CreatePhysicsAssetForSkeleton(SkeletalMesh, CreatedPath);
	if (!NewAsset)
	{
		return false;
	}

	const bool bSaved = FPhysicsAssetManager::Get().SavePhysicsAsset(
		NewAsset,
		CreatedPath,
		SkeletalMesh->GetAssetPathFileName()
	);
	UObjectManager::Get().DestroyObject(NewAsset);

	if (!bSaved)
	{
		return false;
	}

	FPhysicsAssetManager::Get().ScanPhysicsAssets();
	OutCreatedPath = CreatedPath;
	return true;
}

bool FAssetFactory::CreateMaterial(const FString& DirectoryPath, const FString& AssetName, FString& OutCreatedPath)
{
	const std::filesystem::path Directory(FPaths::ToWide(DirectoryPath));
	if (!std::filesystem::exists(Directory) || !std::filesystem::is_directory(Directory))
	{
		return false;
	}

	const std::filesystem::path AssetPath = BuildUniqueAssetPath(
		Directory, AssetName.empty() ? "NewMaterial" : AssetName, L".uasset");

	// 머티리얼 자산 식별자는 프로젝트 상대경로(Content/...) — 임포터/매니저 경로 규칙과 동일.
	const FString RelPath = FPaths::ToUtf8(
		AssetPath.lexically_relative(FPaths::RootDir()).generic_wstring());

	UMaterial* NewMaterial = FMaterialManager::Get().CreateMaterialAsset(RelPath);
	if (!NewMaterial)
	{
		return false;
	}

	OutCreatedPath = RelPath;
	return true;
}

bool FAssetFactory::CreateLuaBlueprint(const FString& DirectoryPath, const FString& AssetName, FString& OutCreatedPath)
{
	const std::filesystem::path Directory(FPaths::ToWide(DirectoryPath));
	if (!std::filesystem::exists(Directory) || !std::filesystem::is_directory(Directory))
	{
		return false;
	}

	const std::filesystem::path AssetPath = BuildUniqueAssetPath(
		Directory,
		AssetName.empty() ? "NewLuaBlueprint" : AssetName,
		L".uasset");

	ULuaBlueprintAsset* NewAsset = UObjectManager::Get().CreateObject<ULuaBlueprintAsset>();
	if (!NewAsset)
	{
		return false;
	}

	NewAsset->SetSourcePath(FPaths::ToUtf8(AssetPath.wstring()));
	NewAsset->InitializeDefault();

	const bool bSaved = FLuaBlueprintManager::Get().Save(NewAsset);
	UObjectManager::Get().DestroyObject(NewAsset);

	if (!bSaved)
	{
		return false;
	}

	OutCreatedPath = FPaths::ToUtf8(AssetPath.wstring());
	return true;
}
