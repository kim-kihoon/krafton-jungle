#include "pch.h"
#include "RotationGizmoActor.h"
#include "World.h"
#include "RingComponent.h"
#include "SphereComponent.h"
#include "FEditorViewportClient.h"
#include "Input.h"

IMPLEMENT_CLASS(URotationGizmoActor, AActor)

void URotationGizmoActor::BeginPlay()
{
	AActor::BeginPlay();

	BasePoint = AddComponent<USphereComponent>();
	BasePoint->SetScale({ 0.f, 0.f, 0.f });
	RootComponent = BasePoint;

	// 1. X축 회전 링 (Red)
	RingX = AddComponent<URingComponent>();
	RingX->SetRotation({ 0.0f, 0.0f, -90.0f });
	RingX->SetColor(ColorX);
	RingX->SetAlwaysOnTop(true);

	// 2. Y축 회전 링 (Green) - XZ 평면에 배치
	RingY = AddComponent<URingComponent>();
	RingY->SetRotation({ 0.0f, 90.0f, 0.0f }); // 필요에 따라 수정
	RingY->SetColor(ColorY);
	RingY->SetAlwaysOnTop(true);

	// 3. Z축 회전 링 (Blue) - XY 평면에 배치
	RingZ = AddComponent<URingComponent>();
	RingZ->SetRotation({ 90.0f, 0.0f, 180.0f });
	RingZ->SetColor(ColorZ);
	RingZ->SetAlwaysOnTop(true);
}

void URotationGizmoActor::Tick(float DeltaTime)
{
	AActor::Tick(DeltaTime);
	UWorld* World = GetWorld();

	AActor* SelectedActor = World ? World->SelectedActor : nullptr;

	if (!World || World->GetCurrentMode() != UWorld::EGizmoMode::Rotation || !SelectedActor)
	{
		if (RingX) RingX->SetScale({ 0.f, 0.f, 0.f });
		if (RingY) RingY->SetScale({ 0.f, 0.f, 0.f });
		if (RingZ) RingZ->SetScale({ 0.f, 0.f, 0.f });
		return;
	}

	if (SelectedActor && SelectedActor->RootComponent && RootComponent)
	{
		FVector TargetPos = SelectedActor->RootComponent->GetPosition();

		// 위치 동기화
		BasePoint->SetPosition(TargetPos);
		if (RingX) RingX->SetPosition(TargetPos);
		if (RingY) RingY->SetPosition(TargetPos);
		if (RingZ) RingZ->SetPosition(TargetPos);
	}

	if (!RootComponent) return;
	FVector CurrentPos = RootComponent->GetPosition();
	if (RingX) RingX->SetPosition(CurrentPos);
	if (RingY) RingY->SetPosition(CurrentPos);  
	if (RingZ) RingZ->SetPosition(CurrentPos);

	// 카메라 거리에 따른 스케일 보정
	FEditorViewportClient* Viewport = GetWorld()->ViewPort;
	float Distance = (Viewport->GetViewLocation() - RootComponent->GetPosition()).Length();
	float ScaleFactor = Distance * 0.15f;
	FVector ResultScale = { ScaleFactor, ScaleFactor, ScaleFactor };

	if (RingX) RingX->SetScale(ResultScale);
	if (RingY) RingY->SetScale(ResultScale);
	if (RingZ) RingZ->SetScale(ResultScale);

	if (RingX) RingX->SetColor(ColorX);
	if (RingY) RingY->SetColor(ColorY);
	if (RingZ) RingZ->SetColor(ColorZ);

	// --- 호버링 및 드래그 색상 처리 ---
	EGizmoAxis ActiveAxis = EGizmoAxis::None;

	// [수정] World 포인터가 유효할 때만 내부 로직을 수행하도록 안전하게 감쌉니다.
	if (World != nullptr)
	{
		ActiveAxis = World->GetDraggingAxis();

		if (ActiveAxis == EGizmoAxis::None)
		{
			// World가 절대 NULL이 아닌 곳에서만 호출되므로 C6011 에러가 사라집니다.
			ActiveAxis = World->GetHoveredAxis();
		}
	}

	
	if (ActiveAxis == EGizmoAxis::None)
	{
		// 직접 쏘지 않고 통제소(World)의 판정을 얌전히 받습니다.
		ActiveAxis = GetWorld()->GetHoveredAxis();
	}

	switch (ActiveAxis)
	{
	case EGizmoAxis::X: if (RingX) RingX->SetColor(ColorHover); break;
	case EGizmoAxis::Y: if (RingY) RingY->SetColor(ColorHover); break;
	case EGizmoAxis::Z: if (RingZ) RingZ->SetColor(ColorHover); break;
	case EGizmoAxis::None: break;
	}
}

