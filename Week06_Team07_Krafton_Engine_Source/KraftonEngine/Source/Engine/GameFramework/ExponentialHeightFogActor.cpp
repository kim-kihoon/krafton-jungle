#include "ExponentialHeightFogActor.h"
#include "Object/ObjectFactory.h"
#include "Component/HeightFogComponent.h"
#include "Component/BillboardComponent.h"

IMPLEMENT_CLASS(AExponentialHeightFogActor, AActor)

void AExponentialHeightFogActor::InitDefaultComponents()
{
	FogComponent = AddComponent<UHeightFogComponent>();
	SetRootComponent(FogComponent);

	SpriteComponent = AddComponent<UBillboardComponent>();
	SpriteComponent->SetTexture("VolumetricCloud");
	SpriteComponent->AttachToComponent(FogComponent);
}
