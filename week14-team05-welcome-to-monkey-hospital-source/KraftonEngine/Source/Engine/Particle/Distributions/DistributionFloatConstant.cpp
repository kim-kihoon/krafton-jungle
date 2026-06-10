#include "Engine/Particle/Distributions/DistributionFloatConstant.h"

const char* UDistributionFloatConstant::GetDistributionDisplayName() const
{
	return "Distribution Float Constant";
}

void UDistributionFloatConstant::GetOutRange(float& OutMin, float& OutMax) const
{
	OutMin = Constant;
	OutMax = Constant;
}
