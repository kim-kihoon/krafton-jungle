#pragma once

#include "AnimNotifyState.h"
#include "Math/Vector.h"
#include "Object/Ptr/WeakObjectPtr.h"

#include "Source/Engine/Animation/Notify/AnimNotifyState_Particle.generated.h"

class USkeletalMeshComponent;
class UParticleSystemComponent;

// UAnimNotifyState_Particle — UE UAnimNotifyState_TimedParticleEffect 대응.
//   Begin: Template 파티클을 owner 의 본/소켓 위치에 스폰(FGameplayStatics::SpawnEmitterAtLocation).
//   Tick : bFollowBone 이면 매 프레임 본 위치로 actor 이동(이동 캐릭터의 trail/이펙트 추적).
//   End  : Deactivate — 잔여 입자 소멸 후 비활성. actor 는 destroy 하지 않고 유지/재사용.
//          (anim tick 중 즉시 DestroyActor 는 actor 순회 무효화 위험 → 회피. 다음 Begin 이 재활성화.)
UCLASS()
class UAnimNotifyState_Particle : public UAnimNotifyState
{
public:
	GENERATED_BODY()
	UAnimNotifyState_Particle() = default;
	~UAnimNotifyState_Particle() override = default;

	// 파티클 시스템 .uasset 프로젝트 상대경로 (예: "Content/Particle System/MyEffect.uasset").
	UPROPERTY(Edit, Save, Category="Particle", DisplayName="Template Path")
	FString TemplatePath = "None";

	UPROPERTY(Edit, Save, Category="Particle", DisplayName="Bone Name")
	FString BoneName = "";

	UPROPERTY(Edit, Save, Category="Particle", DisplayName="Location Offset")
	FVector LocationOffset = FVector(0.0f, 0.0f, 0.0f);

	UPROPERTY(Edit, Save, Category="Particle", DisplayName="Follow Bone")
	bool bFollowBone = true;

	void NotifyBegin(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Anim, float TotalDuration) override;
	void NotifyTick(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Anim, float FrameDeltaTime) override;
	void NotifyEnd(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Anim) override;

private:
	// 활성 인스턴스 추적 — 한 NotifyState 가 여러 mesh 에 동시 active 가능 (AttackHitWindow 와 동일 패턴).
	// 재사용: 같은 mesh 의 반복 발동 시 새 actor 를 스폰하지 않고 기존 PSC 를 재활성화.
	TMap<TWeakObjectPtr<USkeletalMeshComponent>, TWeakObjectPtr<UParticleSystemComponent>> SpawnedByMesh;
};
