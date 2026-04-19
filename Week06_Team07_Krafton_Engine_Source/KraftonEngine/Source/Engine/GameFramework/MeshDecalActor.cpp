#include "MeshDecalActor.h"
#include "Component/BillboardComponent.h"
#include "Component/TextRenderComponent.h"
#include "Component/MeshDecalComponent.h"
#include "Engine/Runtime/Engine.h"

IMPLEMENT_CLASS(AMeshDecalActor, AActor)
void AMeshDecalActor::InitDefaultComponents()
{
	MeshDecalComponent = AddComponent<UMeshDecalComponent>();
	SetRootComponent(MeshDecalComponent);

	ID3D11Device* Device = GEngine->GetRenderer().GetFD3DDevice().GetDevice();

	TextRenderComponent = AddComponent<UTextRenderComponent>();
	TextRenderComponent->SetRelativeLocation(FVector(0.0f, 0.0f, 1.3f));
	TextRenderComponent->SetText("UUID : " + TextRenderComponent->GetOwnerUUIDToString());
	TextRenderComponent->AttachToComponent(MeshDecalComponent);
	TextRenderComponent->SetFont(FName("Default"));

	BillboardComponent = AddComponent<UBillboardComponent>();
	BillboardComponent->SetRelativeLocation(FVector(0.0f, 0.0f, 0.0f));
	BillboardComponent->AttachToComponent(MeshDecalComponent);
	BillboardComponent->SetTexture(FName("DecalActor"));
	BillboardComponent->SetSpriteSize(1.f, 1.f);
}
