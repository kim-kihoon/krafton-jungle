#pragma once
#include "Object/Object.h"
#include "Object/Reflection/ObjectMacros.h"
#include "Source/Engine/Particle/Distributions/Distribution.generated.h"

UENUM()
enum ERawDistributionOperation
{
	RDO_Uninitialized,
	RDO_None,
};

UCLASS()
class UDistribution : public UObject
{
public:
	GENERATED_BODY()

	virtual void GetInRange(float& OutMin, float& OutMax) const;

	// Unreal Cascade 스타일 표시 이름.
	// 예: Distribution Float Constant, Distribution Vector Uniform.
	virtual const char* GetDistributionDisplayName() const;
};