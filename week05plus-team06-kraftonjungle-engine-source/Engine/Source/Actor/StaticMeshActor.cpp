#include "StaticMeshActor.h"

#include "Actor.h"
#include "Object/Class.h"
#include "Component/StaticMeshComponent.h"

IMPLEMENT_RTTI(AStaticMeshActor, AActor)
void AStaticMeshActor::PostSpawnInitialize()
{
	StaticMeshComp = FObjectFactory::ConstructObject<UStaticMeshComponent>(this, "StaticMeshComponent");
	AddOwnedComponent(StaticMeshComp);

	AActor::PostSpawnInitialize();
}

void AStaticMeshActor::FixupReferences(const FDuplicateionContext& Context)
{
	AActor::FixupReferences(Context);
	if (this->StaticMeshComp)
	{
		this->StaticMeshComp = static_cast<UStaticMeshComponent*>(Context.GetMappedObject(this->StaticMeshComp));
	}
}