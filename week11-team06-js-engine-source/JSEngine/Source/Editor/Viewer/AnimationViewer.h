#pragma once

#include "EditorViewer.h"
#include "Editor/Viewport/SkeletalMeshViewportClient.h"

class ASkeletalMeshActor;
class UAnimSingleNodeInstance;
class UAnimationSequence;

class FAnimationViewer : public FEditorViewer
{
public:
    ~FAnimationViewer() override;

    void Init(FWindowsWindow* InWindow, UEditorEngine* InEditor, UWorld* InWorld, FSelectionManager* InSelectionManager) override;
    void Shutdown() override;
    void Tick(float DeltaTime) override;

    FEditorViewportClient* GetViewportClient() override { return &Client; }
    const FEditorViewportClient* GetViewportClient() const override { return &Client; }
    FSkeletalMeshViewportClient& GetClient() { return Client; }
    const FSkeletalMeshViewportClient& GetClient() const { return Client; }

    void SetRect(const FViewportRect& InRect) override
    {
        Viewport.SetRect(InRect);
        Client.SetViewportSize((float)InRect.Width, (float)InRect.Height);
    }

    void ChangeTarget(const FString& InFileName) override;

    ASkeletalMeshActor* GetPreviewActor() const { return PreviewActor; }
    bool SetPreviewSkeletalMesh(const FString& SkeletalMeshPath);
    const FString& GetPreviewSkeletalMeshPath() const { return PreviewSkeletalMeshPath; }

    bool IsAnimationSequenceCompatible(const FString& AnimationPath) const;
    bool SetAnimationSequence(const FString& AnimationPath);
    void ClearAnimationSequence();
    void PlayAnimation();
    void PauseAnimation();
    void StopAnimation();
    bool SaveAnimation();

    void SetAnimationTime(float Time);
    void SetLooping(bool bInLooping);
    bool IsLooping() const;
    void SetPlayRate(float InPlayRate);
    float GetPlayRate() const;
    void SetReversePlay(bool bInReversePlay);
    bool IsReversePlaying() const;

    const FString& GetAnimationSequencePath() const { return AnimationSequencePath; }
    UAnimationSequence* GetAnimationSequence() const { return CurrentAnimationSequence; }
    float GetAnimationTime() const;
    float GetAnimationLength() const;
    bool IsAnimationPlaying() const;
    bool IsAnimationPaused() const;

private:
    FSkeletalMeshViewportClient Client;
    ASkeletalMeshActor* PreviewActor = nullptr;
    UAnimSingleNodeInstance* PreviewAnimInstance = nullptr;
    UAnimationSequence* CurrentAnimationSequence = nullptr;
    FString AnimationSequencePath;
    FString PreviewSkeletalMeshPath;
    float PlayRate = 1.0f;
    bool bLooping = true;
    bool bReversePlay = false;
};
