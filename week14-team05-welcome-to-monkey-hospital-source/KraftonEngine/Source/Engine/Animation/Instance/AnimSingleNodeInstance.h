#pragma once

#include "Animation/AnimInstance.h"

class UAnimSequenceBase;
class FReferenceCollector;

// 시퀀스 1개를 재생하는 가장 단순한 인스턴스.
// PlayRate 음수 → Reverse Play (Notion 데모용 옵션).

#include "Source/Engine/Animation/Instance/AnimSingleNodeInstance.generated.h"

UCLASS()
class UAnimSingleNodeInstance : public UAnimInstance
{
public:
	GENERATED_BODY()
	UAnimSingleNodeInstance() = default;
	~UAnimSingleNodeInstance() override = default;

	UFUNCTION(Callable, Category="Animation|SingleNode")
	void SetAnimationAsset(UAnimSequenceBase* InAsset);
	UFUNCTION(Pure, Category="Animation|SingleNode")
	UAnimSequenceBase* GetAnimationAsset() const;

	UFUNCTION(Callable, Category="Animation|SingleNode")
	void  SetPlayRate(float InRate) { PlayRate = InRate; }
	UFUNCTION(Pure, Category="Animation|SingleNode")
	float GetPlayRate() const       { return PlayRate; }

	UFUNCTION(Callable, Category="Animation|SingleNode")
	void  SetLooping(bool bInLoop) { bLooping = bInLoop; }
	UFUNCTION(Pure, Category="Animation|SingleNode")
	bool  IsLooping() const        { return bLooping; }

	UFUNCTION(Callable, Category="Animation|SingleNode")
	void  SetPlaying(bool bInPlay) { bPlaying = bInPlay; }
	UFUNCTION(Pure, Category="Animation|SingleNode")
	bool  IsPlaying() const        { return bPlaying; }

	UFUNCTION(Pure, Category="Animation|SingleNode")
	float GetCurrentTime() const   { return CurrentTime; }
	UFUNCTION(Callable, Category="Animation|SingleNode")
	void  SetCurrentTime(float T)  { CurrentTime = T; }

	// UAnimInstance:
	void NativeUpdateAnimation(float DeltaSeconds) override;
	void EvaluateAnimation(FPoseContext& Output) override;
	void AddReferencedObjects(FReferenceCollector& Collector) override;
	void BeginDestroy() override;

private:
	// CurrentTime 을 dt * PlayRate 만큼 진행시키고 길이로 wrap/clamp.
	void AdvanceTime(float DeltaSeconds);

	UAnimSequenceBase* CurrentAsset = nullptr;
	float CurrentTime = 0.0f;
	float PlayRate    = 1.0f;
	bool  bPlaying    = true;
	bool  bLooping    = true;
};
