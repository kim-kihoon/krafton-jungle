#pragma once
#include "Actor.h"
#include "PrimitiveComponent.h"


class AStaticMeshActor : public AActor
{
public:
	UPrimitiveComponent* PrimitiveComponent;

	AStaticMeshActor()
	{

	}
};