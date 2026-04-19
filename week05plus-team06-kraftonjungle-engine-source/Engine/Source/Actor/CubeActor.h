#pragma once

#include "Actor.h"

class UStaticMeshComponent;

class ENGINE_API ACubeActor : public AActor
{
public:
	DECLARE_RTTI(ACubeActor, AActor)
	GENERATE_SHALLOW_CLONE(ACubeActor);

	void PostSpawnInitialize() override;
	void FixupReferences(const FDuplicateionContext& Context) override;

private:
	UStaticMeshComponent*CubeMeshComponent = nullptr;
};