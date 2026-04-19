#include "pch.h"
#include "World.h"

#include "ResourceManager.h"
#include "ObjectFactory.h"
#include "Level.h"
#include "Actor.h"
#include "Object.h"

#include "LocationGizmoActor.h"
#include "RotationGizmoActor.h"
#include "ScaleGizmoActor.h"

#include "PrimitiveComponent.h"
#include "SphereComponent.h"
#include "CubeComponent.h"
#include "ArrowComponent.h"
#include "RingComponent.h"
#include "HammerComponent.h"
#include "TriangleComponent.h"

#include "ImGuiDrawer.h"
#include "FEditorViewportClient.h"

#include "Input.h"
#include "Mesh.h"

void UWorld::InitWorld(UResourceManager& ResourceManager, FEditorViewportClient* _ViewPort)
{
	CurrentLevel = UObjectFactory::NewObject<ULevel>();
	CurrentLevel->OwningWorld = this;
	ViewPort = _ViewPort;
	resourceManager = &ResourceManager;

	AActor* CubeActor = SpawnActor<AActor>();
	UCubeComponent* CubeComponent = CubeActor->AddComponent<UCubeComponent>();
	CubeComponent->SetMesh(ResourceManager.FindMeshData("Cube"));
	CubeActor->RootComponent = CubeComponent;

	CubeComponent->SetPosition({ 0.0f, 0.0f, 3.0f }); // 카메라 앞에 배치
	CubeComponent->SetScale({ 0.5f, 0.5f, 0.5f });

	// 초기 상태 명시적 세팅
	CurrentMode = EGizmoMode::Location;
	SelectedActor = nullptr;
	HoveredAxis = EGizmoAxis::None;
	bIsDragging = false;
}

