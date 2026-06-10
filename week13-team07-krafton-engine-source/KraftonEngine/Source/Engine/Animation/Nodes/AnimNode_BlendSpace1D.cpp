#include "AnimNode_BlendSpace1D.h"

#include "Animation/AnimExtractContext.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimationRuntime.h"
#include "Animation/PoseContext.h"
#include "Animation/Sequence/AnimSequence.h"
#include "Animation/Sequence/AnimSequenceBase.h"
#include "Math/Quat.h"
#include "Object/GarbageCollection.h"
#include "Object/Object.h"

#include <algorithm>
#include <cmath>

namespace
{
	constexpr float BlendSpaceValueTolerance = 1.0e-4f;

	float Clamp01(float Value)
	{
		return std::clamp(Value, 0.0f, 1.0f);
	}

	float Wrap01(float Value)
	{
		Value = std::fmod(Value, 1.0f);
		if (Value < 0.0f)
		{
			Value += 1.0f;
		}
		return Value;
	}

	FTransform BlendRootMotion(const FTransform& A, const FTransform& B, float Alpha)
	{
		FTransform Out;
		Out.Location = A.Location + (B.Location - A.Location) * Alpha;
		Out.Rotation = FQuat::Slerp(A.Rotation.GetNormalized(), B.Rotation.GetNormalized(), Alpha).GetNormalized();
		Out.Scale    = A.Scale + (B.Scale - A.Scale) * Alpha;
		return Out;
	}
}

void FAnimNode_BlendSpace1D::AddSample(UAnimSequenceBase* Sequence, float Value, float PlayRate, bool bLooping)
{
	if (!Sequence)
	{
		return;
	}

	FBlendSample Sample;
	Sample.Sequence = Sequence;
	Sample.Value    = Value;
	Sample.PlayRate = std::isfinite(PlayRate) ? PlayRate : 1.0f;
	Sample.bLooping = bLooping;
	Samples.push_back(Sample);
	bSamplesDirty = true;
}

void FAnimNode_BlendSpace1D::ClearSamples()
{
	Samples.clear();
	bSamplesDirty = false;
	Phase01 = 0.0f;
	LastRootMotionDelta = FTransform();
}

void FAnimNode_BlendSpace1D::SetNormalizedPhase(float InPhase)
{
	Phase01 = Wrap01(std::isfinite(InPhase) ? InPhase : 0.0f);
}

void FAnimNode_BlendSpace1D::Initialize(const FAnimationInitializeContext& /*Context*/)
{
	SortSamplesIfNeeded();
	Phase01 = 0.0f;
	LastRootMotionDelta = FTransform();
}

void FAnimNode_BlendSpace1D::OnBecomeRelevant(const FAnimationInitializeContext& /*Context*/)
{
	// StateMachine 없이 root 로 쓰는 경우에는 Initialize 만 호출된다. 그래도 나중에 상위 노드
	// 아래에 붙였을 때 재진입 시점이 튀지 않도록 phase reset 은 유지한다.
	Phase01 = 0.0f;
	LastRootMotionDelta = FTransform();
}

void FAnimNode_BlendSpace1D::SortSamplesIfNeeded()
{
	if (!bSamplesDirty)
	{
		return;
	}

	std::sort(Samples.begin(), Samples.end(), [](const FBlendSample& A, const FBlendSample& B)
	{
		return A.Value < B.Value;
	});
	bSamplesDirty = false;
}

void FAnimNode_BlendSpace1D::ResolveSamplePair(int32& OutA, int32& OutB, float& OutAlpha)
{
	SortSamplesIfNeeded();

	OutA = -1;
	OutB = -1;
	OutAlpha = 0.0f;

	if (Samples.empty())
	{
		return;
	}

	if (Samples.size() == 1 || InputValue <= Samples.front().Value)
	{
		OutA = 0;
		OutB = 0;
		return;
	}

	const int32 LastIndex = static_cast<int32>(Samples.size() - 1);
	if (InputValue >= Samples.back().Value)
	{
		OutA = LastIndex;
		OutB = LastIndex;
		return;
	}

	for (int32 Index = 0; Index < LastIndex; ++Index)
	{
		const FBlendSample& A = Samples[Index];
		const FBlendSample& B = Samples[Index + 1];
		if (InputValue >= A.Value && InputValue <= B.Value)
		{
			OutA = Index;
			OutB = Index + 1;

			const float Range = B.Value - A.Value;
			OutAlpha = std::fabs(Range) > BlendSpaceValueTolerance
				? Clamp01((InputValue - A.Value) / Range)
				: 0.0f;
			return;
		}
	}

	OutA = LastIndex;
	OutB = LastIndex;
}

float FAnimNode_BlendSpace1D::GetSampleLength(int32 SampleIndex) const
{
	if (SampleIndex < 0 || SampleIndex >= static_cast<int32>(Samples.size()))
	{
		return 0.0f;
	}

	UAnimSequenceBase* Sequence = Samples[SampleIndex].Sequence;
	return Sequence ? Sequence->GetPlayLength() : 0.0f;
}

float FAnimNode_BlendSpace1D::GetSampleTime(int32 SampleIndex, float Phase) const
{
	const float Length = GetSampleLength(SampleIndex);
	if (Length <= 0.0f)
	{
		return 0.0f;
	}
	return Clamp01(Phase) * Length;
}

