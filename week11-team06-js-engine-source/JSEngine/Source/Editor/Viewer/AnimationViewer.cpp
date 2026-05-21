#include "AnimationViewer.h"

#include "Animation/AnimSingleNodeInstance.h"
#include "Asset/AnimationSequence.h"
#include "Asset/SkeletonAsset.h"
#include "Component/SkeletalMeshComponent.h"
#include "Core/AssetPathPolicy.h"
#include "Core/Logging/Log.h"
#include "Core/Paths.h"
#include "Core/ResourceManager.h"
#include "Editor/Selection/SelectionManager.h"
#include "GameFramework/PrimitiveActors.h"
#include "GameFramework/World.h"
#include "Object/Object.h"

#include <algorithm>
#include <cmath>
#include <cwctype>
#include <filesystem>

namespace
{
static const char* GetSkeletalMeshPath(const USkeletalMesh* Mesh)
{
    return Mesh ? Mesh->GetAssetPathFileName().c_str() : "<None>";
}

static FString ToLowerNormalizedPath(const FString& Path)
{
    std::wstring Wide = FPaths::ToWide(FPaths::Normalize(Path));
    std::transform(
        Wide.begin(),
        Wide.end(),
        Wide.begin(),
        [](wchar_t Ch)
        {
            return static_cast<wchar_t>(std::towlower(Ch));
        });
    return FPaths::ToUtf8(Wide);
}

static FString ResolveSkeletonPathFromMesh(const USkeletalMesh* Mesh)
{
    if (!Mesh)
    {
        return FString();
    }

    if (const USkeletonAsset* SkeletonAsset = Mesh->GetSkeletonAsset())
    {
        const FString& SkeletonPath = SkeletonAsset->GetAssetPathFileName();
        if (!SkeletonPath.empty())
        {
            return ToLowerNormalizedPath(SkeletonPath);
        }
    }

    if (!Mesh->GetSkeletonSourcePath().empty())
    {
        return ToLowerNormalizedPath(FAssetPathPolicy::ResolveAssetRelativePath(
            Mesh->GetAssetPathFileName(),
            Mesh->GetSkeletonSourcePath()));
    }

    return FString();
}

static FString ResolveSkeletonPathFromAnimation(const UAnimationSequence* Sequence)
{
    const FAnimationSequence* SequenceData = Sequence ? Sequence->GetSequenceData() : nullptr;
    if (!SequenceData || SequenceData->SkeletonSourcePath.empty())
    {
        return FString();
    }

    return ToLowerNormalizedPath(FAssetPathPolicy::ResolveAssetRelativePath(
        Sequence->GetResolvedPath(),
        SequenceData->SkeletonSourcePath));
}

static FString GetImportedFbxStem(const FString& AssetPath)
{
    std::filesystem::path FsPath(FPaths::ToWide(FPaths::Normalize(AssetPath)));
    std::wstring Stem = FsPath.stem().wstring();
    std::transform(Stem.begin(), Stem.end(), Stem.begin(), ::towlower);

    static const std::wstring Markers[] = { L"_skeletalmesh_", L"_skeleton_", L"_anim_" };
    for (const std::wstring& Marker : Markers)
    {
        const size_t MarkerPos = Stem.find(Marker);
        if (MarkerPos != std::wstring::npos)
        {
            Stem = Stem.substr(0, MarkerPos);
            break;
        }
    }

    return FPaths::ToUtf8(Stem);
}

static bool IsAnimationCompatibleWithMesh(const USkeletalMesh* Mesh, const UAnimationSequence* Sequence, const FString& AnimationPath)
{
    if (!Mesh || !Sequence)
    {
        return false;
    }

    const FString MeshSkeleton = ResolveSkeletonPathFromMesh(Mesh);
    const FString AnimationSkeleton = ResolveSkeletonPathFromAnimation(Sequence);
    if (!MeshSkeleton.empty() && !AnimationSkeleton.empty())
    {
        return MeshSkeleton == AnimationSkeleton;
    }

    const std::filesystem::path MeshPath(FPaths::ToWide(FPaths::Normalize(Mesh->GetAssetPathFileName())));
    const std::filesystem::path AnimPath(FPaths::ToWide(FPaths::Normalize(AnimationPath)));

    return ToLowerNormalizedPath(FPaths::ToUtf8(MeshPath.parent_path().generic_wstring())) ==
               ToLowerNormalizedPath(FPaths::ToUtf8(AnimPath.parent_path().generic_wstring())) &&
           GetImportedFbxStem(Mesh->GetAssetPathFileName()) == GetImportedFbxStem(AnimationPath);
}

static bool IsFbxPath(const FString& Path)
{
    std::filesystem::path FsPath(FPaths::ToWide(FPaths::Normalize(Path)));
    std::wstring Extension = FsPath.extension().wstring();
    std::transform(Extension.begin(), Extension.end(), Extension.begin(), ::towlower);
    return Extension == L".fbx";
}
}