EGizmoAxis URotationGizmoActor::CheckGizmoPicking()
{
	UWorld* World = GetWorld();
	if (!World || !World->ViewPort) return EGizmoAxis::None;

	UInput& Input = UInput::GetInstance();
	FVector2D ScreenPos = { static_cast<float>(Input.GetMousePosition().x), static_cast<float>(Input.GetMousePosition().y) };
	FVector RayDirection;
	FVector RayOrigin;
	World->ViewPort->DeprojectScreenToWorld(ScreenPos, RayOrigin, RayDirection);

	// 회전 링 피킹 검사
	if (World->RayIntersectsMesh(RayOrigin, RayDirection, RingX->GetMesh(), RingX->GetComponentTransform()) >= 0.f) return EGizmoAxis::X;
	if (World->RayIntersectsMesh(RayOrigin, RayDirection, RingY->GetMesh(), RingY->GetComponentTransform()) >= 0.f) return EGizmoAxis::Y;
	if (World->RayIntersectsMesh(RayOrigin, RayDirection, RingZ->GetMesh(), RingZ->GetComponentTransform()) >= 0.f) return EGizmoAxis::Z;

	return EGizmoAxis::None;
}

void URotationGizmoActor::LockDragPlane(EGizmoAxis Axis)
{
	switch (Axis)
	{
	case EGizmoAxis::X: LockedPlaneNormal = RingX->GetUpVector(); break;
	case EGizmoAxis::Y: LockedPlaneNormal = RingY->GetUpVector(); break;
	case EGizmoAxis::Z: LockedPlaneNormal = RingZ->GetUpVector(); break;
	default: LockedPlaneNormal = FVector(0.f, 0.f, 1.f); break;
	}
}

void URotationGizmoActor::ApplyRingRotation(EGizmoAxis Axis, float DeltaAngle)
{
	// 기본 조립 뼈대 (절대 꼬이지 않는 기준점)
	FVector BaseX = { 0.0f, 0.0f, -90.0f };
	FVector BaseY = { 0.0f, 90.0f, 0.0f };
	FVector BaseZ = { 90.0f, 0.0f, 180.0f };

	// 1. 모든 링을 원래 형태로 꽉 잡아둡니다. (자이로스코프 현상 완벽 차단)
	if (RingX) RingX->SetRotation(BaseX);
	if (RingY) RingY->SetRotation(BaseY);
	if (RingZ) RingZ->SetRotation(BaseZ);

	if(Axis == EGizmoAxis::X && RingX) RingX->SetRotation({ 0.f, BaseX.Y + DeltaAngle, BaseX.Z }); // Y를 돌리고 Z로 세운다
	if (Axis == EGizmoAxis::Y && RingY) RingY->SetRotation({ 0.f, BaseY.Y + DeltaAngle, 0.f });   // Y만 돌린다
	if (Axis == EGizmoAxis::Z && RingZ) RingZ->SetRotation({ BaseZ.X, 0.f, BaseZ.Z + DeltaAngle });  // X로 세우고 Z를 돌린다
}

FVector URotationGizmoActor::GetDragIntersectionPoint(const FVector& RayOrg, const FVector& RayDir, EGizmoAxis Axis)
{
	if (!RootComponent) return FVector::Zero;
	FVector GizmoPos = RootComponent->GetPosition();
	
	return Math::RayPlaneIntersection(RayOrg, RayDir, LockedPlaneNormal, GizmoPos);
}

float URotationGizmoActor::GetRotationDelta(const FVector& CurrentIntersect, const FVector& StartIntersect, EGizmoAxis Axis)
{
	if (!RootComponent) return 0.0f;
	FVector GizmoPos = RootComponent->GetPosition();

	FVector V1 = StartIntersect - GizmoPos;
	FVector V2 = CurrentIntersect - GizmoPos;
	V1.Normalize();
	V2.Normalize();

	FVector CrossProd = V1.Cross(V2);
	float DotProd = V1.Dot(V2);

	// 고정된 가상 평면(LockedPlaneNormal)을 축으로 삼아 로드리게스 단일 각도 추출
	float AngleRadian = atan2f(CrossProd.Dot(LockedPlaneNormal), DotProd);
	return Math::ToDegrees(AngleRadian);
}

void URotationGizmoActor::Release()
{
	AActor::Release();
}