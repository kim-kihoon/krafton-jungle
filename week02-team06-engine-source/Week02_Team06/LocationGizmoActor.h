#pragma once
#include "Actor.h"

class UArrowComponent;
class USphereComponent;

class ULocationGizmoActor : public AActor
{
	DECLARE_CLASS(ULocationGizmoActor, AActor)

public:
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaTime) override;
	virtual void Release() override;

public:
	EGizmoAxis CheckGizmoPicking();
	FVector GetDragIntersectionPoint(const FVector& RayOrg, const FVector& RayDir, EGizmoAxis Axis);
	void LockDragPlane(const FVector& RayOrg);

public:
	UArrowComponent* ArrowY = nullptr; // Y축 초
	UArrowComponent* ArrowX = nullptr; // X축 빨
	UArrowComponent* ArrowZ = nullptr; // Z축 파
	USphereComponent* BasePoint = nullptr;

	//추척된 참조

	// 기즈모 컴포넌트 색상 설정
private:
	const FVector4 ColorX = { 1.f, 0.f, 0.f, 1.f };      // 빨강
	const FVector4 ColorY = { 0.f, 1.f, 0.f, 1.f };      // 초록
	const FVector4 ColorZ = { 0.f, 0.f, 1.f, 1.f };      // 파랑
	const FVector4 ColorCenter = { 1.f, 1.f, 1.f, 1.f }; // 흰색
	const FVector4 ColorHover = { 1.f, 0.5f, 0.f, 1.f }; // 주황

	FVector LockedPlaneNormal = FVector::Zero; // Center 드래그 시 고정된 평면 노멀
};
