#pragma once

#include "Component/Primitive/SkinnedMeshComponent.h"
#include "Animation/AnimationMode.h"
#include "Cloth/SkeletalClothRuntime.h"
#include "Animation/Skeleton/SkeletonTypes.h"
#include "Object/Ptr/SubclassOf.h"
#include "Object/Ptr/SoftObjectPtr.h"
#include "Object/Ptr/WeakObjectPtr.h"
#include "Physics/PhysicsAssetInstance.h"
#include <memory>

#include "Source/Engine/Component/Primitive/SkeletalMeshComponent.generated.h"

class UAnimInstance;
class UAnimSingleNodeInstance;
class UAnimSequenceBase;
class UClass;
class UPhysicsAsset;

UENUM()
enum class ERagdollRecoveryPhase : uint8
{
    None,
    BlendOutFromPhysics,
    PlayingStandUp,
    HoldingFinalPose,
    Completed,
};

UENUM()
enum class ERagdollStandUpType : uint8
{
    Unknown,
    FaceUp,
    FaceDown,
};

UENUM()
enum class ERagdollMode : uint8
{
    None,
    FullBody,
    Partial,
};

UENUM()
enum class EPartialRagdollPreset : uint8
{
    UpperBody,
    LeftArm,
    RightArm,
    HeadNeck,
};

UENUM()
enum class EPartialRagdollPhase : uint8
{
    None,
    BlendingIn,
    Active,
    BlendingOut,
};

struct FPartialRagdollSelection
{
    FName RootBoneName = FName::None;
    bool bIncludeDescendants = true;

    bool IsValid() const
    {
        return RootBoneName != FName::None;
    }
};

struct FPartialRagdollRequest
{
    EPartialRagdollPreset Preset = EPartialRagdollPreset::UpperBody;
    float HoldTimeOverride = -1.0f;
    bool bAllowRefreshIfSamePreset = true;
    bool bAllowWhileMoving = true;

    bool HasHoldTimeOverride() const
    {
        return HoldTimeOverride >= 0.0f;
    }
};

struct FPartialRagdollHitReactionRequest
{
    EPartialRagdollPreset PreferredPreset = EPartialRagdollPreset::UpperBody;
    FName HitBoneName = FName::None;
    FVector HitWorldLocation = FVector::ZeroVector;
    FVector HitWorldDirection = FVector::ZeroVector;
    float Strength = 0.5f;
    float HoldTimeOverride = -1.0f;
    bool bUsePreferredPreset = false;
    bool bAllowEscalationToFullBody = false;

    bool HasHitBone() const
    {
        return HitBoneName != FName::None;
    }

    bool HasHoldTimeOverride() const
    {
        return HoldTimeOverride >= 0.0f;
    }
};

struct FRagdollImpulseRequest
{
    FName HitBoneName = FName::None;
    FVector WorldLocation = FVector::ZeroVector;
    FVector WorldDirection = FVector::ZeroVector;
    float Strength = 0.5f;
    float HoldTimeOverride = -1.0f;
    EPartialRagdollPreset PreferredPreset = EPartialRagdollPreset::UpperBody;
    bool bUsePreferredPreset = false;
    bool bAllowEscalationToFullBody = true;
    bool bAllowWhileMoving = true;

    bool HasHoldTimeOverride() const
    {
        return HoldTimeOverride >= 0.0f;
    }
};

struct FRagdollShockwaveRequest
{
    FVector Origin = FVector::ZeroVector;
    float Radius = 300.0f;
    float Strength = 1.0f;
    float MinStrength = 0.0f;
    float HoldTimeOverride = -1.0f;
    EPartialRagdollPreset PreferredPreset = EPartialRagdollPreset::UpperBody;
    bool bUsePreferredPreset = false;
    bool bAllowEscalationToFullBody = true;
    bool bAllowWhileMoving = true;

    bool HasHoldTimeOverride() const
    {
        return HoldTimeOverride >= 0.0f;
    }
};

#define KRAFTON_SKELETAL_MESH_RAGDOLL_TYPES_DEFINED 1
#include "Component/Primitive/RagdollReactionPolicy.h"
#undef KRAFTON_SKELETAL_MESH_RAGDOLL_TYPES_DEFINED

