#pragma once

#include "Animation/AnimationAsset.h"
#include "Math/Matrix.h"
#include "Animation/AnimTypes.h"

/** 시간축이 있는 애니메이션의 공통 기반입니다. */
class UAnimSequenceBase : public UAnimationAsset
{
public:
	DECLARE_CLASS(UAnimSequenceBase, UAnimationAsset)

	UAnimSequenceBase() = default;
	~UAnimSequenceBase() override = default;

	void Serialize(FArchive& Ar) override;

	float GetPlayLength() const override { return SequenceLength; }
	void SetSequenceLength(float InSequenceLength) { SequenceLength = InSequenceLength; }

	float GetRateScale() const { return RateScale; }
	void SetRateScale(float InRateScale) { RateScale = InRateScale; }

	bool IsLooping() const { return bLoop; }
	void SetLooping(bool bInLoop) { bLoop = bInLoop; }

	virtual int32 GetNumberOfSampledKeys() const { return 0; }
	virtual float GetSamplingFrameRate() const { return 0.0f; }
	virtual float GetTimeAtFrame(int32 FrameIndex) const;
	virtual bool EvaluatePose(float Time, FPoseContext& OutPose, bool bLoopOverride = true) const;

private:
	float SequenceLength = 0.0f;
	float RateScale = 1.0f;
	bool bLoop = true;
};