void UWorld::Tick(float DeltaTime)
{
	if (!CurrentLevel) return;

	UInput& Input = UInput::GetInstance();

	if (!bIsDragging) {
		if (Input.IsKeyDown('Z'))
		{
			SetGizmoMode(EGizmoMode::Location);
		}
		if (Input.IsKeyDown('X'))
		{
			SetGizmoMode(EGizmoMode::Rotation);
		}
		if (Input.IsKeyDown('C'))
		{
			SetGizmoMode(EGizmoMode::Scale);
		}
	}

	if (Input.IsKeyDown(VK_SPACE))
	{
		EGizmoMode NextMode = EGizmoMode::Location;

		// 현재 모드에 따라 다음 모드 결정 (Location -> Rotation -> Scale -> Location)
		switch (CurrentMode)
		{
		case EGizmoMode::Location: NextMode = EGizmoMode::Rotation; break;
		case EGizmoMode::Rotation: NextMode = EGizmoMode::Scale; break;
		case EGizmoMode::Scale:    NextMode = EGizmoMode::Location; break;
		}

		SetGizmoMode(NextMode);
	}

	for (uint32 i = 0; i < CurrentLevel->Actors.Size(); ++i)
	{
		CurrentLevel->Actors[i]->Tick(DeltaTime);
	}

	FVector2D ScreenPos = { static_cast<float>(Input.GetMousePosition().x), static_cast<float>(Input.GetMousePosition().y) };
	FVector RayDirection;
	FVector RayOrigin;
	ViewPort->DeprojectScreenToWorld(ScreenPos, RayOrigin, RayDirection);

	// TODO: 직관적인 이름으로 Rename 필요합니다
	// 기즈모 축 피킹 수행 
	PreparePicking();

	// TODO: 조건 정리
	// 드래그 시작 (초기 상태 저장)
	if (Input.IsKeyDown(VK_LBUTTON))
	{
		
		
		// 기즈모 축을 잡은 상태
		if (HoveredAxis != EGizmoAxis::None && SelectedActor)
		{
			bIsDragging = true;
			CurrentDraggingAxis = HoveredAxis;

			switch (CurrentMode)
			{
			case EGizmoMode::Location:
				GizmoStartLocation = LocationGizmoActor->RootComponent->GetPosition();
				LocationGizmoActor->LockDragPlane(RayOrigin);
				DragStartPoint = LocationGizmoActor->GetDragIntersectionPoint(RayOrigin, RayDirection, CurrentDraggingAxis);
				break;

			case EGizmoMode::Rotation:
				TargetStartRotation = SelectedActor->RootComponent->GetRotation();
				RotationGizmoActor->LockDragPlane(CurrentDraggingAxis);
				DragStartPoint = RotationGizmoActor->GetDragIntersectionPoint(RayOrigin, RayDirection, CurrentDraggingAxis);
				break;

			case EGizmoMode::Scale:
				TargetStartScale = SelectedActor->RootComponent->GetScale();
				TargetStartRotation = SelectedActor->RootComponent->GetRotation();
				DragStartPoint = ScaleGizmoActor->GetDragIntersectionPoint(RayOrigin, RayDirection, CurrentDraggingAxis);
				break;
			}
		}
		// 기즈모 축이 아닌, 액터 자체를 잡은 상태
		else if (!bIsDragging)
		{
			AActor* HitActor = RaycastForActor(RayOrigin, RayDirection);

			if (HitActor != SelectedActor)
			{
				if (SelectedActor)
				{
					SelectedActor->SetSelected(false); // 이전에 선택된 액터가 있다면 선택 해제
				}
				SelectedActor = HitActor; // 새로 선택된 액터로 업데이트

				if (SelectedActor)
				{
					SelectedActor->SetSelected(true); // 새로 선택된 액터가 있다면 선택 표시
				}
				RefreshGizmo();
			}
		}
	}

	// TODO: 조건 정리
	// 드래그 진행 중
	if (Input.IsKeyPressing(VK_LBUTTON) && bIsDragging && SelectedActor)
	{
		if (CurrentMode == EGizmoMode::Location && LocationGizmoActor)
		{
			FVector CurrentPoint = LocationGizmoActor->GetDragIntersectionPoint(RayOrigin, RayDirection, CurrentDraggingAxis);
			FVector Delta = CurrentPoint - DragStartPoint;
			LocationGizmoActor->RootComponent->SetPosition(GizmoStartLocation + Delta);
			SelectedActor->RootComponent->SetPosition(GizmoStartLocation + Delta);
		}
		else if (CurrentMode == EGizmoMode::Rotation && RotationGizmoActor)
		{
			// 마우스의 현재 교차점을 구하고, 시작점과의 회전 차이(DeltaAngle)를 계산합니다.
			FVector CurrentPoint = RotationGizmoActor->GetDragIntersectionPoint(RayOrigin, RayDirection, CurrentDraggingAxis);
			float DeltaAngle = RotationGizmoActor->GetRotationDelta(CurrentPoint, DragStartPoint, CurrentDraggingAxis);

			// 큐브의 시작 오일러 각도를 '회전 행렬'로 승격시킵니다.
			FMatrix StartMatrix = FMatrix::MakeRotation(TargetStartRotation);

			// 기즈모의 회전 변화량(Delta)을 3D 회전 행렬로 만듭니다.
			FMatrix DeltaMatrix;
			if (CurrentDraggingAxis == EGizmoAxis::X)
				DeltaMatrix = FMatrix::MakeRotationX(DeltaAngle);
			else if (CurrentDraggingAxis == EGizmoAxis::Y)
				DeltaMatrix = FMatrix::MakeRotationY(DeltaAngle);
			else if (CurrentDraggingAxis == EGizmoAxis::Z)
				DeltaMatrix = FMatrix::MakeRotationZ(DeltaAngle);
			else
				DeltaMatrix = FMatrix::Identity;

			// 행렬 곱셈 수행! 
			FMatrix FinalMatrix = StartMatrix * DeltaMatrix;

			// 완성된 행렬에서 오일러 각도를 다시 뽑아내어 큐브에 먹입니다.
			FVector FinalEuler = Math::MatrixToEuler(FinalMatrix);
			SelectedActor->RootComponent->SetRotation(FinalEuler);

			// 2. 기즈모 전체가 아닌, '잡고 있는 링' 하나만 제자리에서 돌립니다!
			RotationGizmoActor->ApplyRingRotation(CurrentDraggingAxis, DeltaAngle);
		}
		else if (CurrentMode == EGizmoMode::Scale && ScaleGizmoActor)
		{
			FVector CurrentPoint = ScaleGizmoActor->GetDragIntersectionPoint(RayOrigin, RayDirection, CurrentDraggingAxis);
			FVector Delta = CurrentPoint - DragStartPoint;

			FVector NewScale = TargetStartScale;
			float Sensitivity = 0.15f;
			float DragAmount = 0.0f;

			// 🚨 [수정] Delta의 절대 좌표(X,Y,Z)가 아닌, 각 기즈모 축이 바라보는 방향(UpVector)으로의 내적(투영 길이)을 구합니다.
			if (CurrentDraggingAxis == EGizmoAxis::X)
			{
				DragAmount = Delta.Dot(ScaleGizmoActor->HammerX->GetUpVector());
				NewScale.X += DragAmount * Sensitivity;
			}
			else if (CurrentDraggingAxis == EGizmoAxis::Y)
			{
				DragAmount = Delta.Dot(ScaleGizmoActor->HammerY->GetUpVector());
				NewScale.Y += DragAmount * Sensitivity;
			}
			else if (CurrentDraggingAxis == EGizmoAxis::Z)
			{
				DragAmount = Delta.Dot(ScaleGizmoActor->HammerZ->GetUpVector());
				NewScale.Z += DragAmount * Sensitivity;
			}
			else if (CurrentDraggingAxis == EGizmoAxis::Center)
			{
				// 전체 스케일링은 기존처럼 이동량의 합을 사용
				float UniformDelta = (Delta.X + Delta.Y + Delta.Z) * Sensitivity;
				NewScale = NewScale + FVector(UniformDelta, UniformDelta, UniformDelta);
			}

			// 0 이하로 작아져서 메쉬가 뒤집히는 것 방지
			if (NewScale.X < 0.01f) NewScale.X = 0.01f;
			if (NewScale.Y < 0.01f) NewScale.Y = 0.01f;
			if (NewScale.Z < 0.01f) NewScale.Z = 0.01f;

			SelectedActor->RootComponent->SetScale(NewScale);
		}
	}

	// 드래그 종료
	if (Input.IsKeyUp(VK_LBUTTON))
	{
		bIsDragging = false;
		CurrentDraggingAxis = EGizmoAxis::None;
		if (CurrentMode == EGizmoMode::Rotation && RotationGizmoActor)
		{
			RotationGizmoActor->ApplyRingRotation(EGizmoAxis::None, 0.0f);
		}
	}

	if (SelectedActor)
	{
		FVector CurrentLocation = SelectedActor->RootComponent->GetPosition();
		switch (CurrentMode)
		{
		case EGizmoMode::Location:
			LocationGizmoActor->RootComponent->SetPosition(CurrentLocation);
			break;
		case EGizmoMode::Rotation:
			RotationGizmoActor->RootComponent->SetPosition(CurrentLocation);
			break;
		case EGizmoMode::Scale:
			ScaleGizmoActor->RootComponent->SetPosition(CurrentLocation);
			break;
		}
	}
}

