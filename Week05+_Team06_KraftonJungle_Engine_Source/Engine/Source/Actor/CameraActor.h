#pragma once

#include "Actor/Actor.h"

class UCameraComponent;
class UBillboardComponent;
class UCameraArrowComponent;

/*
레벨에 배치 가능한 카메라 액터.
에디터에서 배치한 뒤 PIE를 시작하면 이 액터의 시점으로 카메라가 전환됨.
UE의 ACameraActor와 동일한 역할
*/
class ENGINE_API ACameraActor : public AActor
{
public:
	DECLARE_RTTI(ACameraActor, AActor)
	GENERATE_SHALLOW_CLONE(ACameraActor)

	UCameraComponent* GetCameraComponent() const { return CameraComponent; }

	void PostSpawnInitialize() override;
	void FixupReferences(const FDuplicateionContext& Context) override;

private:
	UCameraComponent* CameraComponent = nullptr;
	UBillboardComponent* IconBillboard = nullptr;
	UCameraArrowComponent* ArrowX = nullptr;
};