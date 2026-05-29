#pragma once
#include "Object/Object.h"
#include "Animation/AnimTypes.h"

class USkeletalMeshComponent;
class UAnimSequence;
class UAnimInstance;

class UAnimationStateMachine : public UObject
{
public:
	virtual void Initialize(USkeletalMeshComponent* InOwner, UAnimInstance* InAnimInstance)
	{ 
		OwnerComponent = InOwner;
		OwnerAnimInstance = InAnimInstance;
	}

	// PrevStateLocalTime 저장 → BlendAlpha 갱신 → ProcessState() 호출
	void UpdateAnimationState(float DeltaTime);

	// 각 구현체가 전이 조건과 StateLocalTime을 관리
	virtual void ProcessState(float DeltaSeconds) = 0;

	// 포즈 생성 + 해당 프레임 구간의 Notify 수집
	virtual void GenerateFinalPose(FPoseContext& OutPose, TArray<FAnimNotifyEvent>& OutNotifies) const;

	UAnimSequence* GetCurrentSequence() const;
	float GetCurrentStateTime() const;

protected:
	USkeletalMeshComponent* OwnerComponent  = nullptr;
	UAnimInstance*          OwnerAnimInstance = nullptr;
	UAnimSequence*          CurrentSequence = nullptr;
	UAnimSequence*          PrevSequence    = nullptr;

	float StateLocalTime = 0.0f; // 현재 애니메이션이 얼마나 진행되었는지
	float PrevStateLocalTime = 0.0f; // 바로 전 프레임에 StateLocal이 몇초였는지

	float BlendAlpha     = 1.0f; // 현재 애니메이션에서 다음 애니메이션까지 얼마나 blend 되었는지
	float BlendDuration  = 0.2f; // Blend 되는 시간
	float BlendingPrevStateTime = 0.0f;

	float PlayRate = 1.0f;
};
