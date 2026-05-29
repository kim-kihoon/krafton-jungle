#pragma once
#include "Math/Vector.h"

struct FParticleLODContext
{
	bool bValid = false;
	FVector ViewPosition = FVector::ZeroVector;
	FVector ViewForward = FVector(1.f,0.f,0.f);
};
