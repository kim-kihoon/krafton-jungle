#pragma once
#include "Engine/Particle/Distributions/DistributionVector.h"
#include "Source/Engine/Particle/Distributions/DistributionVectorUniform.generated.h"

UCLASS()
class UDistributionVectorUniform : public UDistributionVector
{
public:
	GENERATED_BODY()

	FVector GetValue(float Time, UObject* Data = nullptr) const override;
	void GetRange(FVector& OutMin, FVector& OutMax) const override;
	const char* GetDistributionDisplayName() const override;

	UPROPERTY(Edit, Save, Category="Distribution", DisplayName="Min")
	FVector Min = {0.0f, 0.0f, 0.0f};

	UPROPERTY(Edit, Save, Category = "Distribution", DisplayName = "Max")
	FVector Max = {1.0f, 1.0f, 1.0f};
};
