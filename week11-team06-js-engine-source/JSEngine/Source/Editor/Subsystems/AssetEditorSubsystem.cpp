#include "Editor/Subsystems/AssetEditorSubsystem.h"

#include "Component/SkeletalMeshComponent.h"
#include "Core/AssetPathPolicy.h"
#include "Core/ImportedFbxAssetDiscovery.h"
#include "Core/Logging/Log.h"
#include "Core/Paths.h"
#include "Core/ResourceManager.h"
#include "Editor/EditorEngine.h"
#include "Editor/Viewer/AnimationViewer.h"
#include "ImGui/imgui.h"

#include <algorithm>
#include <cmath>
#include <filesystem>

namespace
{
bool IsFbxAssetPath(const FString& AssetPath)
{
	std::filesystem::path Path(FPaths::ToWide(FPaths::Normalize(AssetPath)));
	return Path.extension() == L".fbx";
}

FAnimationViewer* FindAnimationViewerUsingSequence(UEditorEngine* EditorEngine, const FString& AnimationPath)
{
    if (!EditorEngine)
    {
        return nullptr;
    }

    for (const auto& Viewer : EditorEngine->GetViewers())
    {
        FAnimationViewer* AnimationViewer = dynamic_cast<FAnimationViewer*>(Viewer.get());
        if (AnimationViewer && AnimationViewer->GetAnimationSequencePath() == AnimationPath)
        {
            return AnimationViewer;
        }
    }

    return nullptr;
}

FAnimationViewer* FindCompatibleAnimationViewer(UEditorEngine* EditorEngine, const FString& AnimationPath)
{
    if (!EditorEngine)
    {
        return nullptr;
    }

    for (const auto& Viewer : EditorEngine->GetViewers())
    {
        FAnimationViewer* AnimationViewer = dynamic_cast<FAnimationViewer*>(Viewer.get());
        if (AnimationViewer && AnimationViewer->IsAnimationSequenceCompatible(AnimationPath))
        {
            return AnimationViewer;
        }
    }

    return nullptr;
}

bool HasSkeletalMeshContent(const FString& FbxPath)
{
    const FFbxMeshContentInfo ContentInfo = FResourceManager::Get().InspectFbxMeshContent(FbxPath);
    return ContentInfo.bHasSkeletalMesh;
}
}

void FAssetEditorSubsystem::Initialize(UEditorEngine* InEditorEngine)
{
	EditorEngine = InEditorEngine;
}

void FAssetEditorSubsystem::RequestOpenEditorForAsset(const FString& AssetPath)
{
	if (!EditorEngine || AssetPath.empty())
	{
		return;
	}

	if (IsFbxAssetPath(AssetPath))
	{
		const TArray<FImportedFbxAssetRecord> Records = FResourceManager::Get().DiscoverImportedFbxAssets(AssetPath);
		auto SkeletalMeshRecordIt = std::find_if(
			Records.begin(),
			Records.end(),
			[](const FImportedFbxAssetRecord& Record)
			{
				return Record.Type == EImportedFbxAssetType::SkeletalMesh;
			});

		if (SkeletalMeshRecordIt != Records.end())
		{
			RequestOpenEditorForAsset(SkeletalMeshRecordIt->AssetPath);
			return;
		}

		auto StaticMeshRecordIt = std::find_if(
			Records.begin(),
			Records.end(),
			[](const FImportedFbxAssetRecord& Record)
			{
				return Record.Type == EImportedFbxAssetType::StaticMesh;
			});

		if (StaticMeshRecordIt != Records.end() && !HasSkeletalMeshContent(AssetPath))
		{
			EditorEngine->GetNotificationService().Info("FBX already imported as static mesh binary.");
			return;
		}

		QueueRequest(ERequestType::ImportFbxAndOpenEditor, AssetPath);
		return;
	}

	if (FAssetPathPolicy::IsAnimationSequenceAssetPath(AssetPath))
	{
		if (FAnimationViewer* AnimationViewer = FindAnimationViewerUsingSequence(EditorEngine, AssetPath))
		{
			EditorEngine->GetMainPanel().OpenViewer(AnimationViewer);
			return;
		}

		QueueRequest(ERequestType::OpenEditorForAsset, AssetPath);
		return;
	}

	for (const auto& Viewer : EditorEngine->GetViewers())
	{
		if (Viewer->GetFileName() == AssetPath)
		{
			EditorEngine->GetMainPanel().OpenViewer(Viewer.get());
			return;
		}
	}

	QueueRequest(ERequestType::OpenEditorForAsset, AssetPath);
}

