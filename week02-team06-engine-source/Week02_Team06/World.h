#pragma once
#include "Object.h"
#include "Level.h"
#include "ImGuiDrawer.h"

class UMesh;
class AActor;
class UResourceManager;
class FEditorViewportClient;
class USceneComponent;

class UWorld : public UObject
{
public:
	UWorld() = default;
	virtual ~UWorld() = default;

public:
	/** Actor를 생성하고 PersistentLevel에 등록한 뒤 BeginPlay 호출 */
	template<typename T>
	T* SpawnActor()
	{
		static_assert(std::is_base_of_v<AActor, T>, "T must derive from AActor");

		T* Actor           = NewObject<T>();

		Actor->OwningLevel = CurrentLevel;

		if (CurrentLevel)
		{
			(CurrentLevel->Actors).PushBack(Actor);
		}

		Actor->BeginPlay();
		return Actor;
	}

	virtual void Release() override;

public:
	enum class EGizmoMode { Location, Rotation, Scale };
	EGizmoMode GetCurrentMode() const { return CurrentMode; }
	
	virtual void InitWorld(UResourceManager& ResourceManager, FEditorViewportClient* _ViewPort);
	void Tick(float DeltaTime);
	
	void SpawnActorFromEditor(FSpawnParameters params);
	float RayIntersectsMesh(const FVector& RayOrigin, const FVector& RayDir, const UMesh* Mesh, const FMatrix& WorldMatrix); // 공용 매시 충돌검사 함수 
	bool RayIntersectsSphere(const FVector& RayOrigin, const FVector& RayDir, const USceneComponent* SceneComponent, const FMatrix& WorldMatrix); // 구 충돌 검사함수

	void PreparePicking();
	EGizmoAxis GetDraggingAxis() const { return bIsDragging ? CurrentDraggingAxis : EGizmoAxis::None; }
	EGizmoAxis GetHoveredAxis() const { return HoveredAxis; }
	AActor* RaycastForActor(const FVector& RayOrigin, const FVector& RayDirection);
	
	void ClearScene();
	TArray<AActor*> GetSerializableActors() const;
	void RefreshGizmo();
	void DestroyCurrentGizmo();
	void SetGizmoMode(EGizmoMode NewMode);

private:
	class ULocationGizmoActor* LocationGizmoActor = { nullptr };
	class URotationGizmoActor* RotationGizmoActor = { nullptr };
	class UScaleGizmoActor* ScaleGizmoActor = { nullptr };

	bool IsGizmoActor(AActor* Actor) const;

public:
	// 월드가 시작할 때 초기 레벨
	// 원래는 월드가 바뀌어도 그려지는 PersisteneLevel과 StreamingLevel로 구별됨
	ULevel* CurrentLevel = { nullptr };

	FEditorViewportClient* ViewPort = { nullptr };

	UResourceManager* resourceManager;

	AActor* SelectedActor = { nullptr };

private:
	// 드래그 관련 상태 변수
	EGizmoMode CurrentMode = EGizmoMode::Location;
	EGizmoAxis HoveredAxis = EGizmoAxis::None;
	EGizmoAxis CurrentDraggingAxis = EGizmoAxis::None; // 현재 잡고 있는 축
	FVector DragStartPoint = { 0.f, 0.f, 0.f };        // 클릭한 시점의 3D 축 위 좌표
	FVector GizmoStartLocation = { 0.f, 0.f, 0.f };    // 클릭한 시점의 기즈모 위치
	FVector GizmoStartRotation = { 0.f, 0.f, 0.f };    // 클릭한 시점의 기즈모 회전
	FVector TargetStartRotation = { 0.f, 0.f, 0.f };   // 클릭한 시점의 타겟 액터 회전 (회전 모드용)
	FVector TargetStartScale = { 1.f, 1.f, 1.f };      // 클릭한 시점의 타겟 액터 스케일 (스케일 모드용)
	bool bIsDragging = false;    // 드래그 중인지 여부

};

