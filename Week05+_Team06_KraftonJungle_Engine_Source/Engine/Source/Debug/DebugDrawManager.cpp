#include "DebugDrawManager.h"
#include "Renderer/Renderer.h"
#include "Core/ShowFlags.h"
#include "World/World.h"
#include "Actor/Actor.h"
#include "Actor/CameraActor.h"
#include "Camera/Camera.h"
#include "Component/CameraComponent.h"
#include "Component/PrimitiveComponent.h"
#include "Component/UUIDBillboardComponent.h"
#include "Component/SubUVComponent.h"
#include "Component/SkyComponent.h"
#include "Object/Class.h"
void FDebugDrawManager::DrawLine(const FVector& Start, const FVector& End, const FVector4& Color)
{
	Lines.push_back({ Start, End, Color });
}

void FDebugDrawManager::DrawCube(const FVector& Center, const FVector& Extent, const FVector4& Color)
{
	Cubes.push_back({ Center, Extent, Color });
}

void FDebugDrawManager::DrawWorldAxis(float Length)
{
	bDrawWorldAxis = true;
}

void FDebugDrawManager::Flush(FRenderer* Renderer, const FShowFlags& ShowFlags, UWorld* World, bool bClearAfterFlush)
{

	if (!Renderer) return;

	//// 디버그 드로우 전체 꺼져있으면 스킵
	//if (!ShowFlags.HasFlag(EEngineShowFlags::SF_DebugDraw))
	//{
	//	if (bClearAfterFlush)
	//	{
	//		Clear();
	//	}
	//	return;
	//}

	// CameraActor 전방 화살표 - SF_DebugDraw 여부와 무관하게 Editor 월드에서 항상 표시.
	// PIE/Game 월드에서는 카메라 자체가 플레이어 시점이므로 화살표를 그리지 않는다.
	bool bCameraArrowsDrawn = false;
	if (World && World->GetWorldType() == EWorldType::Editor)
	{
		DrawCameraActors(Renderer, World);
		bCameraArrowsDrawn = true;
	}

	// 디버그 드로우 전체 꺼져있으면 카메라 화살표만 플러시하고 종료
	if (!ShowFlags.HasFlag(EEngineShowFlags::SF_DebugDraw))
	{
		if (bCameraArrowsDrawn)
		{
			Renderer->ExecuteLineCommands();
		}
		if (bClearAfterFlush)
		{
			Clear();
		}
		return;
	}


	if (ShowFlags.HasFlag(EEngineShowFlags::SF_Collision) && World)
	{
		DrawAllCollisionBounds(Renderer, World);
	}

	for (const auto& Cube : Cubes)
	{
		Renderer->DrawCube(Cube.Center, Cube.Extent, Cube.Color);
	}
	for (const auto& Line : Lines)
	{
		Renderer->DrawLine(Line.Start, Line.End, Line.Color);
	}


	// 월드 축
	if (ShowFlags.HasFlag(EEngineShowFlags::SF_WorldAxis))
	{
		Renderer->DrawLine({ 0,0,0 }, { 1000,0,0 }, { 1,0,0,1 });  // X: Red
		Renderer->DrawLine({ 0,0,0 }, { 0,1000,0 }, { 0,1,0,1 });  // Y: Green
		Renderer->DrawLine({ 0,0,0 }, { 0,0,1000 }, { 0,0,1,1 });  // Z: Blue
	}

	Renderer->ExecuteLineCommands();
	if (bClearAfterFlush)
	{
		Clear();
	}
}

void FDebugDrawManager::Clear()
{
	Lines.clear();
	Cubes.clear();
	bDrawWorldAxis = false;
}

void FDebugDrawManager::DrawAllCollisionBounds(FRenderer* Renderer, UWorld* World)
{
	TArray<AActor*> AllActors = World->GetAllActors();
	for (AActor* Actor : AllActors)
	{
		if (!Actor || Actor->IsPendingDestroy() || !Actor->IsVisible())
			continue;

		for (UActorComponent* Comp : Actor->GetComponents())
		{
			if (!Comp || !Comp->IsA(UPrimitiveComponent::StaticClass()))
				continue;

			UPrimitiveComponent* PrimComp = static_cast<UPrimitiveComponent*>(Comp);

			if (!PrimComp->ShouldDrawDebugBounds())
				continue;

			FBoxSphereBounds Bound = PrimComp->GetWorldBounds();

			// 바운드 크기가 유효한 경우에만 박스를 그립니다.
			if (Bound.BoxExtent.SizeSquared() > 0.0f)
			{
				Renderer->DrawCube(Bound.Center, Bound.BoxExtent, FVector4(1.0f, 0.0f, 0.0f, 1.0f)); // 빨간색
			}
		}
	}
}

void FDebugDrawManager::DrawCameraActors(FRenderer* Renderer, UWorld* World)
{
	constexpr float ArrowLength = 0.6f;
	constexpr float HeadLength = 0.2f;
	constexpr float HeadWidth = 0.1f;
	const FVector4 ArrowColor = { 1.0f, 1.0f , 0.0f , 1.0f };
	
	TArray<AActor*> AllActors = World->GetAllActors();
	for (AActor* Actor : AllActors)
	{
		if (!Actor || Actor->IsPendingDestroy() || !Actor->IsVisible()) continue;
		if (!Actor->IsA(ACameraActor::StaticClass())) continue;
	
		ACameraActor* CamActor = static_cast<ACameraActor*>(Actor);
		UCameraComponent* CamComp = CamActor->GetCameraComponent();
		if (!CamComp) continue;
		
		FCamera* Camera = CamComp->GetCamera();
		if (!Camera) continue;

		const FMatrix& WorldTM = CamComp->GetWorldTransform();
		const FVector Origin = WorldTM.GetTranslation();
		const FVector Forward = WorldTM.GetForwardVector().GetSafeNormal();
		const FVector Right = WorldTM.GetRightVector().GetSafeNormal();
		const FVector Up = FVector::CrossProduct(Forward, Right).GetSafeNormal();

		const FVector Tip = Origin + Forward * ArrowLength;
		const FVector HeadBase = Tip - Forward * HeadLength;

	}
}