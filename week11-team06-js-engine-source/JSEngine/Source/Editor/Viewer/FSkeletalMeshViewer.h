#pragma once

#include "Core/Containers/Map.h"
#include "EditorViewer.h"
#include "Editor/Viewport/SkeletalMeshViewportClient.h"
#include "Object/FName.h"

class ASkeletalMeshActor;
class UEditorEngine;
class UWorld;
class FSelectionManager;
class FWindowsWindow;
class UStaticMeshComponent;

class FSkeletalMeshViewer : public FEditorViewer
{
public:
    ~FSkeletalMeshViewer() override;

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

    ASkeletalMeshActor* GetViewTarget() const { return ViewTarget; }
    ASkeletalMeshActor* GetTargetSkeletal() const { return ViewTarget; }
    void ClearViewTarget() { ViewTarget = nullptr; }
    void ClearTargetSkeletal() { ViewTarget = nullptr; }

    void SelectBone(int32 BoneIndex);
    void SelectSocket(int32 SocketIndex);
    void ClearSelection();
    void NotifySocketDeleted(int32 SocketIndex);
    bool HandleBonePick(float LocalX, float LocalY);
    bool TryPickBone(float LocalX, float LocalY, int32& OutBoneIndex) const;

    int32 GetSelectedBoneIndex() const { return SelectedBoneIndex; }
    int32 GetSelectedSocketIndex() const { return SelectedSocketIndex; }
    FVector& GetCachedBoneRotation() { return CachedRotation; }
    const FVector& GetCachedBoneRotation() const { return CachedRotation; }

    void SetSocketPreviewMesh(const FName& SocketName, const FString& StaticMeshPath);
    void ClearSocketPreview(const FName& SocketName);
    void ClearAllSocketPreviews();
    UStaticMeshComponent* FindPreviewMesh(const FName& SocketName) const;

private:
    FSkeletalMeshViewportClient Client;
    ASkeletalMeshActor* ViewTarget = nullptr;

    TMap<FName, UStaticMeshComponent*, FName::Hash> SocketPreviewMeshes;

    int32 SelectedBoneIndex = -1;
    int32 SelectedSocketIndex = -1;
    FVector CachedRotation;
};
