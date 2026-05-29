#include "SkeletalMeshComponent.h"
#include "Animation/AnimInstanceAssetManager.h"
#include "Animation/AnimGraphInstance.h"
#include "Animation/AnimationRuntime.h"
#include "Animation/CharacterAnimInstance.h"
#include "Core/Log.h"
#include "Object/FUObjectArray.h"
#include "Render/Proxy/SkeletalMeshSceneProxy.h"
#include "Mesh/SkeletalMesh.h"
#include "Mesh/SkeletonAsset.h"
#include "GameFramework/AActor.h"
#include "Platform/Paths.h"
#include "Serialization/Archive.h"
#include <cctype>
#include <cstring>

void USkeletalMeshComponent::BeginPlay()
{
	Super::BeginPlay();

	if (!AnimInstanceAsset.IsNull())
	{
		if (AnimInstance)
		{
			GUObjectArray.DestroyObject(AnimInstance);
			AnimInstance = nullptr;
		}
		RebuildAnimInstanceFromAsset();
		return;
	}

	if (AnimScriptPath.empty())
		return;

	// PIE 재시작 등으로 이미 인스턴스가 있으면 먼저 정리
	if (AnimInstance)
	{
		GUObjectArray.DestroyObject(AnimInstance);
		AnimInstance = nullptr;
	}

	UCharacterAnimInstance* Inst = GUObjectArray.CreateObject<UCharacterAnimInstance>();
	AnimInstance = Inst;
	Inst->Initialize(this, AnimScriptPath);
}

void USkeletalMeshComponent::EndPlay()
{
	if (AnimInstance)
	{
		GUObjectArray.DestroyObject(AnimInstance);
		AnimInstance = nullptr;
	}
	Super::EndPlay();
}

void USkeletalMeshComponent::SetSkeletalMesh(USkeletalMesh* InMesh)
{
	Super::SetSkeletalMesh(InMesh);
	if (!AnimInstanceAsset.IsNull())
	{
		RebuildAnimInstanceFromAsset();
	}
}

void USkeletalMeshComponent::PostDuplicate()
{
	Super::PostDuplicate();
	if (!AnimInstanceAsset.IsNull())
	{
		RebuildAnimInstanceFromAsset();
	}
}

void USkeletalMeshComponent::PostEditProperty(const char* PropertyName)
{
	Super::PostEditProperty(PropertyName);

	if (PropertyName
		&& (strcmp(PropertyName, "Anim Instance") == 0 || strcmp(PropertyName, "AnimInstanceAsset") == 0))
	{
		RebuildAnimInstanceFromAsset();
	}
}

FPrimitiveSceneProxy* USkeletalMeshComponent::CreateSceneProxy()
{
	return new FSkeletalMeshSceneProxy(this);
}

void USkeletalMeshComponent::SetAnimInstance(UAnimInstance* InInstance)
{
	AnimInstance = InInstance;
}

void USkeletalMeshComponent::SetAnimInstanceAsset(UAnimInstanceAsset* InAsset)
{
	if (InAsset)
	{
		AnimInstanceAsset = InAsset;
	}
	else
	{
		AnimInstanceAsset.Reset();
	}
	RebuildAnimInstanceFromAsset();
}