void UWorld::PreparePicking()
{
	HoveredAxis = EGizmoAxis::None;
	if (bIsDragging) return;

	if (CurrentMode == EGizmoMode::Location && LocationGizmoActor)
	{
		HoveredAxis = LocationGizmoActor->CheckGizmoPicking();
	}
	else if (CurrentMode == EGizmoMode::Rotation && RotationGizmoActor)
	{
		HoveredAxis = RotationGizmoActor->CheckGizmoPicking();
	}
	else if (CurrentMode == EGizmoMode::Scale && ScaleGizmoActor)
	{
		HoveredAxis = ScaleGizmoActor->CheckGizmoPicking();
	}
}

AActor* UWorld::RaycastForActor(const FVector& RayOrigin, const FVector& RayDirection)
{
	auto ActorArray = CurrentLevel->Actors;

	AActor* ClosestActor = nullptr;
	float MinT = 1e9;

	for (size_t ActorIndex = 0; ActorIndex < ActorArray.Size(); ++ActorIndex)
	{
		AActor* TargetActor = ActorArray[ActorIndex];
		// 기즈모를 담고 있는 액터 자체는 피킹 검수에서 제외합니다.
		if (LocationGizmoActor == TargetActor || RotationGizmoActor == TargetActor || ScaleGizmoActor == TargetActor) continue;

		// 구와 레이 충돌로 불필요한 정점 순회를 막음
		//if (!RayIntersectsSphere(RayOrigin, RayDirection, TargetActor->RootComponent, TargetActor->RootComponent->GetComponentTransform()))
		//{
		//	continue;
		//}

		TArray<UPrimitiveComponent*> PrimitiveComponents = TargetActor->GetComponentArrayByClass<UPrimitiveComponent>();

		for (size_t ComponentIndex = 0; ComponentIndex < PrimitiveComponents.Size(); ++ComponentIndex)
		{
			UPrimitiveComponent* Primitive = PrimitiveComponents[ComponentIndex];
			if (!Primitive) continue;

			FMatrix ModelWorld = Primitive->GetComponentTransform();

			float T = RayIntersectsMesh(RayOrigin, RayDirection, Primitive->GetMesh(), ModelWorld);
			if (T > 0.f && T < MinT)
			{
				MinT = T;
				ClosestActor = TargetActor;
			}
		}
	}

	return ClosestActor;
}

