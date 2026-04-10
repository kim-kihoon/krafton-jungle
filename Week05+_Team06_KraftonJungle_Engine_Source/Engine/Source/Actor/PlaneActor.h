#pragma once

#include "Actor.h"

class UStaticMeshComponent;

class ENGINE_API APlaneActor : public AActor
{
public:
	DECLARE_RTTI(APlaneActor, AActor)
	GENERATE_SHALLOW_CLONE(APlaneActor);

	void PostSpawnInitialize() override;
	void FixupReferences(const FDuplicateionContext& Context) override;

private:
	UStaticMeshComponent* PlaneMeshComponent = nullptr;
};