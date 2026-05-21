#pragma once

#include "Object/Property.h"
#include "Component/SkinnedMeshComponent.h"

#include "USkeletalMeshComponent.generated.h"
class UAnimStateMachineAsset;
class UAnimInstance;
class UAnimSingleNodeInstance;
class UAnimStateMachineAssetInstance;
class UAnimationSequenceBase;
struct FAnimNotifyEvent;

enum class ESkeletalMeshAnimationMode : int32
{
    SingleAnimation = 0,
    AnimInstance = 1
};

enum class EAnimInstanceSource : int32
{
    EmptyStateMachine = 0,
    CustomCpp = 1,
    StateMachineAsset = 2
};

/**
 * @brief Unreal Engine 스타일에서는 skinned mesh가 skeleton을 이용하는 mesh를 표현하고,
 *        skeletal mesh는 실제로 actor에 붙어서 애니메이션을 붙일 수 있는 component로 사용되고 있으므로
 *        USkeletalMeshComponent 또한 해당 방식대로 우선은 얇게 유지.
 *        핵심 로직들은 대부분 USkinnedMeshComponent로 옮겼습니다.
 */
UCLASS()
class USkeletalMeshComponent : public USkinnedMeshComponent
{
public:
    GENERATED_BODY(USkeletalMeshComponent, USkinnedMeshComponent)
    USkeletalMeshComponent() = default;
    ~USkeletalMeshComponent() override;

    void PostDuplicate(UObject* Original) override;
    void BeginPlay() override;
    void TickComponent(float DeltaTime) override;

    EPrimitiveType GetPrimitiveType() const override { return EPrimitiveType::EPT_SkeletalMesh; }

    void Serialize(FArchive& Ar) override;
    void GetEditableProperties(TArray<FPropertyDescriptor>& OutProps) override;
    void PostEditProperty(const char* PropertyName) override;

    void SetAnimInstance(UAnimInstance* InAnimInstance);
    UAnimInstance* GetAnimInstance() const { return AnimInstance; }
    UAnimInstance* GetOrCreateDefaultAnimInstance();

    bool SetAnimationMode(ESkeletalMeshAnimationMode InAnimationMode);
    bool SetAnimationModeByName(const FString& InAnimationMode);
    ESkeletalMeshAnimationMode GetAnimationMode() const { return AnimationMode; }
    bool SetAnimInstanceSource(EAnimInstanceSource InAnimInstanceSource);
    EAnimInstanceSource GetAnimInstanceSource() const { return AnimInstanceSource; }

    // NOTE: 해당 함수는 AnimationMode를 SingleAnimation로 강제로 바꿀 수 있으므로 주의
    bool SetAnimationSequence(const FString& AnimationPath);
    UAnimationSequenceBase* GetAnimationSequence() const;
    const FString& GetAnimationSequencePath() const { return AnimationSequence.Path; }
    void SetSingleAnimationLooping(bool bInLooping);
    bool IsSingleAnimationLooping() const { return bSingleAnimationLoop; }
    void SetSingleAnimationPlayRate(float InPlayRate);
    float GetSingleAnimationPlayRate() const { return SingleAnimationPlayRate; }
    bool PlaySingleAnimation();
    void PauseSingleAnimation();
    void StopSingleAnimation();
    bool UseDefaultAnimInstance();

    bool UseStateMachine(UAnimStateMachineAsset* StateMachineAsset);
    bool LoadStateMachineAsset(const FString& AssetPath);
    void HandleAnimNotify(const FAnimNotifyEvent& Notify);

    void ApplyLocalPose(const TArray<FMatrix>& InLocalPose);

    void ResetToBindPose();

    void SetBoneLocalTransform(int32 BoneIndex, const FMatrix& NewLocalTransform);
    const FMatrix& GetBoneLocalTransform(int32 BoneIndex) const;

    FMatrix GetBoneGlobalTransform(int32 BoneIndex) const;
    void SetBoneGlobalTransform(int32 BoneIndex, const FMatrix& NewGlobalTransform);

private:
    bool RefreshAnimInstanceFromOptions(bool bKeepCurrentOnFailure);
    UAnimSingleNodeInstance* GetOrCreateSingleNodeAnimInstance();
    bool ActivateSingleAnimationInstance();
    bool ActivateDefaultAnimInstance();
    bool ActivateCustomAnimInstance(bool bKeepCurrentOnFailure);
    bool ActivateStateMachineAssetInstance();
    UAnimStateMachineAssetInstance* GetOrCreateStateMachineAssetInstance();
    bool IsComponentOwnedAnimInstance(const UAnimInstance* InAnimInstance) const;
    void DestroyOwnedAnimInstance(UAnimInstance*& InOutAnimInstance);
    void DestroyOwnedAnimInstance(UAnimSingleNodeInstance*& InOutAnimInstance);
    void DestroyOwnedAnimInstance(UAnimStateMachineAssetInstance*& InOutAnimInstance);
    void DestroyInactiveOwnedAnimInstances(UAnimInstance* InstanceToKeep);
    UAnimationSequenceBase* LoadConfiguredAnimationSequence() const;
    void RegisterStateAnimationPathsFromAsset(const UAnimStateMachineAsset* StateMachineAsset);

private:
    UAnimInstance* AnimInstance = nullptr;
    UAnimInstance* DefaultAnimInstance = nullptr;
    UAnimSingleNodeInstance* SingleNodeAnimInstance = nullptr;
    UAnimInstance* CustomAnimInstance = nullptr;
    UAnimStateMachineAssetInstance* StateMachineAssetInstance = nullptr;

    ESkeletalMeshAnimationMode AnimationMode = ESkeletalMeshAnimationMode::AnimInstance;
    EAnimInstanceSource AnimInstanceSource = EAnimInstanceSource::EmptyStateMachine;
    FAnimationSequenceAssetRef AnimationSequence;
    bool bSingleAnimationLoop = true;
    float SingleAnimationPlayRate = 1.0f;
    FString CustomAnimInstanceClassName;
    FAnimStateMachineAssetRef StateMachineAsset;
};
