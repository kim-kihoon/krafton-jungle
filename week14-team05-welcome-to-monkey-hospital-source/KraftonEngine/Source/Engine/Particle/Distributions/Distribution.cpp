#include "Engine/Particle/Distributions/Distribution.h"

void UDistribution::GetInRange(float& OutMin, float& OutMax) const
{
	OutMin = 0.0f;
	OutMax = 0.0f;
}

const char* UDistribution::GetDistributionDisplayName() const
{
	return "Distribution";
}