// 공용 매시 충돌검사 함수 
float UWorld::RayIntersectsMesh(const FVector& RayOrigin, const FVector& RayDirection, const UMesh* Mesh, const FMatrix& WorldMatrix)
{
	if (!Mesh) return -1.f;

	FVector LocalRayOrigin = FMatrix::TransformCoord(RayOrigin, WorldMatrix.Inverse());
	FVector LocalRayDirection = FMatrix::TransformNormal(RayDirection, WorldMatrix.Inverse());

	const FVertexSimple* BufferData = static_cast<const FVertexSimple*>(Mesh->GetVertexData());

	float MinT = 1e9;
	for (uint64 i = 0; i < Mesh->GetVertexCount(); i += 3)
	{
		FVector V0 = { BufferData[i].X, BufferData[i].Y, BufferData[i].Z };
		FVector V1 = { BufferData[i + 1].X, BufferData[i + 1].Y, BufferData[i + 1].Z };
		FVector V2 = { BufferData[i + 2].X, BufferData[i + 2].Y, BufferData[i + 2].Z };

		float T;
		if (Math::RayIntersectsTriangle(LocalRayOrigin, LocalRayDirection, V0, V1, V2, T))
		{
			MinT = min(MinT, T);
		}
	}

	return (MinT == 1e9) ? -1.f : MinT;
}

bool UWorld::RayIntersectsSphere(const FVector& RayOrigin, const FVector& RayDir, const USceneComponent* SceneComponent, const FMatrix& WorldMatrix)
{
	if (!SceneComponent) return false;

	//구와 직선의 충돌 방정식을 구한다.
	//(Center - 레이의 한점) ^ 2 =  Radius ^ 2 ;
	//여기선 근의 공식을 사용하여 충돌 여부를 확인한다.

	FVector CenterToOrigin = (RayOrigin - SceneComponent->GetComponentLocation());
	FVector ObjectScale = SceneComponent->GetScale();
	float Radius = max(max(ObjectScale.X, ObjectScale.Y), ObjectScale.Z) * 3.f;

	float a = RayDir.Dot(RayDir);
	float b = 2 * CenterToOrigin.Dot(RayDir);
	float c = CenterToOrigin.Dot(CenterToOrigin) - Radius * Radius;

	return (b * b - 4 * a * c >= 0);
}

void UWorld::Release()
{
	// World는 참조만 정리
	CurrentLevel = nullptr;
}

void UWorld::SpawnActorFromEditor(FSpawnParameters params)
{
	for (int i = 0; i < params.Count; i++)
	{
		AActor* actor = SpawnActor<AActor>();
		if (params.bOverrideUUID)
			actor->UUID = params.UUID;

		if (params.PrimitiveType == "Cube")
		{
			UCubeComponent* Cube = actor->AddComponent<UCubeComponent>();
			Cube->SetMesh(resourceManager->FindMeshData(params.PrimitiveType));
			actor->RootComponent = Cube;
		}
		else if (params.PrimitiveType == "Sphere")
		{
			USphereComponent* Sphere = actor->AddComponent<USphereComponent>();
			Sphere->SetMesh(resourceManager->FindMeshData(params.PrimitiveType));
			actor->RootComponent = Sphere;
		}
		else if (params.PrimitiveType == "Triangle")
		{
			UTriangleComponent* Triangle = actor->AddComponent<UTriangleComponent>();
			Triangle->SetMesh(resourceManager->FindMeshData(params.PrimitiveType));
			actor->RootComponent = Triangle;
		}
		else
		{
			continue;
		}

		actor->RootComponent->SetPosition(params.Location);
		actor->RootComponent->SetRotation(params.Rotation);
		actor->RootComponent->SetScale(params.Scale);
	}
}