bool USkeletalMeshComponent::RebuildAnimInstanceFromAsset()
{
	if (AnimInstanceAsset.IsNull())
	{
		SetAnimInstance(nullptr);
		return false;
	}

	const FString AssetPath = FPaths::MakeProjectRelative(AnimInstanceAsset.GetPath().ToString());
	if (AssetPath.empty() || AssetPath == "None")
	{
		SetAnimInstance(nullptr);
		return false;
	}

	UAnimInstanceAsset* Asset = AnimInstanceAsset.Get();
	if (!Asset)
	{
		Asset = FAnimInstanceAssetManager::Get().Load(AssetPath);
		if (Asset)
		{
			AnimInstanceAsset.SetCache(Asset);
		}
	}

	if (!Asset)
	{
		UE_LOG("SkeletalMeshComponent AnimInstanceAsset load failed: %s", AssetPath.c_str());
		SetAnimInstance(nullptr);
		return false;
	}

	USkeletalMesh* Mesh = GetSkeletalMesh();
	const FSkeletalMesh* MeshAsset = Mesh ? Mesh->GetSkeletalMeshAsset() : nullptr;
	const FString MeshSkeletonPath = MeshAsset ? FPaths::MakeProjectRelative(MeshAsset->SkeletonPath) : FString();
	const FString AnimSkeletonPath = FPaths::MakeProjectRelative(Asset->GetSkeletonPath());
	if (MeshSkeletonPath.empty())
	{
		UE_LOG("SkeletalMeshComponent AnimInstanceAsset skipped: SkeletalMesh has no Skeleton. Asset=%s", AssetPath.c_str());
		SetAnimInstance(nullptr);
		return false;
	}

	if (AnimSkeletonPath.empty() || MeshSkeletonPath != AnimSkeletonPath)
	{
		UE_LOG("SkeletalMeshComponent AnimInstanceAsset skeleton mismatch. MeshSkeleton=%s AnimSkeleton=%s Asset=%s",
			MeshSkeletonPath.c_str(), AnimSkeletonPath.c_str(), AssetPath.c_str());
		SetAnimInstance(nullptr);
		return false;
	}

	UAnimGraphInstance* RuntimeInstance = Asset->CreateRuntimeInstance();
	RuntimeInstance->Initialize(this);
	SetAnimInstance(RuntimeInstance);
	return RuntimeInstance != nullptr;
}

void USkeletalMeshComponent::TickComponent(float DeltaTime, ELevelTick TickType,
	FActorComponentTickFunction& ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (!AnimInstance)
	{
		PreIKBoneWorldLocations.clear();
		bHasPreIKPoseCache = false;
		return;
	}

	AnimInstance->Update(DeltaTime);

	FPoseContext Pose;
	AnimInstance->GetCurrentPose(Pose);
	AnimInstance->TriggerAnimNotifies();

	if (!Pose.BoneLocalTransforms.empty())
	{
		ApplyComponentPoseOverrides(Pose);
		CachePreIKPoseBoneWorldLocations(Pose);
		ApplyTwoBoneIKChains(Pose);
		ApplyPoseToComponent(Pose);
	}
	else
	{
		PreIKBoneWorldLocations.clear();
		bHasPreIKPoseCache = false;
	}
}

void USkeletalMeshComponent::SetTwoBoneIKChains(const TArray<FTwoBoneIKChain>& InChains)
{
	TwoBoneIKChains = InChains;
}

void USkeletalMeshComponent::AddTwoBoneIKChain(const FTwoBoneIKChain& Chain)
{
	TwoBoneIKChains.push_back(Chain);
}

void USkeletalMeshComponent::ClearTwoBoneIKChains()
{
	TwoBoneIKChains.clear();
}

bool USkeletalMeshComponent::SetIKTargetPosition(int32 ChainIndex, const FVector& WorldPosition)
{
	if (ChainIndex < 0 || ChainIndex >= static_cast<int32>(TwoBoneIKChains.size()))
	{
		return false;
	}

	TwoBoneIKChains[ChainIndex].TargetPosition = GetWorldInverseMatrix().TransformPositionWithW(WorldPosition);
	return true;
}

bool USkeletalMeshComponent::SetIKChainEnabled(int32 ChainIndex, bool bEnabled)
{
	if (ChainIndex < 0 || ChainIndex >= static_cast<int32>(TwoBoneIKChains.size()))
	{
		return false;
	}

	TwoBoneIKChains[ChainIndex].bEnabled = bEnabled;
	return true;
}

int32 USkeletalMeshComponent::FindBoneIndexByName(const FString& BoneName) const
{
	if (!SkeletalMesh || BoneName.empty())
	{
		return -1;
	}

	const FSkeletonAsset* SkeletonAsset = SkeletalMesh->GetSkeletonAsset();
	if (!SkeletonAsset)
	{
		return -1;
	}

	for (int32 BoneIndex = 0; BoneIndex < static_cast<int32>(SkeletonAsset->Bones.size()); ++BoneIndex)
	{
		if (SkeletonAsset->Bones[BoneIndex].Name == BoneName)
		{
			return BoneIndex;
		}
	}

	return -1;
}

FVector USkeletalMeshComponent::GetPreIKBoneLocationByIndex(int32 BoneIndex) const
{
	if (bHasPreIKPoseCache
		&& BoneIndex >= 0
		&& BoneIndex < static_cast<int32>(PreIKBoneWorldLocations.size()))
	{
		return PreIKBoneWorldLocations[BoneIndex];
	}

	return GetBoneLocationByIndex(BoneIndex);
}

