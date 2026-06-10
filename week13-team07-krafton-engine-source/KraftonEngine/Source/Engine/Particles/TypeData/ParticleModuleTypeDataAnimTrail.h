#pragma once

#include "Particles/TypeData/ParticleModuleTypeDataRibbon.h"

#include "Source/Engine/Particles/TypeData/ParticleModuleTypeDataAnimTrail.generated.h"

UCLASS()
class UParticleModuleTypeDataAnimTrail : public UParticleModuleTypeDataRibbon
{
public:
	GENERATED_BODY()

	// AnimTrail식 2-socket 검격. Top(V=0)은 FirstSocket, Bottom(V=1)은 SecondSocket으로 생성된다.
	UPROPERTY(Edit, Save, Category="AnimTrail")
	FName FirstSocketName = FName("blade_base");

	UPROPERTY(Edit, Save, Category="AnimTrail")
	FName SecondSocketName = FName("blade_tip");

	// Trail particle lifetime. 짧을수록 심홍수연식 빠른 검격 잔상에 가까워진다.
	UPROPERTY(Edit, Save, Category="AnimTrail")
	float TrailLifeTime = 0.16f;

	// 0 이하이면 매 tick 샘플링한다.
	UPROPERTY(Edit, Save, Category="AnimTrail")
	float SampleInterval = 0.008f;

	// 중점 이동거리가 이 값보다 작으면 새 segment 생성을 생략한다. 첫 샘플은 항상 생성한다.
	UPROPERTY(Edit, Save, Category="AnimTrail")
	float MinSampleDistance = 0.5f;

	// 실제 socket 간 거리의 반값에 곱해지는 스케일. 1이면 base/tip을 그대로 잇는다.
	UPROPERTY(Edit, Save, Category="AnimTrail")
	float WidthScale = 1.0f;

	UParticleModuleTypeDataAnimTrail();

	uint32 RequiredBytes(UParticleModuleTypeDataBase* TypeData) override;
	FParticleEmitterInstance* CreateInstance(UParticleEmitter* InEmitterParent, UParticleSystemComponent& InComponent) override;
	void Serialize(FArchive& Ar) override;
};
