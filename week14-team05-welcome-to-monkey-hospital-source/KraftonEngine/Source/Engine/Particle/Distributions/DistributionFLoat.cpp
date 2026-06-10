#include "Engine/Particle/Distributions/DistributionFloat.h"

float UDistributionFloat::GetValue(float Time, UObject* Data) const
{
	(void)Time;
	(void)Data;
	return 0.0f;
}

void UDistributionFloat::GetOutRange(float& OutMin, float& OutMax) const
{
	OutMin = 0.0f;
	OutMax = 0.0f;
}
