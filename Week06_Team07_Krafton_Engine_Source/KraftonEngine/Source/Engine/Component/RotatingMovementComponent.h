#pragma once

#include "MovementComponent.h"
#include "Math/Rotator.h"

// 런타임 동안 UpdatedComponent를 일정 각속도로 회전시키는 이동 컴포넌트
class UPrimitiveComponent;
class URotatingMovementComponent : public UMovementComponent
{
public:
	DECLARE_CLASS(URotatingMovementComponent, UMovementComponent)

	URotatingMovementComponent() = default;
	~URotatingMovementComponent() override = default;

	void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction& ThisTickFunction) override;
	void GetEditableProperties(TArray<FPropertyDescriptor>& OutProps) override;
	void Serialize(FArchive& Ar) override;
	void CollectEditorVisualizations(FRenderBus& RenderBus) const override;

private:
	// 초당 회전 속도 (Pitch, Yaw, Roll)
	FRotator RotationRate = FRotator(0.0f, 90.0f, 0.0f);

	// 회전 중심점 오프셋 (UpdatedComponent의 위치 기준 로컬 오프셋)
	FVector PivotTranslation = FVector(0.0f, 0.0f, 0.0f);
	//UPrimitiveComponent* PivotComponent = nullptr;  // 회전 중심으로 사용할 컴포넌트 (null이면 UpdatedComponent의 위치 기준)

	// 로컬 공간에서 회전할지 여부
	bool bRotationInLocalSpace = true;



};
