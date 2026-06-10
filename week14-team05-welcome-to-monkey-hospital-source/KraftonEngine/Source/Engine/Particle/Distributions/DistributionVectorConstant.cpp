#include "DistributionVectorConstant.h"

const char* UDistributionVectorConstant::GetDistributionDisplayName() const
{
	return "Distribution Vector Constant";
}

void UDistributionVectorConstant::GetRange(FVector& OutMin, FVector& OutMax) const
{
	OutMin = Constant;
	OutMax = Constant;
}