bool UWorld::IsGizmoActor(AActor* Actor) const
{
	return Actor == LocationGizmoActor ||
		Actor == RotationGizmoActor ||
		Actor == ScaleGizmoActor;
}

void UWorld::ClearScene()
{
	TArray<AActor*> RemaingActors;

	for (size_t i = 0; i < CurrentLevel->Actors.Size(); ++i)
	{
		if (IsGizmoActor(CurrentLevel->Actors[i]))
		{
			RemaingActors.PushBack(CurrentLevel->Actors[i]);
			continue;
		}
		UObjectFactory::DestroyObject(CurrentLevel->Actors[i]);
	}

	CurrentLevel->Actors = RemaingActors;
}

TArray<AActor*> UWorld::GetSerializableActors() const
{
	TArray<AActor*> Result;

	for (size_t i = 0; i < CurrentLevel->Actors.Size(); ++i)
	{
		if (IsGizmoActor(CurrentLevel->Actors[i])) 
			continue;
		
		Result.PushBack(CurrentLevel->Actors[i]);
	}

	return Result;
}

void UWorld::SetGizmoMode(EGizmoMode NewMode)
{
	if (CurrentMode == NewMode) return;

	CurrentMode = NewMode;

	RefreshGizmo();
}

void UWorld::RefreshGizmo()
{
	DestroyCurrentGizmo();

	if (!SelectedActor)
	{
		return;
	}

	FVector ActorLocation = SelectedActor->RootComponent->GetPosition();

	switch (CurrentMode)
	{
	case EGizmoMode::Location:
		LocationGizmoActor = SpawnActor<ULocationGizmoActor>();
		LocationGizmoActor->RootComponent->SetPosition(ActorLocation);
		LocationGizmoActor->ArrowX->SetMesh(resourceManager->FindMeshData("GizmoLocation"));
		LocationGizmoActor->ArrowY->SetMesh(resourceManager->FindMeshData("GizmoLocation"));
		LocationGizmoActor->ArrowZ->SetMesh(resourceManager->FindMeshData("GizmoLocation"));
		LocationGizmoActor->BasePoint->SetMesh(resourceManager->FindMeshData("Sphere"));

		break;

	case EGizmoMode::Rotation:
		RotationGizmoActor = SpawnActor<URotationGizmoActor>();
		RotationGizmoActor->RootComponent->SetPosition(ActorLocation);
		RotationGizmoActor->RingX->SetMesh(resourceManager->FindMeshData("GizmoRotation"));
		RotationGizmoActor->RingY->SetMesh(resourceManager->FindMeshData("GizmoRotation"));
		RotationGizmoActor->RingZ->SetMesh(resourceManager->FindMeshData("GizmoRotation"));
		break;

	case EGizmoMode::Scale:
		ScaleGizmoActor = SpawnActor<UScaleGizmoActor>();
		ScaleGizmoActor->RootComponent->SetPosition(ActorLocation);
		ScaleGizmoActor->HammerX->SetMesh(resourceManager->FindMeshData("GizmoScale"));
		ScaleGizmoActor->HammerY->SetMesh(resourceManager->FindMeshData("GizmoScale"));
		ScaleGizmoActor->HammerZ->SetMesh(resourceManager->FindMeshData("GizmoScale"));
		ScaleGizmoActor->BasePoint->SetMesh(resourceManager->FindMeshData("Cube"));
		break;
	}
}

void UWorld::DestroyCurrentGizmo()
{
	AActor* GizmosToDestroy[] = { LocationGizmoActor, RotationGizmoActor, ScaleGizmoActor };

	for (AActor* Gizmo : GizmosToDestroy)
	{
		if (Gizmo)
		{
			// CurrentLevel->Actors 배열에서 제거
			for (uint32 i = 0; i < CurrentLevel->Actors.Size(); ++i)
			{
				if (CurrentLevel->Actors[i] == Gizmo)
				{
					uint32 LastIndex = CurrentLevel->Actors.Size() - 1;
					if (i != LastIndex)
					{
						std::swap(CurrentLevel->Actors[i], CurrentLevel->Actors[LastIndex]);
						--i;
					}
					UObjectFactory::DestroyObject(CurrentLevel->Actors[LastIndex]);
					CurrentLevel->Actors.PopBack();
					break;
				}
			}
		}
	}

	LocationGizmoActor = nullptr;
	RotationGizmoActor = nullptr;
	ScaleGizmoActor = nullptr;
}