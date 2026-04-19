#include "pch.h"
#include "ScaleGizmoActor.h"
#include "World.h"
#include "HammerComponent.h"
#include "CubeComponent.h"
#include "SphereComponent.h"
#include "FEditorViewportClient.h"

IMPLEMENT_CLASS(UScaleGizmoActor, AActor)

void UScaleGizmoActor::BeginPlay()
{
	AActor::BeginPlay();

	FVector RelativeScaleFactor = { 8.f, 8.f, 8.f };

	HammerY = AddComponent<UHammerComponent>();
	HammerY->SetColor(ColorY);
	HammerY->SetRelativeScale(RelativeScaleFactor);

	HammerX = AddComponent<UHammerComponent>();
	HammerX->SetRotation({ 0.0f, 0.0f, -90.0f });
	HammerX->SetColor(ColorX);
	HammerX->SetRelativeScale(RelativeScaleFactor);

	HammerZ = AddComponent<UHammerComponent>();
	HammerZ->SetRotation({ 90.0f, 0.0f, 0.0f });
	HammerZ->SetColor(ColorZ);
	HammerZ->SetRelativeScale(RelativeScaleFactor);

	BasePoint = AddComponent<UCubeComponent>();
	BasePoint->SetColor({ 1.f, 1.f, 1.f, 1.f });
	BasePoint->SetRelativeScale({ 0.2f, 0.2f, 0.2f });

	HammerY->SetAlwaysOnTop(true);
	HammerX->SetAlwaysOnTop(true);
	HammerZ->SetAlwaysOnTop(true);
	BasePoint->SetAlwaysOnTop(true);

	RootComponent = BasePoint;
}
void UScaleGizmoActor::Tick(float DeltaTime)
{
	AActor::Tick(DeltaTime);
	UWorld* World = GetWorld();

	AActor* SelectedActor = World->SelectedActor;

	if (!World || World->GetCurrentMode() != UWorld::EGizmoMode::Scale || !SelectedActor)
	{
		BasePoint->SetScale({ 0.f, 0.f, 0.f });
		if (HammerX) HammerX->SetScale({ 0.f, 0.f, 0.f });
		if (HammerY) HammerY->SetScale({ 0.f, 0.f, 0.f });
		if (HammerZ)HammerZ->SetScale({ 0.f, 0.f, 0.f });
		return;
	}

	if (SelectedActor && SelectedActor->RootComponent && RootComponent)
	{
		FVector TargetPos = SelectedActor->RootComponent->GetPosition();
		FVector TargetRot = SelectedActor->RootComponent->GetRotation(); // 오일러 각도

		// 위치 동기화
		BasePoint->SetPosition(TargetPos);
		if (HammerX) HammerX->SetPosition(TargetPos);
		if (HammerY) HammerY->SetPosition(TargetPos);
		if (HammerZ) HammerZ->SetPosition(TargetPos);

		// 회전 동기화 (행렬 공간에서 결합하여 짐벌락 방지)
		FMatrix TargetRotMat = FMatrix::MakeRotation(TargetRot);

		BasePoint->SetRotation(TargetRot);
		if (HammerY) HammerY->SetRotation(TargetRot); // Y는 기본 회전이 0이므로 그대로 적용

		if (HammerX)
		{
			// X축: 원래 Z축으로 -90도 누워있었으므로, 그 상태에서 타겟 회전을 곱함
			FMatrix MatX = FMatrix::MakeRotationZ(-90.0f) * TargetRotMat;
			HammerX->SetRotation(Math::MatrixToEuler(MatX));
		}
		if (HammerZ)
		{
			// Z축: 원래 X축으로 90도 누워있었으므로, 그 상태에서 타겟 회전을 곱함
			FMatrix MatZ = FMatrix::MakeRotationX(90.0f) * TargetRotMat;
			HammerZ->SetRotation(Math::MatrixToEuler(MatZ));
		}
	}

	FEditorViewportClient* Viewport = GetWorld()->ViewPort;
	float Distance = (Viewport->GetViewLocation() - RootComponent->GetComponentLocation()).Length();
	float ScaleFactor = Distance * 0.15f;
	FVector ResultScale = { ScaleFactor, ScaleFactor, ScaleFactor };

	BasePoint->SetScale({ 0.1f * ScaleFactor, 0.1f * ScaleFactor, 0.1f * ScaleFactor });
	if (HammerX) HammerX->SetScale({ ScaleFactor, ScaleFactor , ScaleFactor });
	if (HammerY) HammerY->SetScale({ ScaleFactor, ScaleFactor , ScaleFactor });
	if (HammerZ) HammerZ->SetScale({ ScaleFactor, ScaleFactor , ScaleFactor });
	// 색상 초기화
	HammerX->SetColor(ColorX);
	HammerY->SetColor(ColorY);
	HammerZ->SetColor(ColorZ);
	BasePoint->SetColor({ 1.f, 1.f, 1.f, 1.f });

	// 활성화된 축 하이라이트
	EGizmoAxis ActiveAxis = GetWorld()->GetDraggingAxis();
	if (ActiveAxis == EGizmoAxis::None) ActiveAxis = GetWorld()->GetHoveredAxis();

	switch (ActiveAxis)
	{
	case EGizmoAxis::X: HammerX->SetColor(ColorHover); break;
	case EGizmoAxis::Y: HammerY->SetColor(ColorHover); break;
	case EGizmoAxis::Z: HammerZ->SetColor(ColorHover); break;
	case EGizmoAxis::Center: BasePoint->SetColor(ColorHover); break;
	}
}

