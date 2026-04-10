#include "PlaneActor.h"

#include "Asset/ObjManager.h"
#include "Component/StaticMeshComponent.h"
#include "Core/Paths.h"
#include "Object/ObjectFactory.h"
#include "Object/Class.h"

IMPLEMENT_RTTI(APlaneActor, AActor)

void APlaneActor::PostSpawnInitialize()
{
	UStaticMesh* PlaneMesh = nullptr;
	PlaneMesh = FObjManager::LoadModelStaticMeshAsset(FPaths::FromPath(FPaths::MeshDir() / "PrimitivePlane.Model"));

	PlaneMeshComponent = FObjectFactory::ConstructObject<UStaticMeshComponent>(this, "StaticMeshComponent");
	PlaneMeshComponent->SetStaticMesh(PlaneMesh);

	AddOwnedComponent(PlaneMeshComponent);

	AActor::PostSpawnInitialize();
}


void APlaneActor::FixupReferences(const FDuplicateionContext& Context)
{
	AActor::FixupReferences(Context);
	if (this->PlaneMeshComponent)
	{
		this->PlaneMeshComponent = static_cast<UStaticMeshComponent*>(Context.GetMappedObject(this->PlaneMeshComponent));
	}
}
