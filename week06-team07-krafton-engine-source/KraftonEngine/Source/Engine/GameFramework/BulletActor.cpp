#include "BulletActor.h"
#include "Component/StaticMeshComponent.h"
#include "Component/ProjectileMovementComponent.h"

void ABulletActor::InitDefaultComponents()
{
	MeshComponent = AddComponent<UStaticMeshComponent>();
	SetRootComponent(MeshComponent);

	MovementComp = AddComponent<UProjectileMovementComponent>();

}
