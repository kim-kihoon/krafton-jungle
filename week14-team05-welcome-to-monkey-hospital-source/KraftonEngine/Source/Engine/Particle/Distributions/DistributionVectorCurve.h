#pragma once

#include "Engine/Particle/Distributions/DistributionVector.h"
#include "Math/FloatCurve.h"

#include "Source/Engine/Particle/Distributions/DistributionVectorCurve.generated.h"

// =============================================================================
// UDistributionVectorCurve
//   Time -> FVector 값을 X/Y/Z 세 개의 FFloatCurve로 평가한다.
//   Time의 의미는 호출 모듈이 결정한다.
//   - Initial 계열 모듈: SpawnTime(emitter loop seconds)
//   - Over-Life 계열 모듈: Particle->RelativeTime(0..1)
// =============================================================================
UCLASS()
class UDistributionVectorCurve : public UDistributionVector
{
public:
	GENERATED_BODY()
	UDistributionVectorCurve();

	FVector GetValue(float Time, UObject* Data = nullptr) const override;
	void GetInRange(float& OutMin, float& OutMax) const override;
	void GetRange(FVector& OutMin, FVector& OutMax) const override;
	const char* GetDistributionDisplayName() const override;

	FFloatCurve& GetXCurve() { return XCurve; }
	FFloatCurve& GetYCurve() { return YCurve; }
	FFloatCurve& GetZCurve() { return ZCurve; }

	const FFloatCurve& GetXCurve() const { return XCurve; }
	const FFloatCurve& GetYCurve() const { return YCurve; }
	const FFloatCurve& GetZCurve() const { return ZCurve; }

	void SetConstant(const FVector& Value);
	void SetLinear(float StartTime, const FVector& StartValue, float EndTime, const FVector& EndValue);

private:
	UPROPERTY(Save, Type=Struct, Struct=FFloatCurve)
	FFloatCurve XCurve;

	UPROPERTY(Save, Type=Struct, Struct=FFloatCurve)
	FFloatCurve YCurve;

	UPROPERTY(Save, Type=Struct, Struct=FFloatCurve)
	FFloatCurve ZCurve;
};