void FAnimNode_BlendSpace1D::Update(const FAnimationUpdateContext& Context)
{
	int32 SampleA = -1;
	int32 SampleB = -1;
	float Alpha = 0.0f;
	ResolveSamplePair(SampleA, SampleB, Alpha);

	if (SampleA < 0)
	{
		LastRootMotionDelta = FTransform();
		return;
	}

	const float WeightA = (SampleA == SampleB) ? 1.0f : (1.0f - Alpha);
	const float WeightB = (SampleA == SampleB) ? 0.0f : Alpha;

	const float PreviousPhase = Phase01;

	float PhaseDelta = 0.0f;
	bool bAnyLooping = false;
	float TotalWeight = 0.0f;

	auto AccumulatePhaseRate = [&](int32 SampleIndex, float Weight)
	{
		if (SampleIndex < 0 || Weight <= 0.0f)
		{
			return;
		}

		const float Length = GetSampleLength(SampleIndex);
		if (Length <= 0.0f)
		{
			return;
		}

		const FBlendSample& Sample = Samples[SampleIndex];
		PhaseDelta += Weight * (Sample.PlayRate / Length) * Context.DeltaSeconds;
		TotalWeight += Weight;
		bAnyLooping = bAnyLooping || Sample.bLooping;
	};

	AccumulatePhaseRate(SampleA, WeightA);
	AccumulatePhaseRate(SampleB, WeightB);

	if (TotalWeight > 0.0f)
	{
		Phase01 += PhaseDelta;
		Phase01 = bAnyLooping ? Wrap01(Phase01) : Clamp01(Phase01);
	}

	// Notify 는 dominant sample 하나만 발생시킨다. 두 샘플의 notify 를 모두 발사하면
	// footstep / attack window 등이 blend 구간에서 중복 실행될 수 있기 때문이다.
	const int32 DominantSample = (WeightB > WeightA) ? SampleB : SampleA;
	const float DominantWeight = (WeightB > WeightA) ? WeightB : WeightA;
	if (Context.AnimInstance && DominantSample >= 0 && Context.FinalBlendWeight * DominantWeight > ZERO_ANIMWEIGHT_THRESH)
	{
		const FBlendSample& Sample = Samples[DominantSample];
		const float PreviousTime = GetSampleTime(DominantSample, PreviousPhase);
		const float CurrentTime  = GetSampleTime(DominantSample, Phase01);
		Context.AnimInstance->AddAnimNotifies(PreviousTime, CurrentTime, Sample.Sequence);
	}

	if (SampleA == SampleB)
	{
		LastRootMotionDelta = ExtractSampleRootMotion(SampleA, PreviousPhase, Phase01);
	}
	else
	{
		const FTransform RMA = ExtractSampleRootMotion(SampleA, PreviousPhase, Phase01);
		const FTransform RMB = ExtractSampleRootMotion(SampleB, PreviousPhase, Phase01);
		LastRootMotionDelta = BlendRootMotion(RMA, RMB, Alpha);
	}
}

FTransform FAnimNode_BlendSpace1D::ExtractSampleRootMotion(int32 SampleIndex, float PreviousPhase, float CurrentPhase) const
{
	if (SampleIndex < 0 || SampleIndex >= static_cast<int32>(Samples.size()))
	{
		return FTransform();
	}

	const FBlendSample& Sample = Samples[SampleIndex];
	UAnimSequence* Sequence = Cast<UAnimSequence>(Sample.Sequence);
	if (!Sequence || !Sequence->GetEnableRootMotion())
	{
		return FTransform();
	}

	const float PreviousTime = GetSampleTime(SampleIndex, PreviousPhase);
	const float CurrentTime  = GetSampleTime(SampleIndex, CurrentPhase);
	return Sequence->ExtractRootMotion(PreviousTime, CurrentTime, Sample.bLooping);
}

void FAnimNode_BlendSpace1D::EvaluateSample(int32 SampleIndex, FPoseContext& Output) const
{
	if (SampleIndex < 0 || SampleIndex >= static_cast<int32>(Samples.size()))
	{
		Output.ResetToRefPose();
		return;
	}

	const FBlendSample& Sample = Samples[SampleIndex];
	if (!Sample.Sequence)
	{
		Output.ResetToRefPose();
		return;
	}

	FAnimExtractContext Ctx;
	Ctx.CurrentTime = GetSampleTime(SampleIndex, Phase01);
	Ctx.bLooping    = Sample.bLooping;
	Sample.Sequence->GetBonePose(Output, Ctx);
}

void FAnimNode_BlendSpace1D::Evaluate(FPoseContext& Output)
{
	int32 SampleA = -1;
	int32 SampleB = -1;
	float Alpha = 0.0f;
	ResolveSamplePair(SampleA, SampleB, Alpha);

	if (SampleA < 0)
	{
		Output.ResetToRefPose();
		return;
	}

	if (SampleA == SampleB || Alpha <= ZERO_ANIMWEIGHT_THRESH)
	{
		EvaluateSample(SampleA, Output);
		return;
	}

	if (Alpha >= 1.0f - ZERO_ANIMWEIGHT_THRESH)
	{
		EvaluateSample(SampleB, Output);
		return;
	}

	FPoseContext PoseA;
	PoseA.SkeletalMesh = Output.SkeletalMesh;
	PoseA.ResetToRefPose();
	EvaluateSample(SampleA, PoseA);

	FPoseContext PoseB;
	PoseB.SkeletalMesh = Output.SkeletalMesh;
	PoseB.ResetToRefPose();
	EvaluateSample(SampleB, PoseB);

	FAnimationRuntime::BlendTwoPosesTogether(PoseA, PoseB, Alpha, Output);
}

void FAnimNode_BlendSpace1D::AddReferencedObjects(FReferenceCollector& Collector)
{
	for (FBlendSample& Sample : Samples)
	{
		Collector.AddReferencedObject(Sample.Sequence);
	}
}
