#include "Editor/Subsystem/AssetFactory.h"

#include "Animation/AnimInstanceAsset.h"
#include "Animation/AnimInstanceAssetManager.h"
#include "CameraShake/CameraShakeAsset.h"
#include "CameraShake/CameraShakeManager.h"
#include "FloatCurve/FloatCurveManager.h"
#include "FloatCurve/FloatCurveAsset.h"
#include "Object/ObjectFactory.h"
#include "Particle/ParticleSystem.h"
#include "Particle/ParticleSystemManager.h"
#include "Platform/Paths.h"

#include <filesystem>

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

	UFloatCurveAsset* NewAsset = GUObjectArray.CreateObject<UFloatCurveAsset>();
	NewAsset->SetSourcePath(FPaths::ToUtf8(AssetPath.wstring()));

	FFloatCurve& Curve = NewAsset->GetCurve();
	Curve.Reset();
	Curve.AddKey(0.0f, 0.0f);
	Curve.AddKey(1.0f, 1.0f);
	Curve.SortKeys();

	bool bSaved = FFloatCurveManager::Get().Save(NewAsset);
	GUObjectArray.DestroyObject(NewAsset);

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

	UCameraShakeAsset* NewAsset = GUObjectArray.CreateObject<UCameraShakeAsset>();
	NewAsset->SetSourcePath(FPaths::ToUtf8(AssetPath.wstring()));
	NewAsset->Version = 1;
	NewAsset->ShakeType = ECameraShakeType::Sequence;

	bool bSaved = FCameraShakeManager::Get().Save(NewAsset);
	GUObjectArray.DestroyObject(NewAsset);

	if (!bSaved)
	{
		return false;
	}

	OutCreatedPath = FPaths::ToUtf8(AssetPath.wstring());
	return true;
}

bool FAssetFactory::CreateAnimInstanceAsset(const FString& DirectoryPath, const FString& AssetName, FString& OutCreatedPath)
{
	const std::filesystem::path Directory(FPaths::ToWide(DirectoryPath));
	if (!std::filesystem::exists(Directory) || !std::filesystem::is_directory(Directory))
	{
		return false;
	}

	const FString DefaultName = AssetName.empty() ? FString("NewAnimInstance") : AssetName;
	const std::filesystem::path AssetPath = BuildUniqueAssetPath(Directory, DefaultName, L".uasset");

	UAnimInstanceAsset* NewAsset = GUObjectArray.CreateObject<UAnimInstanceAsset>();
	NewAsset->SetAssetPathFileName(FPaths::ToUtf8(AssetPath.wstring()));

	FAnimGraphData& Graph = NewAsset->GetGraph();

	FAnimGraphNodeData OutputNode;
	OutputNode.NodeId = "Output";
	OutputNode.DisplayName = "Output";
	OutputNode.NodeType = EAnimGraphNodeType::Output;
	OutputNode.EditorPosX = 360.0f;
	OutputNode.EditorPosY = 80.0f;

	Graph.OutputNodeId = OutputNode.NodeId;
	Graph.Nodes.push_back(OutputNode);

	bool bSaved = FAnimInstanceAssetManager::Get().Save(NewAsset);
	GUObjectArray.DestroyObject(NewAsset);

	if (!bSaved)
	{
		return false;
	}

	OutCreatedPath = FPaths::ToUtf8(AssetPath.wstring());
	return true;
}

bool FAssetFactory::CreateParticleSystem(const FString& DirectoryPath, const FString& AssetName, FString& OutCreatedPath)
{
	const std::filesystem::path Directory(FPaths::ToWide(DirectoryPath));
	if (!std::filesystem::exists(Directory) || !std::filesystem::is_directory(Directory))
	{
		return false;
	}

	const FString DefaultName = AssetName.empty() ? FString("NewParticleSystem") : AssetName;
	const std::filesystem::path AssetPath = BuildUniqueAssetPath(Directory, DefaultName, L".uasset");

	UParticleSystem* NewAsset = GUObjectArray.CreateObject<UParticleSystem>();
	NewAsset->SetAssetPathFileName(FPaths::ToUtf8(AssetPath.wstring()));
	NewAsset->SetFName(FName(FPaths::ToUtf8(AssetPath.stem().wstring())));

	bool bSaved = FParticleSystemManager::Get().Save(NewAsset);
	GUObjectArray.DestroyObject(NewAsset);

	if (!bSaved)
	{
		return false;
	}

	OutCreatedPath = FPaths::ToUtf8(AssetPath.wstring());
	return true;
}
