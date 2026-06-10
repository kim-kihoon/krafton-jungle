#include "Engine/Particle/Distributions/DistributionVectorUniform.h"

#include <algorithm>
#include <cstdlib>

namespace
{
	float RandomFloat()
	{
		return static_cast<float>(std::rand()) / static_cast<float>(RAND_MAX);
	}

	float RandomRange(float InMin, float InMax)
	{
		const float SafeMin = std::min(InMin, InMax);
		const float SafeMax = std::max(InMin, InMax);

		const float Alpha = RandomFloat();
		return SafeMin + (SafeMax - SafeMin) * Alpha;
	}
}

const char* UDistributionVectorUniform::GetDistributionDisplayName() const
{
	return "Distribution Vector Uniform";
}

FVector UDistributionVectorUniform::GetValue(float Time, UObject* Data) const
{
	(void)Time;
	(void)Data;

	return FVector(
		RandomRange(Min.X, Max.X),
		RandomRange(Min.Y, Max.Y),
		RandomRange(Min.Z, Max.Z)
	);
}

void UDistributionVectorUniform::GetRange(FVector& OutMin, FVector& OutMax) const
{
	OutMin = FVector(
		std::min(Min.X, Max.X),
		std::min(Min.Y, Max.Y),
		std::min(Min.Z, Max.Z)
	);

	OutMax = FVector(
		std::max(Min.X, Max.X),
		std::max(Min.Y, Max.Y),
		std::max(Min.Z, Max.Z)
	);
}