FAnimationViewer::~FAnimationViewer()
{
    Shutdown();
}

void FAnimationViewer::Init(
    FWindowsWindow* InWindow,
    UEditorEngine* InEditor,
    UWorld* InWorld,
    FSelectionManager* InSelectionManager)
{
    FEditorViewer::Init(InWindow, InEditor, InWorld, InSelectionManager);

    Viewport.SetClient(&Client);
    Client.Initialize(InWindow, InEditor);
    Client.SetWorld(InWorld);
    Client.SetGizmo(InSelectionManager->GetGizmo());
    Client.SetSelectionManager(InSelectionManager);
    Client.SetSceneEditingShortcutsEnabled(false);

    Client.SetViewport(&Viewport);
    Client.SetState(&Viewport.GetState());

    Client.SetViewportType(EEditorViewportType::EVT_Perspective);
    Client.CreateCamera();
    Client.ApplyCameraMode();

    PreviewActor = InWorld->SpawnActor<ASkeletalMeshActor>();
    if (PreviewActor)
    {
        PreviewActor->InitDefaultComponents();
        PreviewActor->SetFName(FName("AnimationViewerActor"));
        PreviewActor->SetActorLocation(FVector(0.0f, 0.0f, 0.0f));
    }

    InWorld->SyncSpatialIndex();
}

void FAnimationViewer::Shutdown()
{
    if (PreviewAnimInstance)
    {
        UObjectManager::Get().DestroyObject(PreviewAnimInstance);
        PreviewAnimInstance = nullptr;
    }
    CurrentAnimationSequence = nullptr;
    PreviewActor = nullptr;
    Viewport.SetClient(nullptr);
}

void FAnimationViewer::Tick(float DeltaTime)
{
    Client.Tick(DeltaTime);
}

void FAnimationViewer::ChangeTarget(const FString& InFileName)
{
    FEditorViewer::ChangeTarget(InFileName);
    ClearAnimationSequence();
}

bool FAnimationViewer::SetPreviewSkeletalMesh(const FString& SkeletalMeshPath)
{
    if (!PreviewActor)
    {
        return false;
    }

    USkeletalMesh* Mesh = FResourceManager::Get().LoadSkeletalMesh(SkeletalMeshPath);
    if (!Mesh)
    {
        return false;
    }

    USkeletalMeshComponent* SkelComp = PreviewActor->GetSkeletalMeshComponent();
    if (!SkelComp)
    {
        return false;
    }

    PreviewSkeletalMeshPath = SkeletalMeshPath;
    SkelComp->SetSkeletalMesh(Mesh);
    if (UWorld* World = Client.GetFocusedWorld())
    {
        World->RebuildSpatialIndex();
    }
    return true;
}

bool FAnimationViewer::IsAnimationSequenceCompatible(const FString& AnimationPath) const
{
    USkeletalMeshComponent* SkelMeshComp = PreviewActor ? PreviewActor->GetSkeletalMeshComponent() : nullptr;
    USkeletalMesh* SkelMesh = SkelMeshComp ? SkelMeshComp->GetSkeletalMesh() : nullptr;
    if (!SkelMesh)
    {
        return false;
    }

    UAnimationSequence* Sequence = FResourceManager::Get().LoadAnimationSequence(AnimationPath);
    return IsAnimationCompatibleWithMesh(SkelMesh, Sequence, AnimationPath);
}

bool FAnimationViewer::SetAnimationSequence(const FString& AnimationPath)
{
    UAnimationSequence* Sequence = FResourceManager::Get().LoadAnimationSequence(AnimationPath);
    if (!Sequence)
    {
        return false;
    }
    if (!PreviewActor)
    {
        return false;
    }

    const FString& SourceImportPath = Sequence->GetSourceImportPath();
    if (!SourceImportPath.empty() && !SetPreviewSkeletalMesh(SourceImportPath))
    {
        if (!IsFbxPath(SourceImportPath))
        {
            return false;
        }

        FResourceManager::Get().ImportFbxAssets(SourceImportPath);
        if (!SetPreviewSkeletalMesh(SourceImportPath))
        {
            return false;
        }
    }

    USkeletalMeshComponent* SkelComp = PreviewActor->GetSkeletalMeshComponent();
    if (!SkelComp || !SkelComp->GetSkeletalMesh())
    {
        return false;
    }

    if (!IsAnimationCompatibleWithMesh(SkelComp->GetSkeletalMesh(), Sequence, AnimationPath))
    {
        FString AnimationSkeletonPath = ResolveSkeletonPathFromAnimation(Sequence);
        if (AnimationSkeletonPath.empty())
        {
            AnimationSkeletonPath = "<None>";
        }

        UE_LOG_WARNING(
            "[AnimationViewer] RejectAnimationSequence | PreviewMesh=%s | MeshSkeleton=%s | Animation=%s | AnimationSkeleton=%s",
            GetSkeletalMeshPath(SkelComp->GetSkeletalMesh()),
            ResolveSkeletonPathFromMesh(SkelComp->GetSkeletalMesh()).c_str(),
            AnimationPath.c_str(),
            AnimationSkeletonPath.c_str());
        return false;
    }

    if (!PreviewAnimInstance)
    {
        PreviewAnimInstance = UObjectManager::Get().CreateObject<UAnimSingleNodeInstance>();
        PreviewAnimInstance->SetLooping(bLooping);
        PreviewAnimInstance->SetPlayRate(PlayRate);
        PreviewAnimInstance->SetReversePlay(bReversePlay);
    }

    CurrentAnimationSequence = Sequence;
    AnimationSequencePath = AnimationPath;

    SkelComp->SetAnimInstance(PreviewAnimInstance);
    PreviewAnimInstance->SetSequence(Sequence);
    PreviewAnimInstance->SetLooping(bLooping);
    PreviewAnimInstance->SetPlayRate(PlayRate);
    PreviewAnimInstance->SetReversePlay(bReversePlay);
    PreviewAnimInstance->SetCurrentTime(0.0f);
    return true;
}

