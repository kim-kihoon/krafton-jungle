#include "Animation/AnimSequence.h"

#include "Animation/AnimDataModel.h"
#include "Mesh/SkeletonAsset.h"
#include "Math/Transform.h"
#include "Object/ObjectFactory.h"

#include <algorithm>
#include <cmath>

IMPLEMENT_CLASS(UAnimSequence, UAnimSequenceBase)

namespace
{
	float NormalizeSequenceTime(float Time, float Length, bool bLoop)
	{
		if (Length <= 0.0f)
		{
			return 0.0f;
		}

		if (bLoop)
		{
			Time = std::fmod(Time, Length);
			if (Time < 0.0f)
			{
				Time += Length;
			}
			return Time;
		}

		return std::max(0.0f, std::min(Time, Length));
	}
}

void UAnimSequence::Serialize(FArchive& Ar)
{
	Super::Serialize(Ar);

	if (!DataModel)
	{
		DataModel = GUObjectArray.CreateObject<UAnimDataModel>(this);
	}

	DataModel->Serialize(Ar);

	if (Ar.IsLoading())
	{
		SetSequenceLength(DataModel->GetPlayLength());
		ResolveSkeleton();
	}
}

void UAnimSequence::SetDataModel(UAnimDataModel* InDataModel)
{
	DataModel = InDataModel;
	if (DataModel)
	{
		SetSequenceLength(DataModel->GetPlayLength());
	}
}

void UAnimSequence::CollectNotifies(float PrevTime, float CurrentTime, bool bLooping, bool bReverse, TArray<FAnimNotifyEvent>& OutNotifies)
{
	const float SeqLength = DataModel->GetPlayLength();
	if (SeqLength <= 0.f) return;

	// 누적 시간 → 시퀀스 내 위치로 변환
	float LocalPrev = fmod(PrevTime, SeqLength);
	float LocalCurrent = fmod(CurrentTime, SeqLength);

	// 이번 틱에서 wrap이 몇 번 일어났는지
	const int32 PrevLap = floor(PrevTime / SeqLength);
	const int32 CurrentLap = floor(CurrentTime / SeqLength);
	const bool  bWrapped = bLooping && (CurrentLap > PrevLap);

	for (const FAnimNotifyEvent& Notify : DataModel->GetNotifies())
	{
		const float T = Notify.TriggerTime;
		bool bShouldTrigger = false;

		if (!bReverse)
		{
			if (bWrapped)
				bShouldTrigger = T > LocalPrev || T <= LocalCurrent;
			else
				bShouldTrigger = T > LocalPrev && T <= LocalCurrent;
		}
		else
		{
			// 역방향이면 누적 시간은 감소 방향
			if (bWrapped)
				bShouldTrigger = T < LocalPrev || T >= LocalCurrent;
			else
				bShouldTrigger = T < LocalPrev && T >= LocalCurrent;
		}

		if (bShouldTrigger)
			OutNotifies.push_back(Notify);
	}
}

const TArray<FAnimNotifyEvent>& UAnimSequence::GetNotifyEvents() const
{
	static const TArray<FAnimNotifyEvent> Empty;
	return DataModel ? DataModel->GetNotifies() : Empty;
}

int32 UAnimSequence::GetNumberOfSampledKeys() const
{
	return DataModel ? DataModel->GetNumberOfKeys() : 0;
}

float UAnimSequence::GetSamplingFrameRate() const
{
	return DataModel ? DataModel->GetFrameRate() : 0.0f;
}

bool UAnimSequence::EvaluatePose(float Time, FPoseContext& OutPose, bool bLoopOverride) const
{
	OutPose.Reset();

	const FSkeletonAsset* SkeletonAsset = GetSkeletonAsset();
	if (!DataModel || !SkeletonAsset)
	{
		return false;
	}

	const int32 BoneCount = static_cast<int32>(SkeletonAsset->Bones.size());
	OutPose.BoneLocalTransforms.resize(BoneCount);
	for (int32 BoneIndex = 0; BoneIndex < BoneCount; ++BoneIndex)
	{
		OutPose.BoneLocalTransforms[BoneIndex] = FTransform(SkeletonAsset->Bones[BoneIndex].LocalMatrix);
	}

	if (BoneCount == 0 || DataModel->GetBoneAnimationTracks().empty())
	{
		return true;
	}

	const float EvalTime = NormalizeSequenceTime(Time, GetPlayLength(), bLoopOverride && IsLooping());
	for (const FBoneAnimationTrack& Track : DataModel->GetBoneAnimationTracks())
	{
		if (Track.BoneTreeIndex < 0 || Track.BoneTreeIndex >= BoneCount)
		{
			continue;
		}

		const FTransform RefTransform(SkeletonAsset->Bones[Track.BoneTreeIndex].LocalMatrix);
		FTransform SampledTransform;
		DataModel->EvaluateBoneTrackTransform(Track, EvalTime, SampledTransform, RefTransform);
		OutPose.BoneLocalTransforms[Track.BoneTreeIndex] = SampledTransform;
	}

	return true;
}
