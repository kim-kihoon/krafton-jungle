#include "Animation/AnimSequenceBase.h"

#include "Object/ObjectFactory.h"
#include "Serialization/Archive.h"

#include <algorithm>

IMPLEMENT_CLASS(UAnimSequenceBase, UAnimationAsset)

void UAnimSequenceBase::Serialize(FArchive& Ar)
{
	Super::Serialize(Ar);
	Ar << SequenceLength;
	Ar << RateScale;
	Ar << bLoop;
}

float UAnimSequenceBase::GetTimeAtFrame(int32 FrameIndex) const
{
	const float FrameRate = GetSamplingFrameRate();
	if (FrameRate <= 0.0f)
	{
		return 0.0f;
	}

	const float Time = static_cast<float>(FrameIndex) / FrameRate;
	return std::max(0.0f, std::min(Time, SequenceLength));
}

bool UAnimSequenceBase::EvaluatePose(float Time, FPoseContext& OutPose, bool bLoopOverride) const
{
	(void)Time;
	(void)bLoopOverride;
	OutPose.Reset();
	return false;
}
