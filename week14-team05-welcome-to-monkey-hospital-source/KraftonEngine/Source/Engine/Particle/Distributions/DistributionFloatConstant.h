#pragma once
#include "Engine/Particle/Distributions/DistributionFloat.h"
#include "Source/Engine/Particle/Distributions/DistributionFloatConstant.generated.h"

UCLASS()
class UDistributionFloatConstant : public UDistributionFloat
{
public:
	GENERATED_BODY()

	float GetValue(float Time, UObject* Data = nullptr) const override { return Constant; }
	void GetOutRange(float& OutMin, float& OutMax) const override;
	const char* GetDistributionDisplayName() const override;

	UPROPERTY(Edit, Save, Category="Distribution", DisplayName="Constant")
	float Constant = 0.0f;
};