void FAssetEditorSubsystem::RequestApplySkeletalMeshToComponent(
	USkeletalMeshComponent* Component,
	const FString& SkeletalMeshPath)
{
	if (!Component || SkeletalMeshPath.empty())
	{
		return;
	}

	if (Component->GetSkeletalMesh() &&
		Component->GetSkeletalMesh()->GetAssetPathFileName() == SkeletalMeshPath)
	{
		return;
	}

	QueueRequest(ERequestType::ApplySkeletalMeshToComponent, SkeletalMeshPath, Component);
}

void FAssetEditorSubsystem::DrawOpeningAssetModal()
{
	if (PendingRequest.Type == ERequestType::None)
	{
		return;
	}

	if (PendingRequest.bModalRequested)
	{
		ImGui::OpenPopup("Opening Asset");
		PendingRequest.bModalRequested = false;
	}

	if (!ImGui::BeginPopupModal("Opening Asset", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
	{
		return;
	}

	switch (PendingRequest.Type)
	{
	case ERequestType::OpenEditorForAsset:
		ImGui::TextUnformatted("Opening asset editor...");
		break;
	case ERequestType::ImportFbxAndOpenEditor:
		ImGui::TextUnformatted("Importing FBX...");
		break;
	case ERequestType::ApplySkeletalMeshToComponent:
		ImGui::TextUnformatted("Applying Skeletal Mesh...");
		break;
	default:
		ImGui::TextUnformatted("Opening asset...");
		break;
	}

	if (!PendingRequest.Path.empty())
	{
		ImGui::TextDisabled("%s", PendingRequest.Path.c_str());
	}

	const double Time = ImGui::GetTime();
	const float Progress = static_cast<float>(std::fmod(Time * 0.45, 1.0));
	ImGui::ProgressBar(Progress, ImVec2(360.0f, 0.0f), "");
	ImGui::TextDisabled("The editor may pause while the asset is loaded.");

	if (PendingRequest.DelayFrames > 0)
	{
		--PendingRequest.DelayFrames;
	}
	else
	{
		ExecutePendingRequest();
	}

	if (PendingRequest.Type == ERequestType::None)
	{
		ImGui::CloseCurrentPopup();
	}

	ImGui::EndPopup();
}

void FAssetEditorSubsystem::QueueRequest(
	ERequestType Type,
	const FString& Path,
	USkeletalMeshComponent* TargetComponent)
{
	PendingRequest.Type = Type;
	PendingRequest.Path = Path;
	PendingRequest.TargetComponent = TargetComponent;
	PendingRequest.DelayFrames = 2;
	PendingRequest.bModalRequested = true;
}

void FAssetEditorSubsystem::ExecutePendingRequest()
{
	if (!EditorEngine)
	{
		PendingRequest = FPendingRequest();
		return;
	}

	FPendingRequest Request = PendingRequest;
	PendingRequest = FPendingRequest();

	if (Request.Path.empty())
	{
		return;
	}

	switch (Request.Type)
	{
	case ERequestType::OpenEditorForAsset:
		OpenEditorForAsset(Request.Path);
		break;

	case ERequestType::ImportFbxAndOpenEditor:
	{
		const TArray<FImportedFbxAssetRecord> Records = FResourceManager::Get().ImportFbxAssets(Request.Path);
		EditorEngine->GetAssetService().RefreshAssetDatabase();

		auto MeshRecordIt = std::find_if(
			Records.begin(),
			Records.end(),
			[](const FImportedFbxAssetRecord& Record)
			{
				return Record.Type == EImportedFbxAssetType::SkeletalMesh;
			});

		if (MeshRecordIt != Records.end())
		{
			OpenEditorForAsset(MeshRecordIt->AssetPath);
		}
		else if (std::any_of(
			Records.begin(),
			Records.end(),
			[](const FImportedFbxAssetRecord& Record)
			{
				return Record.Type == EImportedFbxAssetType::StaticMesh;
			}))
		{
			EditorEngine->GetNotificationService().Info("FBX imported as static mesh binary.");
		}
		else
		{
			EditorEngine->GetNotificationService().Warning("No imported mesh was produced for this FBX.");
		}
		break;
	}

	case ERequestType::ApplySkeletalMeshToComponent:
	{
		if (!Request.TargetComponent)
		{
			return;
		}

		USkeletalMesh* Mesh = FResourceManager::Get().LoadSkeletalMesh(Request.Path);
		if (!Mesh)
		{
			UE_LOG_WARNING("[AssetEditorSubsystem] Failed to load skeletal mesh: %s", Request.Path.c_str());
			return;
		}

		Request.TargetComponent->SetSkeletalMesh(Mesh);
		EditorEngine->GetSceneService().MarkDirty();
		break;
	}

	default:
		break;
	}
}

void FAssetEditorSubsystem::OpenEditorForAsset(const FString& AssetPath)
{
	if (!EditorEngine || AssetPath.empty())
	{
		return;
	}

	if (FAssetPathPolicy::IsAnimationSequenceAssetPath(AssetPath))
	{
		if (FAnimationViewer* AnimationViewer = FindAnimationViewerUsingSequence(EditorEngine, AssetPath))
		{
			EditorEngine->GetMainPanel().OpenViewer(AnimationViewer);
			return;
		}

		FAnimationViewer* AnimationViewer = FindCompatibleAnimationViewer(EditorEngine, AssetPath);
		if (!AnimationViewer)
		{
			AnimationViewer = EditorEngine->CreateAnimationViewer(AssetPath);
		}

		if (!AnimationViewer || !AnimationViewer->SetAnimationSequence(AssetPath))
		{
			UE_LOG_WARNING("[AssetEditorSubsystem] Failed to apply animation sequence: %s", AssetPath.c_str());
			return;
		}

		EditorEngine->GetMainPanel().OpenViewer(AnimationViewer);
		return;
	}

	if (FAssetPathPolicy::IsAnimStateMachineAssetPath(AssetPath))
	{
		if (!EditorEngine->CreateAnimStateMachineGraphViewer(AssetPath))
		{
			UE_LOG_WARNING("[AssetEditorSubsystem] Failed to open animation state machine graph: %s", AssetPath.c_str());
		}
		return;
	}

	EditorEngine->CreateSkeletalViewer(AssetPath);
}

bool FAssetEditorSubsystem::FindExistingSkeletalMeshBinaryForFbx(
	const FString& FbxPath,
	FString& OutBinaryPath) const
{
	if (FbxPath.empty())
	{
		return false;
	}

	std::filesystem::path SourcePath(FPaths::ToWide(FPaths::Normalize(FbxPath)));
	if (SourcePath.is_relative())
	{
		SourcePath = FPaths::ToAbsolute(SourcePath.wstring());
	}

	std::error_code ErrorCode;
	if (!std::filesystem::exists(SourcePath, ErrorCode))
	{
		return false;
	}

	const std::filesystem::path ParentPath = SourcePath.parent_path();
	const std::wstring Prefix = SourcePath.stem().wstring() + L"_skeletalmesh_";

	for (const std::filesystem::directory_entry& Entry : std::filesystem::directory_iterator(ParentPath, ErrorCode))
	{
		if (ErrorCode || !Entry.is_regular_file())
		{
			continue;
		}

		const std::filesystem::path CandidatePath = Entry.path();
		if (CandidatePath.extension() != L".bin")
		{
			continue;
		}

		const std::wstring CandidateName = CandidatePath.filename().wstring();
		if (CandidateName.rfind(Prefix, 0) == 0)
		{
			OutBinaryPath = FPaths::ToRelativeString(CandidatePath.wstring());
			return true;
		}
	}

	return false;
}
