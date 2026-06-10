#pragma once

#include "Engine/Particle/Distributions/DistributionFloat.h"
#include "Math/FloatCurve.h"

#include "Source/Engine/Particle/Distributions/DistributionFloatCurve.generated.h"

// =============================================================================
// UDistributionFloatCurve
//   Unreal Cascade의 DistributionFloatConstantCurve 계열처럼 Time -> float 값을
//   FFloatCurve로 평가한다. Time의 의미는 호출하는 모듈이 결정한다.
//   - Initial 계열 모듈: SpawnTime(emitter loop seconds)
//   - Over-Life 계열 모듈: Particle->RelativeTime(0..1)
// =============================================================================
UCLASS()
class UDistributionFloatCurve : public UDistributionFloat
{
public:
	GENERATED_BODY()
	UDistributionFloatCurve();

	float GetValue(float Time, UObject* Data = nullptr) const override;
	void GetInRange(float& OutMin, float& OutMax) const override;
	void GetOutRange(float& OutMin, float& OutMax) const override;
	const char* GetDistributionDisplayName() const override;

	FFloatCurve& GetCurve() { return Curve; }
	const FFloatCurve& GetCurve() const { return Curve; }

	void SetConstant(float Value);
	void SetLinear(float StartTime, float StartValue, float EndTime, float EndValue);

private:
	UPROPERTY(Save, Type=Struct, Struct=FFloatCurve)
	FFloatCurve Curve;
};
