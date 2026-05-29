#pragma once

#include "Component/PrimitiveComponent.h"
#include "MeshComponent.generated.h"

UCLASS(HiddenInComponentList)
class UMeshComponent : public UPrimitiveComponent
{
public:
	GENERATED_BODY(UMeshComponent)

	UMeshComponent() = default;
	~UMeshComponent() override = default;
};