// SkeletalMesh 전용 render proxy만 제공하는 얇은 wrapper.
// Skinning/bone/material/bounds 상태는 모두 USkinnedMeshComponent가 소유한다.
UCLASS()
class USkeletalMeshComponent : public USkinnedMeshComponent
{
public:
	GENERATED_BODY()
	USkeletalMeshComponent() = default;
	~USkeletalMeshComponent() override;

    // Render access 섹션: SceneProxy
    FPrimitiveSceneProxy* CreateSceneProxy() override;
    ESkinningMode GetEffectiveSkinningMode() const override;
    bool HasEnabledClothSections() const;
    void TickClothSimulationForEditorPreview(float DeltaTime);
    void ResetClothSimulation();
    void SetClothPreviewWindOverride(bool bEnable, const FVector& WorldWindVelocity);
    void BeginPlay() override;

    // Mesh 가 바뀌면 AnimInstance 도 새 SkeletalMesh 기준으로 재구성해야 하므로 override.
    UFUNCTION(Callable, Category="Mesh")
    void SetSkeletalMesh(USkeletalMesh* InMesh) override;

    UFUNCTION(Callable, Category="Physics")
    void SetPhysicsAssetOverride(UPhysicsAsset* InPhysicsAsset);
    UFUNCTION(Callable, Exec, Category="Physics")
    void SetPhysicsAssetOverridePath(const FString& InPath);
    UFUNCTION(Pure, Category="Physics")
    UPhysicsAsset* GetPhysicsAssetOverride() const;
    // This is the single selection entry point for ragdoll work:
    // component override -> mesh default -> skeleton default.
    UFUNCTION(Pure, Category="Physics")
    UPhysicsAsset* GetEffectivePhysicsAsset() const;
    bool ResolvePhysicsAssetOverride();
    void ClearPhysicsAssetOverride();
    void ResetRagdollRuntimeState();
    void OnPhysicsAssetChanged();
    FPhysicsAssetInstance* GetPhysicsAssetInstance() const;
    FPhysicsAssetInstance* GetOrCreatePhysicsAssetInstance();
    void DestroyPhysicsAssetInstance();
    // Higher-level ragdoll controls hide instance/body lifecycle details from gameplay
    // code and keep this component as the policy owner.
    UFUNCTION(Callable, Category="Physics")
    bool EnableRagdollPhysics();
    // Gameplay should prefer TriggerPartialRagdoll() so preset resolution and request
    // policy remain centralized, while these low-level entry points stay available for
    // tests or specialized callers.
    bool EnablePartialRagdoll(const FPartialRagdollSelection& Selection);
    UFUNCTION(Callable, Category="Physics")
    bool EnablePartialRagdoll(const FName& RootBoneName);
    bool ApplyRagdollReaction(const FRagdollReactionRequest& Request);
    bool ApplyRagdollImpulse(const FRagdollImpulseRequest& Request);
    bool ApplyRagdollShockwave(const FRagdollShockwaveRequest& Request);
    bool TriggerPartialRagdoll(const FPartialRagdollRequest& Request);
    bool TriggerPartialRagdollHitReaction(const FPartialRagdollHitReactionRequest& Request);
    UFUNCTION(Callable, Category="Physics")
    void DisableRagdollPhysics();
    UFUNCTION(Callable, Category="Physics")
    void DisablePartialRagdoll();
    UFUNCTION(Callable, Category="Physics")
    bool BeginRagdollRecovery();
    UFUNCTION(Pure, Category="Physics")
    bool IsRagdollActive() const;
    UFUNCTION(Pure, Category="Physics")
    bool IsPartialRagdollActive() const;
    bool TryGetRagdollRepresentativeLocation(FVector& OutWorldLocation) const;
    UFUNCTION(Pure, Category="Physics")
    ERagdollMode GetRagdollMode() const { return ActiveRagdollMode; }
    UFUNCTION(Pure, Category="Physics")
    EPartialRagdollPhase GetPartialRagdollPhase() const { return PartialRagdollPhase; }
    UFUNCTION(Pure, Category="Physics")
    float GetPartialRagdollHoldRemaining() const { return PartialRagdollHoldRemaining; }
    UFUNCTION(Pure, Category="Physics")
    bool IsPartialRagdollBlendOutPending() const { return bPendingPartialRagdollBlendOut; }
    UFUNCTION(Pure, Category="Physics")
    FName GetActivePartialRagdollRootBoneName() const { return ActivePartialRagdollSelection.RootBoneName; }
    UFUNCTION(Pure, Category="Physics")
    EPartialRagdollPreset GetLastPartialHitReactionPreset() const { return LastPartialHitReactionPreset; }
    UFUNCTION(Pure, Category="Physics")
    FName GetLastPartialHitReactionHitBoneName() const { return LastPartialHitReactionHitBoneName; }
    UFUNCTION(Pure, Category="Physics")
    FName GetLastPartialHitReactionRootBoneName() const { return LastPartialHitReactionRootBoneName; }
    UFUNCTION(Pure, Category="Physics")
    FName GetLastPartialHitReactionTargetBoneName() const { return LastPartialHitReactionTargetBoneName; }
    UFUNCTION(Pure, Category="Physics")
    float GetLastPartialHitReactionStrength() const { return LastPartialHitReactionStrength; }
    UFUNCTION(Pure, Category="Physics")
    float GetLastPartialHitReactionHoldTime() const { return LastPartialHitReactionHoldTime; }
    UFUNCTION(Pure, Category="Physics")
    float GetLastPartialHitReactionImpulseMagnitude() const { return LastPartialHitReactionImpulseMagnitude; }
    UFUNCTION(Pure, Category="Physics")
    FVector GetLastPartialHitReactionDirection() const { return LastPartialHitReactionDirection; }
    UFUNCTION(Pure, Category="Physics")
    bool WasLastPartialHitReactionEscalationCandidate() const { return bLastPartialHitReactionEscalationCandidate; }
    ERagdollReactionEventKind GetLastRagdollReactionEventKind() const { return LastRagdollReactionEventKind; }
    ERagdollReactionType GetLastRagdollReactionType() const { return LastRagdollReactionType; }
    ERagdollReactionDecisionReason GetLastRagdollReactionDecisionReason() const { return LastRagdollReactionDecisionReason; }
    bool IsPartialRagdollSelfSuppressionActive() const;
    UFUNCTION(Pure, Category="Physics")
    bool IsRecoveringFromRagdoll() const { return RecoveryPhase != ERagdollRecoveryPhase::None; }
    UFUNCTION(Pure, Category="Physics")
    int32 GetLiveRagdollBodyCount() const;
    UFUNCTION(Pure, Category="Physics")
    int32 GetLiveRagdollConstraintCount() const;
    UFUNCTION(Pure, Category="Physics")
    UPhysicsAsset* GetActivePhysicsAsset() const;
    UFUNCTION(Pure, Category="Physics")
    float GetPhysicsAssetBlendWeight() const { return PhysicsPoseBlendWeight; }
    UFUNCTION(Callable, Category="Physics")
    bool CreatePhysicsAssetInstanceBodies();
    UFUNCTION(Callable, Category="Physics")
    void DestroyPhysicsAssetInstanceBodies();
    void BeginPhysicsAssetPosePreview(bool bFullBlend = true);
    UFUNCTION(Callable, Category="Physics")
    void SetUsePhysicsAssetPose(bool bEnable);
    UFUNCTION(Pure, Category="Physics")
    bool IsUsingPhysicsAssetPose() const { return bUsePhysicsAssetPose; }
    // Physics pose now blends against the current animation pose so ragdoll entry/exit can
    // transition smoothly without changing runtime ownership boundaries.
    bool ApplyPhysicsAssetPose();

