#pragma once

#include "Particle/ParticleModule.h"

#include "Source/Engine/Particle/TypeData/ParticleModuleTypeDataBase.generated.h"

class FParticleEmitterInstance;
class UParticleSystemComponent;

// =============================================================================
// UParticleModuleTypeDataBase
//   "이 emitter 는 어떤 종류의 입자인가?" 를 결정. nullptr 이면 Sprite (default).
//   서브클래스가 emitter 인스턴스 팩토리를 override 하여 본인이 원하는
//   FParticleEmitterInstance 파생을 만든다.
// =============================================================================
UCLASS()
class UParticleModuleTypeDataBase : public UParticleModule
{
public:
	GENERATED_BODY()
	UParticleModuleTypeDataBase() = default;

	EModuleCategory GetCategory() const override { return EModuleCategory::TypeData; }
	const char*     GetDisplayName() const override { return "TypeData (Base)"; }
	bool            IsUnique() const override { return true; }

	// emitter::CreateInstance 가 호출. 서브클래스가 본인의 FParticleEmitterInstance
	// 파생을 new 해서 반환. 기본은 nullptr → emitter 가 Sprite instance 폴백.
	virtual FParticleEmitterInstance* CreateInstance(UParticleSystemComponent* InComponent);
};
