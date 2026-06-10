#include "Engine/Particle/Distributions/DistributionFloatUniform.h"

#include <algorithm>
#include <cstdlib>

namespace
{
	float RandomFloat()
	{
		return static_cast<float>(std::rand()) / static_cast<float>(RAND_MAX);
	}

}

const char* UDistributionFloatUniform::GetDistributionDisplayName() const
{
	return "Distribution Float Uniform";
}

float UDistributionFloatUniform::GetValue(float Time, UObject* Data) const
{
	(void)Time;
	(void)Data;

	const float SafeMin = std::min(Min, Max);
	const float SafeMax = std::max(Min, Max);

	const float Alpha = RandomFloat();
	return SafeMin + (SafeMax - SafeMin) * Alpha;
}

void UDistributionFloatUniform::GetOutRange(float& OutMin, float& OutMax) const
{
	OutMin = std::min(Min, Max);
	OutMax = std::max(Min, Max);
}