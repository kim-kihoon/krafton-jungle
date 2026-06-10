#pragma once

#include "Engine/Particle/Distributions/DistributionFloat.h"
#include "Math/FloatCurve.h"

#include "Source/Engine/Particle/Distributions/DistributionFloatUniformCurve.generated.h"

// =============================================================================
// UDistributionFloatUniformCurve
//   Unreal Cascade의 DistributionFloatUniformCurve 계열처럼 Time -> Min/Max
//   두 개의 float curve를 평가하고, 그 사이의 랜덤 값을 반환한다.
//   Time의 의미는 호출하는 모듈이 결정한다.
// =============================================================================
UCLASS()
class UDistributionFloatUniformCurve : public UDistributionFloat
{
public:
	GENERATED_BODY()
	UDistributionFloatUniformCurve();

	float GetValue(float Time, UObject* Data = nullptr) const override;
	void GetInRange(float& OutMin, float& OutMax) const override;
	void GetOutRange(float& OutMin, float& OutMax) const override;
	const char* GetDistributionDisplayName() const override;

	FFloatCurve& GetMinCurve() { return MinCurve; }
	FFloatCurve& GetMaxCurve() { return MaxCurve; }
	const FFloatCurve& GetMinCurve() const { return MinCurve; }
	const FFloatCurve& GetMaxCurve() const { return MaxCurve; }

	void SetConstant(float InMin, float InMax);
	void SetLinear(float StartTime, float StartMin, float StartMax, float EndTime, float EndMin, float EndMax);

private:
	UPROPERTY(Save, Type=Struct, Struct=FFloatCurve)
	FFloatCurve MinCurve;

	UPROPERTY(Save, Type=Struct, Struct=FFloatCurve)
	FFloatCurve MaxCurve;
};