    // SingleNode 재생 편의 API.
    UFUNCTION(Callable, Category="Animation")
    void PlayAnimation(UAnimSequenceBase* NewAnimToPlay, bool bLooping);
    UFUNCTION(Callable, Exec, Category="Animation")
    bool PlayAnimationByPath(const FString& AnimationPath, bool bLooping = true);
    UFUNCTION(Callable, Category="Animation")
    void StopAnimation();

    // Animation 섹션: Mode 에 따라 AnimInstance 의 생성/파기를 컴포넌트가 책임진다.
    //   - None              : AnimInstance 미생성. BoneEdit 만 적용.
    //   - AnimationSingleNode: UAnimSingleNodeInstance 자동 생성, AnimationData 로 구동.
    //   - AnimationCustom   : AnimInstanceClass 가 가리키는 자식 클래스를 FObjectFactory 로 인스턴스화.
    UFUNCTION(Callable, Exec, Category="Animation")
    void SetAnimationMode(EAnimationMode InMode);
    UFUNCTION(Pure, Category="Animation")
    EAnimationMode GetAnimationMode() const { return AnimationMode; }

    // SingleNode 모드용 헬퍼. Custom 모드에선 무시 (자체 인스턴스가 자체 시퀀스를 관리).
    UFUNCTION(Callable, Category="Animation")
    void SetAnimation(UAnimSequenceBase* InAsset);
    UFUNCTION(Callable, Exec, Category="Animation")
    bool SetAnimationByPath(const FString& AnimationPath);
    UFUNCTION(Pure, Category="Animation")
    bool CanUseAnimation(UAnimSequenceBase* InAsset) const;
    UFUNCTION(Pure, Category="Animation")
    UAnimSequenceBase* GetAnimation() const { return AnimationData.AnimToPlay.Get(); }
    UFUNCTION(Callable, Exec, Category="Animation")
    void SetPlayRate(float InRate);
    UFUNCTION(Callable, Exec, Category="Animation")
    void SetLooping(bool bInLoop);
    UFUNCTION(Callable, Exec, Category="Animation")
    void SetPlaying(bool bInPlay);
    const FSingleAnimationPlayData& GetAnimationData() const { return AnimationData; }

