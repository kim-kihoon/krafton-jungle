#include "SpotLightActor.h"
#include "Component/DecalComponent.h"
#include "Component/BillboardComponent.h"
#include "Component/SceneComponent.h"

IMPLEMENT_CLASS(ASpotLightActor, AActor)

void ASpotLightActor::InitDefaultComponents()
{
	// 액터의 기준점이 될 비가시적 루트 컴포넌트 생성 (PIE에서도 항상 존재)
	Root = AddComponent<USceneComponent>();
	SetRootComponent(Root);

	// 빛의 근원지 아이콘 (에디터 전용)
	LightSource = AddComponent<UBillboardComponent>();
	LightSource->AttachToComponent(Root);
	LightSource->SetTexture("SpotLight");

	// 바닥에 맺히는 빛
	FloorDecal = AddComponent<UDecalComponent>();
	FloorDecal->AttachToComponent(Root);
	FloorDecal->SetTexture("Floor_Light");
	FloorDecal->SetRelativeRotation(FVector(0.f, 90.f, 0.f));

	// 거리가 멀어지거나 가장자리로 갈수록 부드럽게 사라지는 페이드(Fade) 효과 활성화
	// Inner(0.2)부터 서서히 흐려지기 시작해서 Outer(0.8)에서 완전히 투명해짐
	FloorDecal->SetFade(true, 0.2f, 0.8f);

	// 빛이 맺히는 바닥의 위치 (절댓값 1 활용)
	FloorDecal->SetRelativeLocation(FVector(0.f, 0.f, -1.f));
	FloorDecal->SetRelativeScale(FVector(2.f, 1.f, 1.f));
}
