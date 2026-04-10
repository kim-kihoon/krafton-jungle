#pragma once
#include "CoreMinimal.h"
#include "Actor/Actor.h"

class UStaticMeshComponent;
class URandomColorComponent;

class ENGINE_API AStaticMeshActor : public AActor
{
public:
	DECLARE_RTTI(AStaticMeshActor, AActor)
	GENERATE_SHALLOW_CLONE(AStaticMeshActor);

	virtual void PostSpawnInitialize() override;

	void FixupReferences(const FDuplicateionContext& Context) override;

private:
	UStaticMeshComponent* StaticMeshComp = nullptr;
};

