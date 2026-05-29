#pragma once

#include "SkinnedMeshComponent.h"
#include "Animation/AnimInstanceAsset.h"
#include "Animation/AnimInstance.h"
#include "SkeletalMeshComponent.generated.h"

// SkeletalMesh 전용 render proxy만 제공하는 얇은 wrapper.
// Skinning/bone/material/bounds 상태는 모두 USkinnedMeshComponent가 소유한다.
UCLASS()
class USkeletalMeshComponent : public USkinnedMeshComponent
{
public:
	GENERATED_BODY(USkeletalMeshComponent)
	USkeletalMeshComponent() = default;
	~USkeletalMeshComponent() override = default;

	void BeginPlay() override;
	void SetSkeletalMesh(USkeletalMesh* InMesh) override;
	void PostDuplicate() override;
	void PostEditProperty(const char* PropertyName) override;

	// Render access 섹션: SceneProxy
	FPrimitiveSceneProxy* CreateSceneProxy() override;

	// AnimationInstance 관리
	void SetAnimInstance(UAnimInstance* InInstance);
	UAnimInstance* GetAnimInstance() const { return AnimInstance; }
	void SetAnimInstanceAsset(UAnimInstanceAsset* InAsset);
	UAnimInstanceAsset* GetAnimInstanceAsset() const { return AnimInstanceAsset.Get(); }
	const FString& GetAnimInstanceAssetPath() const { return AnimInstanceAsset.GetPath().ToString(); }
	bool RebuildAnimInstanceFromAsset();

	void SetAnimScriptPath(const FString& Path) { AnimScriptPath = Path; }
	const FString& GetAnimScriptPath() const { return AnimScriptPath; }

	void EndPlay() override;

	void SetTwoBoneIKEnabled(bool bEnabled) { bEnableTwoBoneIK = bEnabled; }
	bool IsTwoBoneIKEnabled() const { return bEnableTwoBoneIK; }
	void SetTwoBoneIKChains(const TArray<FTwoBoneIKChain>& InChains);
	void AddTwoBoneIKChain(const FTwoBoneIKChain& Chain);
	void ClearTwoBoneIKChains();
	const TArray<FTwoBoneIKChain>& GetTwoBoneIKChains() const { return TwoBoneIKChains; }
	bool SetIKTargetPosition(int32 ChainIndex, const FVector& WorldPosition);
	bool SetIKChainEnabled(int32 ChainIndex, bool bEnabled);
	int32 FindBoneIndexByName(const FString& BoneName) const;
	FVector GetPreIKBoneLocationByIndex(int32 BoneIndex) const;

	// Notify 실행을 위해서 AnimInstance에서 여기로 전달하고 GetOwner로 전달
	void HandleAnimNotify(const FAnimNotifyEvent& Notify);

protected:
	void TickComponent(float DeltaTime, ELevelTick TickType,
		FActorComponentTickFunction& ThisTickFunction) override;

private:
	// AnimationInstance의 포즈 BoneEditLocalMatrices에 복사
	void ApplyComponentPoseOverrides(FPoseContext& Pose) const;
	void ApplyPoseToComponent(const FPoseContext& Pose);
	void CachePreIKPoseBoneWorldLocations(const FPoseContext& Pose);
	void ApplyTwoBoneIKChains(FPoseContext& Pose);

	void SolveTwoBoneIK(FPoseContext& Pose, int RootBoneIndex, int MidBoneIndex, int EndBoneIndex, const FVector& TargetPosition, const FVector& PolePosition);

	UPROPERTY(Edit, Category="Animation", DisplayName="Anim Script Path")
	FString AnimScriptPath;

	UPROPERTY(Edit, Category="IK", DisplayName="Enable Two Bone IK")
	bool bEnableTwoBoneIK = true;

	UPROPERTY(Edit, Category="IK", DisplayName="Two Bone IK Chains")
	TArray<FTwoBoneIKChain> TwoBoneIKChains;

	TArray<FVector> PreIKBoneWorldLocations;
	bool bHasPreIKPoseCache = false;

	UPROPERTY(Edit, Category="Animation", DisplayName="Anim Instance", Type=SoftObject, Class=UAnimInstanceAsset)
	TSoftObjectPtr<UAnimInstanceAsset> AnimInstanceAsset;

	UAnimInstance* AnimInstance = nullptr;

};
