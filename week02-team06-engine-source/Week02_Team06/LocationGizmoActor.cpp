#include "pch.h"
#include "LocationGizmoActor.h"
#include "World.h"
#include "ArrowComponent.h"
#include "SphereComponent.h"
#include "FEditorViewportClient.h"
#include "Input.h"

IMPLEMENT_CLASS(ULocationGizmoActor, AActor)

void ULocationGizmoActor::BeginPlay()
{
	AActor::BeginPlay();

	FVector RelativeScaleFactor = { 10.f, 10.f, 10.f };

	ArrowY = AddComponent<UArrowComponent>();
	ArrowY->SetColor({0.f, 1.f, 0.f, 1.f});
	ArrowY->SetRelativeScale(RelativeScaleFactor);

	ArrowX = AddComponent<UArrowComponent>();
	ArrowX->SetRotation({ 0.0f, 0.0f, -90.0f });
	ArrowX->SetColor({1.f, 0.f, 0.f, 1.f});
	ArrowX->SetRelativeScale(RelativeScaleFactor);

	ArrowZ = AddComponent<UArrowComponent>();
	ArrowZ->SetColor({ 0.f, 0.f, 1.f, 1.f });
	ArrowZ->SetRotation({ 90.0f, 0.0f, 0.0f });
	ArrowZ->SetRelativeScale(RelativeScaleFactor);

	BasePoint = AddComponent<USphereComponent>();
	BasePoint->SetColor({ 1.f, 1.f, 1.f, 1.f });
	BasePoint->SetRelativeScale({0.1f, 0.1f, 0.1f }); // Root 및 BasePoint의 절대크기(Arrow의 Relative Scale이 이에 대한 상대크기임.)

	ArrowY->SetAlwaysOnTop(true);
	ArrowX->SetAlwaysOnTop(true);
	ArrowZ->SetAlwaysOnTop(true);
	BasePoint->SetAlwaysOnTop(true);

	// Sphere가 Base
	RootComponent = BasePoint;
}

void ULocationGizmoActor::Tick(float DeltaTime)
{
	AActor::Tick(DeltaTime);
	UWorld* World = GetWorld();
	
	if (!World || !World->SelectedActor || World->GetCurrentMode() != UWorld::EGizmoMode::Location)
	{
		BasePoint->SetScale({ 0.f, 0.f, 0.f });
		if (ArrowX) ArrowX->SetScale({ 0.f, 0.f, 0.f });
		if (ArrowY) ArrowY->SetScale({ 0.f, 0.f, 0.f });
		if (ArrowZ) ArrowZ->SetScale({ 0.f, 0.f, 0.f });
		return;
	}

	FEditorViewportClient* Viewport = GetWorld()->ViewPort;
	float Distance = (Viewport->GetViewLocation() - RootComponent->GetPosition()).Length();
	float ScaleFactor = Distance * 0.15f;
	FVector ResultScale = { ScaleFactor, ScaleFactor, ScaleFactor };

	BasePoint->SetScale({ 0.1f * ScaleFactor, 0.1f * ScaleFactor, 0.1f * ScaleFactor });
	if (ArrowX) ArrowX->SetScale({ ScaleFactor, ScaleFactor , ScaleFactor });
	if (ArrowY) ArrowY->SetScale({ ScaleFactor, ScaleFactor , ScaleFactor });
	if (ArrowZ) ArrowZ->SetScale({ ScaleFactor, ScaleFactor , ScaleFactor });

	//BoundingSphere->SetScale(ResultScale);

	// 일단 모두 기본 색상으로 초기화
	ArrowX->SetColor(ColorX);
	ArrowY->SetColor(ColorY);
	ArrowZ->SetColor(ColorZ);
	BasePoint->SetColor(ColorCenter);

	// 월드로부터 현재 드래그 중인 축 확인
	EGizmoAxis ActiveAxis = EGizmoAxis::None;

	if (World)
	{
		// 드래그 중인 축이 있으면 무조건 그 축을 활성화
		ActiveAxis = World->GetDraggingAxis();

		// 드래그 중이 아니라면, World가 방금 PreparePicking()에서 판정해둔 Hover 축을 가져옴
		if (ActiveAxis == EGizmoAxis::None)
		{
			// 직접 CheckGizmoPicking()을 호출하지 않고 World의 상태를 읽습니다.
			ActiveAxis = World->GetHoveredAxis();
		}
	}

	// ActiveAxis 결과에 따라 주황색 불 켜기
	switch (ActiveAxis)
	{
	case EGizmoAxis::X: ArrowX->SetColor(ColorHover); break;
	case EGizmoAxis::Y: ArrowY->SetColor(ColorHover); break;
	case EGizmoAxis::Z: ArrowZ->SetColor(ColorHover); break;
	case EGizmoAxis::Center: BasePoint->SetColor(ColorHover); break;
	case EGizmoAxis::None: break;
	}
}

