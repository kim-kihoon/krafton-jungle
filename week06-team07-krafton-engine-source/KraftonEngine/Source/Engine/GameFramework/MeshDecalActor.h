#pragma once
#include "GameFramework/AActor.h"
class UTextRenderComponent;
class UMeshDecalComponent;
class UBillboardComponent;
class AMeshDecalActor : public AActor
{
public:
	DECLARE_CLASS(AMeshDecalActor, AActor)
	AMeshDecalActor() {}

	//void InitDefaultComponents(const FString& UStaticMeshFileName);
	void InitDefaultComponents();

private:
	UMeshDecalComponent* MeshDecalComponent = nullptr;
	UTextRenderComponent* TextRenderComponent = nullptr;
	UBillboardComponent* BillboardComponent = nullptr;
};