    // Custom 모드용. 클래스 변경 시 다음 InitializeAnimation 에서 재인스턴스화.
    // 슬롯은 TSubclassOf<UAnimInstance> — 잘못된 클래스 대입은 nullptr 로 흡수.
    UFUNCTION(Callable, Category="Animation")
    void SetAnimInstanceClass(UClass* InClass);
    UFUNCTION(Pure, Category="Animation")
    UClass* GetAnimInstanceClass() const { return AnimInstanceClass.Get(); }

    // 외부에서 직접 만든 인스턴스 주입 (테스트 / 특수 케이스). Mode 와 무관하게 즉시 교체.
    UFUNCTION(Callable, Category="Animation")
    void SetAnimInstance(UAnimInstance* InInstance);
    UFUNCTION(Pure, Category="Animation")
    UAnimInstance* GetAnimInstance() const { return AnimInstance; }

    // SingleNode 모드에서 현재 자동 생성된 노드를 반환한다. NodeName 은 현재 단일 노드 구조에서는 무시한다.
    UFUNCTION(Pure, Category="Animation")
    UAnimSingleNodeInstance* GetAnimNodeInstance(FName NodeName) const;

    // Mode/Class/SkeletalMesh 변경 후 일관성 재정렬. SetSkeletalMesh override 안에서 자동 호출됨.
    void InitializeAnimation();
    void ClearAnimInstance();

    // Editor / 직렬화 통합.
    void GetEditableProperties(TArray<FPropertyValue>& OutProps) override;
    void PostEditProperty(const char* PropertyName) override;
    void Serialize(FArchive& Ar) override;
    void BeginDestroy() override;

protected:
    // 매 프레임 AnimInstance 평가 → 결과 포즈를 SetBoneLocalTransforms 로 푸시.
    // 이 경로가 CPU skinning 과 bounds dirty 를 한 번에 처리한다.
    void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction& ThisTickFunction) override;

    bool EvaluateAnimInstance(float DeltaTime);
    void TickClothSimulation(float DeltaTime);

