#include "DecalActor.h"
#include "Component/DecalComponent.h"
#include "Component/BillboardComponent.h"

IMPLEMENT_CLASS(ADecalActor, AActor)

void ADecalActor::InitDefaultComponents()
{
	DecalComponent = AddComponent<UDecalComponent>();
	SetRootComponent(DecalComponent);

	// 뷰포트에서 선택하기 위한 아이콘 빌보드 추가
	SpriteComponent = AddComponent<UBillboardComponent>();
	SpriteComponent->SetTexture("DecalActor");
	SpriteComponent->SetUsePixelPerfectPicking(true);
	SpriteComponent->AttachToComponent(DecalComponent);
}
