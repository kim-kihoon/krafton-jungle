#include "FireBallActor.h"
#include "Component/StaticMeshComponent.h"
#include "Component/FireBallComponent.h"
#include "Mesh/ObjManager.h"
#include "Engine/Runtime/Engine.h"
IMPLEMENT_CLASS(AFireBallActor, AActor)

AFireBallActor::AFireBallActor()
{
}

void AFireBallActor::InitDefaultComponents()
{
	StaticMeshComp = AddComponent<UStaticMeshComponent>();
	SetRootComponent(StaticMeshComp);
	ID3D11Device* Device = GEngine->GetRenderer().GetFD3DDevice().GetDevice();
	UStaticMesh* DefaultMesh = FObjManager::LoadObjStaticMesh("Data/BasicShape/Sphere.OBJ", Device);
	if (DefaultMesh)
	{
		StaticMeshComp->SetStaticMesh(DefaultMesh);
	}
	FireBallComp = AddComponent<UFireBallComponent>();
	FireBallComp->AttachToComponent(StaticMeshComp);
}

void AFireBallActor::TickActor(float DeltaSeconds, ELevelTick TickType, FActorTickFunction& ThisTickFunction)
{
}



