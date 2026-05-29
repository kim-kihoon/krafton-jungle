#pragma once
#include "Object/Object.h"
#include "Animation/AnimTypes.h"

class UAnimationStateMachine;
class USkeletalMeshComponent;

class UAnimInstance : public UObject
{
public:
	DECLARE_CLASS(UAnimInstance, UObject)
	virtual void Initialize(USkeletalMeshComponent* InOwner, const FString& InScriptPath = "");
	void Update(float DeltaTime);
	virtual void NativeUpdateAnimation(float DeltaSeconds) {}

	void TriggerAnimNotifies();

	// SkeletalMeshComponent가 호출 — 포즈 생성 및 Notify 수집
	virtual void GetCurrentPose(FPoseContext& OutPose);

	void SetStateMachine(UAnimationStateMachine* SM) { StateMachine = SM; }

	USkeletalMeshComponent* GetOwnerComponent() const { return OwnerComponent; }

protected:
	USkeletalMeshComponent* OwnerComponent = nullptr;
	UAnimationStateMachine* StateMachine = nullptr;
	TArray<FAnimNotifyEvent> NotifyQueue;
	float                   LastEvaluatedTime = 0.0f;

	void ResetNotifyState();
	void RouteNotify(const FAnimNotifyEvent& Notify);

private:
	struct FActiveNotifyState
	{
		FAnimNotifyEvent Notify;
	};

	TArray<FActiveNotifyState>  ActiveStateNotifies;
};