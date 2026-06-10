#pragma once

#include "Engine/Particle/Distributions/DistributionVector.h"
#include "Math/FloatCurve.h"

#include "Source/Engine/Particle/Distributions/DistributionVectorUniformCurve.generated.h"

// =============================================================================
// UDistributionVectorUniformCurve
//   Unreal Cascade의 DistributionVectorUniformCurve 계열처럼 Time -> Min/Max
//   Vector curve를 평가하고, 축별로 그 사이의 랜덤 값을 반환한다.
//   Time의 의미는 호출하는 모듈이 결정한다.
// =============================================================================
UCLASS()
class UDistributionVectorUniformCurve : public UDistributionVector
{
public:
	GENERATED_BODY()
	UDistributionVectorUniformCurve();

	FVector GetValue(float Time, UObject* Data = nullptr) const override;
	void GetInRange(float& OutMin, float& OutMax) const override;
	void GetRange(FVector& OutMin, FVector& OutMax) const override;
	const char* GetDistributionDisplayName() const override;

	FFloatCurve& GetMinXCurve() { return MinXCurve; }
	FFloatCurve& GetMinYCurve() { return MinYCurve; }
	FFloatCurve& GetMinZCurve() { return MinZCurve; }
	FFloatCurve& GetMaxXCurve() { return MaxXCurve; }
	FFloatCurve& GetMaxYCurve() { return MaxYCurve; }
	FFloatCurve& GetMaxZCurve() { return MaxZCurve; }

	const FFloatCurve& GetMinXCurve() const { return MinXCurve; }
	const FFloatCurve& GetMinYCurve() const { return MinYCurve; }
	const FFloatCurve& GetMinZCurve() const { return MinZCurve; }
	const FFloatCurve& GetMaxXCurve() const { return MaxXCurve; }
	const FFloatCurve& GetMaxYCurve() const { return MaxYCurve; }
	const FFloatCurve& GetMaxZCurve() const { return MaxZCurve; }

	void SetConstant(const FVector& InMin, const FVector& InMax);
	void SetLinear(float StartTime, const FVector& StartMin, const FVector& StartMax,
	               float EndTime, const FVector& EndMin, const FVector& EndMax);

private:
	UPROPERTY(Save, Type=Struct, Struct=FFloatCurve)
	FFloatCurve MinXCurve;

	UPROPERTY(Save, Type=Struct, Struct=FFloatCurve)
	FFloatCurve MinYCurve;

	UPROPERTY(Save, Type=Struct, Struct=FFloatCurve)
	FFloatCurve MinZCurve;

	UPROPERTY(Save, Type=Struct, Struct=FFloatCurve)
	FFloatCurve MaxXCurve;

	UPROPERTY(Save, Type=Struct, Struct=FFloatCurve)
	FFloatCurve MaxYCurve;

	UPROPERTY(Save, Type=Struct, Struct=FFloatCurve)
	FFloatCurve MaxZCurve;
};
