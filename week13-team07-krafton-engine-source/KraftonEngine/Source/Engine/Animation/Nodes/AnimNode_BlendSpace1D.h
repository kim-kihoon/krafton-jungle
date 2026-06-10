#pragma once

#include "AnimNode_Base.h"
#include "Core/Types/CoreTypes.h"
#include "Math/Transform.h"

class UAnimSequenceBase;

// Lua 전용 1D BlendSpace 런타임 노드.
// StateMachine 없이 Speed 같은 단일 입력값으로 Idle/Walk/Run 샘플을 연속 보간한다.
//
// 설계 의도:
//   - Asset/Editor 의존 없음. Lua init() 에서 샘플을 직접 등록한다.
//   - 샘플들은 Value 기준 정렬되고, 현재 InputValue 를 감싸는 두 샘플만 평가한다.
//   - Walk/Run 발 타이밍이 따로 놀지 않도록 노드 하나의 Normalized Phase 를 공유한다.
//   - Notify 는 중복 방지를 위해 현재 가중치가 더 큰 dominant sample 하나에서만 발생한다.
class FAnimNode_BlendSpace1D : public FAnimNode_Base
{
public:
	struct FBlendSample
	{
		UAnimSequenceBase* Sequence = nullptr;
		float              Value    = 0.0f;
		float              PlayRate = 1.0f;
		bool               bLooping = true;
	};

	TArray<FBlendSample> Samples;
	float                InputValue = 0.0f;

	void AddSample(UAnimSequenceBase* Sequence, float Value, float PlayRate = 1.0f, bool bLooping = true);
	void ClearSamples();
	void SetInputValue(float InValue) { InputValue = InValue; }
	float GetInputValue() const { return InputValue; }
	void SetNormalizedPhase(float InPhase);
	float GetNormalizedPhase() const { return Phase01; }

	void Initialize(const FAnimationInitializeContext& Context) override;
	void OnBecomeRelevant(const FAnimationInitializeContext& Context) override;
	void Update(const FAnimationUpdateContext& Context) override;
	void Evaluate(FPoseContext& Output) override;
	const FTransform& GetLastRootMotionDelta() const override { return LastRootMotionDelta; }
	const char* GetDebugName() const override { return "BlendSpace1D"; }
	void AddReferencedObjects(FReferenceCollector& Collector) override;

private:
	float      Phase01 = 0.0f;
	FTransform LastRootMotionDelta;
	bool       bSamplesDirty = false;

	void SortSamplesIfNeeded();
	void ResolveSamplePair(int32& OutA, int32& OutB, float& OutAlpha);
	float GetSampleLength(int32 SampleIndex) const;
	float GetSampleTime(int32 SampleIndex, float Phase) const;
	void EvaluateSample(int32 SampleIndex, FPoseContext& Output) const;
	FTransform ExtractSampleRootMotion(int32 SampleIndex, float PreviousPhase, float CurrentPhase) const;
};