void USkeletalMeshComponent::ApplyComponentPoseOverrides(FPoseContext& Pose) const
{
	if (bIgnoreRootMotion && TargetRootBoneIndex < static_cast<uint32>(Pose.BoneLocalTransforms.size()))
	{
		Pose.BoneLocalTransforms[TargetRootBoneIndex].Location = FVector::ZeroVector;
	}
}

void USkeletalMeshComponent::ApplyPoseToComponent(const FPoseContext& Pose)
{
	if (!SkeletalMesh)
		return;

	EnsureBoneEditPose();

	const FSkeletonAsset* SkeletonAsset = SkeletalMesh->GetSkeletonAsset();

	for (uint32 BoneIdx = 0; BoneIdx < static_cast<uint32>(Pose.BoneLocalTransforms.size()); ++BoneIdx)
	{
		if (BoneIdx >= static_cast<uint32>(BoneEditLocalMatrices.size()))
			continue;

		FTransform BoneTM = Pose.BoneLocalTransforms[BoneIdx];

		if (bIgnoreRootMotion && BoneIdx == TargetRootBoneIndex)
		{
			BoneTM.Location = FVector::ZeroVector;
		}

		BoneEditLocalMatrices[BoneIdx] = BoneTM.ToMatrix();
	}

	bUseBoneEditPose = true;
	UpdateSkinMatrices();
	MarkWorldBoundsDirty();
}

void USkeletalMeshComponent::CachePreIKPoseBoneWorldLocations(const FPoseContext& Pose)
{
	PreIKBoneWorldLocations.clear();
	bHasPreIKPoseCache = false;

	if (!SkeletalMesh)
	{
		return;
	}

	const FSkeletonAsset* SkeletonAsset = SkeletalMesh->GetSkeletonAsset();
	if (!SkeletonAsset)
	{
		return;
	}

	TArray<FMatrix> GlobalMatrices;
	FAnimationRuntime::BuildPoseGlobalMatrices(Pose, SkeletonAsset, GlobalMatrices);
	if (GlobalMatrices.empty())
	{
		return;
	}

	const FMatrix& ComponentToWorld = GetWorldMatrix();
	PreIKBoneWorldLocations.resize(GlobalMatrices.size());
	for (int32 BoneIndex = 0; BoneIndex < static_cast<int32>(GlobalMatrices.size()); ++BoneIndex)
	{
		PreIKBoneWorldLocations[BoneIndex] = ComponentToWorld.TransformPositionWithW(GlobalMatrices[BoneIndex].GetLocation());
	}

	bHasPreIKPoseCache = true;
}

void USkeletalMeshComponent::ApplyTwoBoneIKChains(FPoseContext& Pose)
{
	if (!bEnableTwoBoneIK || TwoBoneIKChains.empty() || !SkeletalMesh)
	{
		return;
	}

	const FSkeletonAsset* SkeletonAsset = SkeletalMesh->GetSkeletonAsset();
	if (!SkeletonAsset)
	{
		return;
	}

	for (const FTwoBoneIKChain& Chain : TwoBoneIKChains)
	{
		if (!Chain.bEnabled)
		{
			continue;
		}

		FAnimationRuntime::SolveTwoBoneIK(Pose, SkeletonAsset, Chain);
	}
}

void USkeletalMeshComponent::SolveTwoBoneIK(FPoseContext& Pose, int RootBoneIndex, int MidBoneIndex, int EndBoneIndex,
	const FVector& TargetPosition, const FVector& PolePosition)
{
	if (!SkeletalMesh)
	{
		return;
	}

	FTwoBoneIKChain Chain;
	Chain.RootBoneIndex = RootBoneIndex;
	Chain.MidBoneIndex = MidBoneIndex;
	Chain.EndBoneIndex = EndBoneIndex;
	Chain.TargetPosition = TargetPosition;
	Chain.PolePosition = PolePosition;

	FAnimationRuntime::SolveTwoBoneIK(Pose, SkeletalMesh->GetSkeletonAsset(), Chain);
}

void USkeletalMeshComponent::HandleAnimNotify(const FAnimNotifyEvent& Notify)
{
	if (!Notify.NotifyTrigger)
		return;

	AActor* Owner = GetOwner();
	Notify.NotifyTrigger->OnNotify(Owner, this);
}
