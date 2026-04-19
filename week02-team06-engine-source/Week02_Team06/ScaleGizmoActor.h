#pragma once
#include "Actor.h"

class UHammerComponent;
class UCubeComponent;

class UScaleGizmoActor : public AActor
{
	DECLARE_CLASS(UScaleGizmoActor, AActor)

public:
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaTime) override;
	virtual void Release() override;

	EGizmoAxis CheckGizmoPicking();
	FVector GetDragIntersectionPoint(const FVector& RayOrg, const FVector& RayDir, EGizmoAxis Axis);


public:
	UHammerComponent* HammerX = nullptr; // X축 스케일용 (Red)
	UHammerComponent* HammerY = nullptr; // Y축 스케일용 (Green)
	UHammerComponent* HammerZ = nullptr; // Z축 스케일용 (Blue)
	UCubeComponent* BasePoint = nullptr;

private:
	// 기즈모 컴포넌트 색상 설정
	const FVector4 ColorX = { 1.f, 0.f, 0.f, 1.f };      // 빨강
	const FVector4 ColorY = { 0.f, 1.f, 0.f, 1.f };      // 초록
	const FVector4 ColorZ = { 0.f, 0.f, 1.f, 1.f };      // 파랑
	const FVector4 ColorHover = { 1.f, 0.5f, 0.f, 1.f }; // 주황
};
