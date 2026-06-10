#pragma once
#include "Engine/Particle/Distributions/DistributionVector.h"
#include "Source/Engine/Particle/Distributions/DistributionVectorConstant.generated.h"

UCLASS()
class UDistributionVectorConstant : public UDistributionVector
{
public:
	GENERATED_BODY()

	FVector GetValue(float Time, UObject* Data = nullptr) const override { return Constant; };
	void GetRange(FVector& OutMin, FVector& OutMax) const override;
	const char* GetDistributionDisplayName() const override;

	UPROPERTY(Edit, Save, Category="Distribution", DisplayName="Constant")
	FVector Constant = {0.0f, 0.0f, 0.0f};
};