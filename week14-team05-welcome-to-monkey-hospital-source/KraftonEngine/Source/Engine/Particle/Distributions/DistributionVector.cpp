#include "Engine/Particle/Distributions/DistributionVector.h"

FVector UDistributionVector::GetValue(float Time, UObject* Data) const
{
	(void)Time;
	(void)Data;

	return FVector(0.0f, 0.0f, 0.0f);
}

void UDistributionVector::GetRange(FVector& OutMin, FVector& OutMax) const
{
	OutMin = FVector(0.0f, 0.0f, 0.0f);
	OutMax = FVector(0.0f, 0.0f, 0.0f);
}

