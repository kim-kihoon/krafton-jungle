#pragma once

#include "Core/CoreTypes.h"
#include "Core/Property/PropertyTypes.h"
#include "Math/Quat.h"
#include "Math/Transform.h"
#include "Math/Vector.h"
#include "Object/FName.h"
#include "Object/ObjectMacros.h"
#include "Serialization/Archive.h"
#include "Notify.h"
#include "AnimTypes.generated.h"

/** 한 Bone의 transform key 데이터만 들고 있는 순수 데이터 구조체 입니다. */
struct FRawAnimSequenceTrack
{
	TArray<FVector> PosKeys;
	TArray<FQuat> RotKeys;
	TArray<FVector> ScaleKeys;

	friend FArchive& operator<<(FArchive& Ar, FRawAnimSequenceTrack& Track)
	{
		Ar << Track.PosKeys;
		Ar << Track.RotKeys;
		Ar << Track.ScaleKeys;
		return Ar;
	}
};

/** FRawAnimSequenceTrack이 어떤 Bone에 속하는지 연결합니다. */
struct FBoneAnimationTrack
{
	FRawAnimSequenceTrack InternalTrackData;
	int32 BoneTreeIndex = -1;
	FName Name;

	friend FArchive& operator<<(FArchive& Ar, FBoneAnimationTrack& Track)
	{
		Ar << Track.InternalTrackData;
		Ar << Track.BoneTreeIndex;
		Ar << Track.Name;
		return Ar;
	}
};

struct FPoseContext
{
	// 부모 bone 기준의 Local Transform (Skeleton bone 순서와 동일한 index로 접근)
	TArray<FTransform> BoneLocalTransforms;

	// TODO: IK를 위한 TArray<FMatrix> GlobalTransforms 추가 해야함.

	void Reset()
	{
		BoneLocalTransforms.clear();
	}
};

struct FAnimNotifyEvent
{
	float TriggerTime = 0.0f;
	float Duration    = 0.0f;
	FName NotifyName;

	UNotify* NotifyTrigger = nullptr;

	bool IsStateNotify() const { return Duration > 0.0f; }

	friend FArchive& operator<<(FArchive& Ar, FAnimNotifyEvent& Notify)
	{
		Ar << Notify.TriggerTime;
		Ar << Notify.Duration;
		Ar << Notify.NotifyName;
		return Ar;
	}
};

USTRUCT()
struct FTwoBoneIKChain
{
	GENERATED_BODY(FTwoBoneIKChain)

	UPROPERTY(Edit, Category="IK", DisplayName="Enabled")
	bool bEnabled = true;

	/** UpperArm or Thigh */
	UPROPERTY(Edit, Category="IK", DisplayName="Root Bone Index", Speed=1.0f)
	int RootBoneIndex = -1;

	/** LowerArm or Calf */
	UPROPERTY(Edit, Category="IK", DisplayName="Mid Bone Index", Speed=1.0f)
	int MidBoneIndex = -1;

	/** Hand or Foot */
	UPROPERTY(Edit, Category="IK", DisplayName="End Bone Index", Speed=1.0f)
	int EndBoneIndex = -1;

	UPROPERTY(Edit, Category="IK", DisplayName="Target Position")
	FVector TargetPosition = FVector::ZeroVector;

	UPROPERTY(Edit, Category="IK", DisplayName="Pole Position")
	FVector PolePosition = FVector::ForwardVector;

	friend FArchive& operator<<(FArchive& Ar, FTwoBoneIKChain& Chain)
	{
		Ar << Chain.bEnabled;
		Ar << Chain.RootBoneIndex;
		Ar << Chain.MidBoneIndex;
		Ar << Chain.EndBoneIndex;
		Ar << Chain.TargetPosition;
		Ar << Chain.PolePosition;
		return Ar;
	}
};
