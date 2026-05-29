#pragma once

#include "GameFramework/AActor.h"
#include "StaticMeshActor.generated.h"

class UStaticMeshComponent;
class UTextRenderComponent;
class USubUVComponent;

UCLASS(Actor)
class AStaticMeshActor : public AActor
{
public:
	GENERATED_BODY(AStaticMeshActor)
	AStaticMeshActor() {}

	void BeginPlay() override;

	void InitDefaultComponents(const FString& UStaticMeshFileName = "Asset/Mesh/BasicShape/Cylinder_StaticMesh.uasset");

private:
	UStaticMeshComponent* StaticMeshComponent = nullptr;
	//UTextRenderComponent* TextRenderComponent = nullptr;
	//USubUVComponent* SubUVComponent = nullptr;
};