void UScaleGizmoActor::Release()
{
	AActor::Release();
}

EGizmoAxis UScaleGizmoActor::CheckGizmoPicking()
{
	UWorld* World = GetWorld();
	if (!World || !World->ViewPort) return EGizmoAxis::None;

	FVector RayOrigin = World->ViewPort->GetViewLocation();
	FVector RayDir = World->ViewPort->GetCameraRayDirection();

	// 큐브(Center)를 가장 먼저 검사 (제일 작고 중앙에 있으므로)
	if (World->RayIntersectsMesh(RayOrigin, RayDir, BasePoint->GetMesh(), BasePoint->GetComponentTransform()) >= 0.f)
		return EGizmoAxis::Center;

	if (World->RayIntersectsMesh(RayOrigin, RayDir, HammerX->GetMesh(), HammerX->GetComponentTransform()) >= 0.f) return EGizmoAxis::X;
	if (World->RayIntersectsMesh(RayOrigin, RayDir, HammerY->GetMesh(), HammerY->GetComponentTransform()) >= 0.f) return EGizmoAxis::Y;
	if (World->RayIntersectsMesh(RayOrigin, RayDir, HammerZ->GetMesh(), HammerZ->GetComponentTransform()) >= 0.f) return EGizmoAxis::Z;

	return EGizmoAxis::None;
}

FVector UScaleGizmoActor::GetDragIntersectionPoint(const FVector& RayOrg, const FVector& RayDir, EGizmoAxis Axis)
{
	if (!RootComponent) return FVector::Zero;

	FVector GizmoPos = RootComponent->GetPosition();

	// Center (가운데 큐브)를 잡았을 때는 카메라를 바라보는 가상 평면과의 교차점을 구합니다.
	if (Axis == EGizmoAxis::Center)
	{
		FVector PlaneNormal = RayOrg - GizmoPos;
		PlaneNormal.Normalize();
		return Math::RayPlaneIntersection(RayOrg, RayDir, PlaneNormal, GizmoPos);
	}

	// 특정 축(X, Y, Z)을 잡았을 때는 해당 축의 선분과 가장 가까운 점을 구합니다.
	FVector AxisDir;
	switch (Axis)
	{
	case EGizmoAxis::X: AxisDir = HammerX->GetUpVector(); break;
	case EGizmoAxis::Y: AxisDir = HammerY->GetUpVector(); break;
	case EGizmoAxis::Z: AxisDir = HammerZ->GetUpVector(); break;
	default: return GizmoPos;
	}

	return Math::ClosestPointOnLine(RayOrg, RayDir, GizmoPos, AxisDir);
}