void FAnimationViewer::ClearAnimationSequence()
{
    CurrentAnimationSequence = nullptr;
    AnimationSequencePath.clear();

    if (PreviewAnimInstance)
    {
        PreviewAnimInstance->Stop();
        PreviewAnimInstance->SetSequence(nullptr);
    }

    USkeletalMeshComponent* SkelComp = PreviewActor ? PreviewActor->GetSkeletalMeshComponent() : nullptr;
    if (SkelComp)
    {
        SkelComp->SetAnimInstance(nullptr);
        SkelComp->ResetToBindPose();
    }
}

void FAnimationViewer::PlayAnimation()
{
    if (PreviewAnimInstance && CurrentAnimationSequence)
    {
        PreviewAnimInstance->Play();
    }
}

void FAnimationViewer::PauseAnimation()
{
    if (PreviewAnimInstance)
    {
        PreviewAnimInstance->Pause();
    }
}

void FAnimationViewer::StopAnimation()
{
    if (PreviewAnimInstance)
    {
        PreviewAnimInstance->Stop();
    }

    USkeletalMeshComponent* SkelComp = PreviewActor ? PreviewActor->GetSkeletalMeshComponent() : nullptr;
    if (SkelComp)
    {
        SkelComp->ResetToBindPose();
    }
}

bool FAnimationViewer::SaveAnimation()
{
    if (!CurrentAnimationSequence)
    {
        return false;
    }

    const bool bSaved = FResourceManager::Get().SaveAnimationSequence(CurrentAnimationSequence);
    if (bSaved)
    {
        CurrentAnimationSequence->ResetDirty();
    }
    return bSaved;
}

void FAnimationViewer::SetLooping(bool bInLooping)
{
    bLooping = bInLooping;
    if (PreviewAnimInstance)
    {
        PreviewAnimInstance->SetLooping(bLooping);
    }
}

bool FAnimationViewer::IsLooping() const
{
    return PreviewAnimInstance ? PreviewAnimInstance->IsLooping() : bLooping;
}

void FAnimationViewer::SetPlayRate(float InPlayRate)
{
    if (InPlayRate < 0.0f)
    {
        bReversePlay = true;
    }

    PlayRate = std::max(0.001f, std::fabs(InPlayRate));
    if (PreviewAnimInstance)
    {
        PreviewAnimInstance->SetPlayRate(PlayRate);
        PreviewAnimInstance->SetReversePlay(bReversePlay);
    }
}

float FAnimationViewer::GetPlayRate() const
{
    return PreviewAnimInstance ? PreviewAnimInstance->GetPlayRate() : PlayRate;
}

void FAnimationViewer::SetReversePlay(bool bInReversePlay)
{
    bReversePlay = bInReversePlay;
    if (PreviewAnimInstance)
    {
        PreviewAnimInstance->SetReversePlay(bReversePlay);
    }
}

bool FAnimationViewer::IsReversePlaying() const
{
    return PreviewAnimInstance ? PreviewAnimInstance->IsReversePlaying() : bReversePlay;
}

void FAnimationViewer::SetAnimationTime(float Time)
{
    if (PreviewAnimInstance && CurrentAnimationSequence)
    {
        PreviewAnimInstance->SetCurrentTime(Time);
    }
}

float FAnimationViewer::GetAnimationTime() const
{
    return PreviewAnimInstance ? PreviewAnimInstance->GetCurrentTime() : 0.0f;
}

float FAnimationViewer::GetAnimationLength() const
{
    return CurrentAnimationSequence ? CurrentAnimationSequence->GetPlayLength() : 0.0f;
}

bool FAnimationViewer::IsAnimationPlaying() const
{
    return PreviewAnimInstance && PreviewAnimInstance->IsPlaying();
}

bool FAnimationViewer::IsAnimationPaused() const
{
    return PreviewAnimInstance && PreviewAnimInstance->IsPaused();
}