private:
    FRagdollReactionContext BuildRagdollReactionContext(const FRagdollReactionRequest& Request) const;
    bool ExecuteRagdollReactionDecision(
        const FRagdollReactionRequest& Request,
        const FRagdollReactionContext& Context,
        const FRagdollReactionDecision& Decision);
    bool ApplyImpulseForRagdollReaction(
        const FRagdollReactionRequest& Request,
        const FRagdollReactionDecision& Decision,
        const FName& PartialRootBoneName);
    void RefreshRagdollReactionTuning();
    void UpdateLastRagdollReactionDiagnostics(
        const FRagdollReactionRequest& Request,
        const FRagdollReactionDecision& Decision);
    bool ShouldAdvanceAnimationDuringTick() const;
    bool ShouldBlockExternalAnimationControl() const;
    bool CaptureRagdollPoseBaseline();
    void ClearRagdollPoseBaseline();
    bool BuildPartialRagdollBoneMasks(const FPartialRagdollSelection& Selection);
    bool BuildPartialRagdollSelectionFromPreset(EPartialRagdollPreset Preset, FPartialRagdollSelection& OutSelection) const;
    bool ResolvePartialRagdollPresetFromHitBone(const FName& HitBoneName, EPartialRagdollPreset& OutPreset) const;
    void ClearPartialRagdollState();
    bool IsSamePartialRagdollSelection(const FPartialRagdollSelection& Selection) const;
    void BeginPartialRagdollBlendOut();
    void ResetPhysicsPoseBlendState();
    void UpdatePhysicsPoseBlend(float DeltaTime);
    void ResetRagdollRecoveryState();
    bool CaptureRagdollRecoverySnapshot();
    ERagdollStandUpType EvaluateRagdollRecoveryOrientation() const;
    bool CanUseStandUpAnimation(UAnimSequenceBase* InAsset) const;
    UAnimSequenceBase* SelectStandUpAnimation();
    bool StartStandUpAnimation();
    bool IsStandUpAnimationFinished() const;
    void RestorePostRecoveryAnimationState();
    void FinishRagdollRecovery();
    void LoadAnimationFromPath();
    bool CanUsePhysicsAsset(UPhysicsAsset* InPhysicsAsset, FSkeletonCompatibilityReport* OutReport = nullptr) const;

