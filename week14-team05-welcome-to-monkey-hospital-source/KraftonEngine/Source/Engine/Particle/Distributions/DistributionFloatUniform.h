#pragma once
#include "Engine/Particle/Distributions/DistributionFloat.h"
#include "Source/Engine/Particle/Distributions/DistributionFloatUniform.generated.h"

UCLASS()
class UDistributionFloatUniform : public UDistributionFloat
{
public:
	GENERATED_BODY()

	float GetValue(float Time, UObject* Data = nullptr) const override;
	void GetOutRange(float& OutMin, float& OutMax) const override;
	const char* GetDistributionDisplayName() const override;

	UPROPERTY(Edit, Save, Category="Distribution", DisplayName="Min")
	float Min = 0.0f;

	UPROPERTY(Edit, Save, Category = "Distribution", DisplayName = "Max")
	float Max = 1.0f;
};