#pragma once

#include "Engine/Input/InputMapping.h"
#include "Audio/AudioSystem.h"
#include "Game/Input/BaseGameController.h"
#include "Game/Systems/CleaningGameTypes.h"
#include "Math/Vector.h"
#include "Render/Common/ViewTypes.h"

#include <functional>
#include <unordered_map>

class AActor;
struct FSceneView;
class FViewportCamera;
struct FViewportRect;
struct FCameraSnapshot;
class UCameraComponent;
class UCharacterMovementComponent;
class UPhysicsHandleComponent;
class UPrimitiveComponent;
class URigidBodyComponent;
class USceneComponent;
class UStaticMeshComponent;
class USubUVComponent;
class UWorld;

class FGamePlayerController : public IBaseGameController
{
public:
    FGamePlayerController();
    ~FGamePlayerController() override;

    void Tick(float DeltaTime) override;
    void LateTick(float DeltaTime);

    void OnMouseMove(float DeltaX, float DeltaY) override;
    void OnMouseMoveAbsolute(float X, float Y) override;
    void OnLeftMouseClick(float X, float Y) override;
    void OnLeftMouseDrag(float X, float Y) override;
    void OnLeftMouseDragEnd(float X, float Y) override;
    void OnLeftMouseButtonUp(float X, float Y) override;
    void OnRightMouseClick(float DeltaX, float DeltaY) override;
    void OnRightMouseDrag(float DeltaX, float DeltaY) override;
    void OnMiddleMouseDrag(float DeltaX, float DeltaY) override;
    void OnKeyPressed(int VK) override;
    void OnKeyDown(int VK) override;
    void OnKeyReleased(int VK) override;
    void OnWheelScrolled(float Notch) override;

    void SetPlayer(AActor* InPlayer);
    AActor* GetPlayer() const { return Player; }
    void SetWorld(UWorld* InWorld);

    void SetCamera(UCameraComponent* InCamera);
    UCameraComponent* GetCamera() const { return Camera; }

    void SetFreeCamera(FViewportCamera* InCamera);
    FViewportCamera* GetFreeCamera() const { return FreeCamera; }
    void InitializeFreeCameraFromSnapshot(const FCameraSnapshot& Snapshot);

    void SetMoveSpeed(float InMoveSpeed) { MoveSpeed = InMoveSpeed; }
    void SetRotateSensitivity(float InSensitivity) { RotateSensitivity = InSensitivity; }
    void SetToggleInputCaptureCallback(std::function<void()> Callback) { OnRequestToggleInputCapture = std::move(Callback); }
    void ClearToggleInputCaptureCallback() { OnRequestToggleInputCapture = nullptr; }
    void SetTogglePauseCallback(std::function<void()> Callback) { OnRequestTogglePause = std::move(Callback); }
    void ClearTogglePauseCallback() { OnRequestTogglePause = nullptr; }
    AActor* GetHoveredPickableActor() const { return HoveredPickableActor; }
    AActor* GetHeldNonCleaningToolActor() const;

    void BuildSceneView(FSceneView& OutView, const FViewportRect& ViewRect, EViewMode ViewMode) const;

private:
    void SetupDefaultInputMappings();
    void ApplyInputAxes();
    bool TryBeginCleaningUse();
    void EndCleaningUse();
    void StartCleaningLoopSound(const struct FCleaningToolData& ToolData);
    void StopCleaningLoopSound();
    void UpdateCleaningUseSound();
    void PlayNextSpongeCleaningSound();
    void TogglePickup();
    void NotifyPickedUp(AActor* PickedActor);
    void TryInspectHoveredItem();
    bool TryPlaceHeldItemInHoveredDecisionBox();
    UPhysicsHandleComponent* GetPhysicsHandle();
    UCharacterMovementComponent* GetCharacterMovement();
    void DestroyPhysicsHandle();
    void RefreshPawnComponents();
    void UpdateHoveredPickableActor();
    AActor* FindHoveredDecisionBoxActor(EItemDecisionBoxType& OutBoxType) const;
    bool GetActiveCameraFrame(FVector& OutLocation, FVector& OutForward) const;
    bool GetActiveCameraBasis(FVector& OutLocation, FVector& OutForward, FVector& OutRight, FVector& OutUp) const;
    void CaptureInitialRigidBodyRotations();
    void ResetHeldBodyRotationToInitial();
    bool BeginCleaningToolViewModel(const struct FCleaningToolData& ToolData);
    void EndCleaningToolViewModel();
    void UpdateCleaningToolViewModel(
        const struct FCleaningToolData& ToolData,
        const FVector& CameraLocalOffset,
        const FVector& CameraLocation,
        const FVector& CameraForward,
        const FVector& CameraRight);
    bool IsRuntimeWorld() const;
    void RotateActiveCamera(float DeltaX, float DeltaY);
    void MoveActiveCamera(const FVector& Direction, float Scale);
    void SyncFreeCameraAngles();
    void UpdateFreeCameraRotation();

private:
    UWorld* World = nullptr;
    AActor* Player = nullptr;
    UCameraComponent* Camera = nullptr;
    FViewportCamera* FreeCamera = nullptr;
    UPhysicsHandleComponent* PhysicsHandle = nullptr;
    UCharacterMovementComponent* CharacterMovement = nullptr;
    UStaticMeshComponent* CleaningToolViewModel = nullptr;
    AActor* HoveredPickableActor = nullptr;
    AActor* HoveredDecisionBoxActor = nullptr;
    EItemDecisionBoxType HoveredDecisionBoxType = EItemDecisionBoxType::KeepBox;
    FInputMappingContext InputMapping;
    std::unordered_map<URigidBodyComponent*, FVector> InitialRigidBodyRotations;
    TArray<UPrimitiveComponent*> HiddenCleaningToolPrimitives;
    TArray<bool> HiddenCleaningToolPrimitiveVisibility;
    TArray<USubUVComponent*> CleaningToolSubUVs;
    TArray<USceneComponent*> CleaningToolSubUVOriginalParents;

    float MoveSpeed = 10.0f;
    float RotateSensitivity = 0.15f;
    float FreeCameraYaw = 0.0f;
    float FreeCameraPitch = 0.0f;
    bool bFreeCameraInitialized = false;
    bool bInitialRigidBodyRotationsCaptured = false;
    bool bIsCleaningUseHeld = false;
    FAudioHandle CleaningLoopSoundHandle;
    FAudioHandle CleaningOneShotSoundHandle;
    int LastSpongeUseStrokeCycle = -1;
    std::function<void()> OnRequestToggleInputCapture;
    std::function<void()> OnRequestTogglePause;
};