protected:
    // Animation 런타임 상태.
    UPROPERTY(Edit, Save, Category="Animation", DisplayName="Animation Mode", Enum=EAnimationMode)
    EAnimationMode             AnimationMode = EAnimationMode::None;
    UPROPERTY(Edit, Save, Category="Animation", DisplayName="Animation Data", Type=Struct)
    FSingleAnimationPlayData   AnimationData;
    UPROPERTY(Edit, Save, Category="Animation", DisplayName="Anim Instance Class", Type=ClassRef, AllowedClass=UAnimInstance)
    TSubclassOf<UAnimInstance> AnimInstanceClass;
    UPROPERTY(Save, Instanced, Category="Animation", DisplayName="Anim Instance", Type=ObjectRef, AllowedClass=UAnimInstance)
    UAnimInstance*             AnimInstance  = nullptr;

    // Components own per-instance overrides and runtime-instance lifecycle only; low-level
    // rigid body/constraint handles stay inside FPhysicsAssetInstance.
    UPROPERTY(Edit, Save, Category="Physics", DisplayName="Physics Asset Override", AssetType="PhysicsAsset")
    FSoftObjectPtr PhysicsAssetOverridePath = "None";
    UPROPERTY(Edit, Save, Category="Physics|Ragdoll", DisplayName="Ragdoll Blend In Time", Min=0.0f, Max=0.0f, Speed=0.01f)
    float RagdollBlendInTime = 0.15f;
    UPROPERTY(Edit, Save, Category="Physics|Ragdoll", DisplayName="Ragdoll Recovery Blend Out Time", Min=0.0f, Max=0.0f, Speed=0.01f)
    float RagdollRecoveryBlendOutTime = 0.3f;
    UPROPERTY(Edit, Save, Category="Physics|Ragdoll", DisplayName="Ragdoll First Valid Pose Blend In Time", Min=0.0f, Max=0.0f, Speed=0.01f)
    float RagdollFirstValidPoseBlendInTime = 0.08f;
    UPROPERTY(Edit, Save, Category="Physics|Ragdoll", DisplayName="Ragdoll Completion Hold Time", Min=0.0f, Max=0.0f, Speed=0.01f)
    float RagdollCompletionHoldTime = 0.05f;
    UPROPERTY(Edit, Save, Category="Physics|Ragdoll", DisplayName="Ragdoll Fallback Hold Time", Min=0.0f, Max=0.0f, Speed=0.01f)
    float RagdollFallbackHoldTime = 0.12f;
    UPROPERTY(Edit, Save, Category="Physics|Partial Ragdoll", DisplayName="Partial Ragdoll Blend In Time", Min=0.0f, Max=0.0f, Speed=0.01f)
    float PartialRagdollBlendInTime = 0.08f;
    UPROPERTY(Edit, Save, Category="Physics|Partial Ragdoll", DisplayName="Partial Ragdoll Blend Out Time", Min=0.0f, Max=0.0f, Speed=0.01f)
    float PartialRagdollBlendOutTime = 0.14f;
    UPROPERTY(Edit, Save, Category="Physics|Partial Ragdoll", DisplayName="Partial Ragdoll Hold Time", Min=0.0f, Max=0.0f, Speed=0.01f)
    float PartialRagdollHoldTime = 0.18f;
    UPROPERTY(Edit, Save, Category="Physics|Partial Ragdoll", DisplayName="Partial First Valid Pose Blend In Time", Min=0.0f, Max=0.0f, Speed=0.01f)
    float PartialFirstValidPoseBlendInTime = 0.04f;
    UPROPERTY(Edit, Save, Category="Physics|Ragdoll Reaction", DisplayName="Direct Hit Full Body Threshold", Min=0.0f, Max=1.0f, Speed=0.01f)
    float DirectHitFullBodyThreshold = 0.85f;
    UPROPERTY(Edit, Save, Category="Physics|Ragdoll Reaction", DisplayName="Partial Escalation Threshold", Min=0.0f, Max=1.0f, Speed=0.01f)
    float PartialEscalationThreshold = 0.80f;
    UPROPERTY(Edit, Save, Category="Physics|Ragdoll Reaction", DisplayName="Shockwave Full Body Threshold", Min=0.0f, Max=1.0f, Speed=0.01f)
    float ShockwaveFullBodyThreshold = 0.90f;
    UPROPERTY(Edit, Save, Category="Physics|Ragdoll Reaction", DisplayName="Shockwave Impulse Threshold", Min=0.0f, Max=1.0f, Speed=0.01f)
    float ShockwaveImpulseThreshold = 0.45f;
    UPROPERTY(Edit, Save, Category="Physics|Ragdoll Reaction", DisplayName="Recovery Full Body Threshold", Min=0.0f, Max=1.0f, Speed=0.01f)
    float RecoveryFullBodyThreshold = 0.90f;
    UPROPERTY(Edit, Save, Category="Physics|Ragdoll Reaction", DisplayName="Recovery Impulse Threshold", Min=0.0f, Max=1.0f, Speed=0.01f)
    float RecoveryImpulseThreshold = 0.55f;
    UPROPERTY(Edit, Save, Category="Physics|Ragdoll Launch", DisplayName="Base Impulse Magnitude", Min=0.0f, Max=5000.0f, Speed=1.0f)
    float RagdollBaseImpulseMagnitude = 120.0f;
    UPROPERTY(Edit, Save, Category="Physics|Ragdoll Launch", DisplayName="Additional Impulse Magnitude", Min=0.0f, Max=5000.0f, Speed=1.0f)
    float RagdollAdditionalImpulseMagnitude = 240.0f;
    UPROPERTY(Edit, Save, Category="Physics|Ragdoll Launch", DisplayName="Overdrive Impulse Magnitude Scale", Min=0.0f, Max=10000.0f, Speed=1.0f)
    float RagdollOverdriveImpulseMagnitudeScale = 600.0f;
    UPROPERTY(Edit, Save, Category="Physics|Ragdoll Launch", DisplayName="Full Body Launch Impulse Magnitude", Min=0.0f, Max=10000.0f, Speed=1.0f)
    float FullBodyLaunchImpulseMagnitude = 900.0f;
    // FaceDown means prone/front get-up. FaceUp means supine/back get-up.
    UPROPERTY(Edit, Save, Category="Physics|Ragdoll", DisplayName="Front Stand Up Animation", AssetType="UAnimSequence")
    FSoftObjectPtr FrontStandUpAnimationPath = "None";
    UPROPERTY(Edit, Save, Category="Physics|Ragdoll", DisplayName="Back Stand Up Animation", AssetType="UAnimSequence")
    FSoftObjectPtr BackStandUpAnimationPath = "None";
    mutable TWeakObjectPtr<UPhysicsAsset> PhysicsAssetOverride;
    std::unique_ptr<FPhysicsAssetInstance> PhysicsAssetInstance;
    std::unique_ptr<FSkeletalClothRuntime> ClothRuntime;
    FVector ClothPreviewWorldWindVelocity = FVector::ZeroVector;
    bool bClothPreviewWindOverride = false;
    bool bUsePhysicsAssetPose = false;
    bool bAllowInternalRagdollAnimationControl = false;
    ERagdollMode ActiveRagdollMode = ERagdollMode::None;
    FPartialRagdollSelection ActivePartialRagdollSelection;
    int32 PartialRootBoneIndex = -1;
    int32 PartialBoundaryParentBoneIndex = -1;
    TArray<uint8> PartialSimulatedBoneMask;
    TArray<uint8> PartialPhysicsApplyBoneMask;
    EPartialRagdollPhase PartialRagdollPhase = EPartialRagdollPhase::None;
    bool bPendingPartialRagdollBlendOut = false;
    float PendingPartialRagdollHoldTimeOverride = -1.0f;
    float PartialRagdollHoldRemaining = 0.0f;
    FRagdollReactionTuning RagdollReactionTuning;
    ERagdollReactionEventKind LastRagdollReactionEventKind = ERagdollReactionEventKind::DirectHit;
    ERagdollReactionType LastRagdollReactionType = ERagdollReactionType::None;
    ERagdollReactionDecisionReason LastRagdollReactionDecisionReason = ERagdollReactionDecisionReason::None;
    EPartialRagdollPreset LastPartialHitReactionPreset = EPartialRagdollPreset::UpperBody;
    FName LastPartialHitReactionHitBoneName = FName::None;
    FName LastPartialHitReactionRootBoneName = FName::None;
    FName LastPartialHitReactionTargetBoneName = FName::None;
    float LastPartialHitReactionStrength = 0.0f;
    float LastPartialHitReactionHoldTime = 0.0f;
    float LastPartialHitReactionImpulseMagnitude = 0.0f;
    FVector LastPartialHitReactionDirection = FVector::ZeroVector;
    bool bLastPartialHitReactionEscalationCandidate = false;
    float PhysicsPoseBlendWeight = 0.0f;
    float TargetPhysicsPoseBlendWeight = 0.0f;
    TArray<FTransform> RagdollBaselineComponentSpacePose;
    TArray<FTransform> RagdollBaselineLocalPose;
    bool bHasReceivedValidPhysicsPose = false;
    float FirstValidPhysicsPoseBlendAlpha = 0.0f;
    float RecoveryCompletionHoldRemaining = 0.0f;
    // Recovery keeps blend-out and stand-up playback as separate phases so physics teardown
    // does not race against animation takeover.
    ERagdollRecoveryPhase RecoveryPhase = ERagdollRecoveryPhase::None;
    ERagdollStandUpType SelectedStandUpType = ERagdollStandUpType::Unknown;
    FTransform RecoveryPelvisWorldTransform;
    FTransform RecoveryChestWorldTransform;
    UAnimSequenceBase* SelectedStandUpAnimation = nullptr;
    bool bHasSavedPostRecoveryAnimationState = false;
    EAnimationMode SavedPostRecoveryAnimationMode = EAnimationMode::None;
    FSingleAnimationPlayData SavedPostRecoveryAnimationData;
    TSubclassOf<UAnimInstance> SavedPostRecoveryAnimInstanceClass;
};