FVector ULocationGizmoActor::GetDragIntersectionPoint(const FVector& RayOrg, const FVector& RayDir, EGizmoAxis Axis)
{
	if (!RootComponent) return FVector::Zero;

	FVector GizmoPos = RootComponent->GetPosition();

	// Center (가운데 구체)를 잡았을 때는 드래그 시작 시 고정된 평면과의 교차점을 구합니다.
	if (Axis == EGizmoAxis::Center)
	{
		return Math::RayPlaneIntersection(RayOrg, RayDir, LockedPlaneNormal, GizmoPos);
	}

	// 특정 축(X, Y, Z)을 잡았을 때는 해당 축의 선분과 가장 가까운 점을 구합니다.
	FVector AxisDir;
	switch (Axis)
	{
	case EGizmoAxis::X: AxisDir = ArrowX->GetUpVector(); break;
	case EGizmoAxis::Y: AxisDir = ArrowY->GetUpVector(); break;
	case EGizmoAxis::Z: AxisDir = ArrowZ->GetUpVector(); break;
	default: return GizmoPos;
	}

	return Math::ClosestPointOnLine(RayOrg, RayDir, GizmoPos, AxisDir);
}

EGizmoAxis ULocationGizmoActor::CheckGizmoPicking()
{
	UWorld* World = GetWorld();
	if (!World || !World->ViewPort) return EGizmoAxis::None;

	UInput& Input = UInput::GetInstance();
	FVector2D ScreenPos = { static_cast<float>(Input.GetMousePosition().x), static_cast<float>(Input.GetMousePosition().y) };
	FVector RayOrigin;
	FVector RayDirection;

	World->ViewPort->DeprojectScreenToWorld(ScreenPos, RayOrigin, RayDirection);

	// 구체(Center)를 가장 먼저 검사 (제일 작고 중앙에 있으므로)
	if (World->RayIntersectsMesh(RayOrigin, RayDirection, BasePoint->GetMesh(), BasePoint->GetComponentTransform()) >= 0.f)
		return EGizmoAxis::Center;

	// 각 화살표 검사 (이미 회전이 적용된 상태이므로 그대로 넘김)
	if (World->RayIntersectsMesh(RayOrigin, RayDirection, ArrowX->GetMesh(), ArrowX->GetComponentTransform()) >= 0.f) return EGizmoAxis::X;
	if (World->RayIntersectsMesh(RayOrigin, RayDirection, ArrowY->GetMesh(), ArrowY->GetComponentTransform()) >= 0.f) return EGizmoAxis::Y;
	if (World->RayIntersectsMesh(RayOrigin, RayDirection, ArrowZ->GetMesh(), ArrowZ->GetComponentTransform()) >= 0.f) return EGizmoAxis::Z;

	return EGizmoAxis::None;
}

void ULocationGizmoActor::LockDragPlane(const FVector& RayOrg)
{
	FVector PlaneNormal = RayOrg - RootComponent->GetPosition();
	PlaneNormal.Normalize();
	LockedPlaneNormal = PlaneNormal;
}

void ULocationGizmoActor::Release()
{
	AActor::Release();
}
