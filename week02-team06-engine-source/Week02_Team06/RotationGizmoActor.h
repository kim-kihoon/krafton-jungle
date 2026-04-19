#pragma once
#include "Actor.h"

class URingComponent;
class USphereComponent;

class URotationGizmoActor : public AActor
{
	DECLARE_CLASS(URotationGizmoActor, AActor)

public:
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaTime) override;
	virtual void Release() override;
	void LockDragPlane(EGizmoAxis Axis);
	void ApplyRingRotation(EGizmoAxis Axis, float DeltaAngle);

	EGizmoAxis CheckGizmoPicking();
	FVector GetDragIntersectionPoint(const FVector& RayOrg, const FVector& RayDir, EGizmoAxis Axis);
	float GetRotationDelta(const FVector& CurrentIntersect, const FVector& StartIntersect, EGizmoAxis Axis);


public:
	URingComponent* RingX = nullptr; // X축 회전용 (Red)
	URingComponent* RingY = nullptr; // Y축 회전용 (Green)
	URingComponent* RingZ = nullptr; // Z축 회전용 (Blue)
	USphereComponent* BasePoint = nullptr;

private:
	// 기즈모 컴포넌트 색상 설정
	const FVector4 ColorX = { 1.f, 0.f, 0.f, 1.f };      // 빨강
	const FVector4 ColorY = { 0.f, 1.f, 0.f, 1.f };      // 초록
	const FVector4 ColorZ = { 0.f, 0.f, 1.f, 1.f };      // 파랑
	const FVector4 ColorHover = { 1.f, 0.5f, 0.f, 1.f }; // 주황
	FVector LockedPlaneNormal = { 0.f, 0.f, 1.f };
};